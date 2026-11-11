#include "../core/update_temp.h"
#include "updater.h"

#include <bcrypt.h>
#include <winhttp.h>

#include <cstdio>
#include <vector>

#include "../core/version.h"
#include "platform.h"

#include <wintrust.h>
#include <softpub.h>

namespace {
struct Url { std::wstring host, path; INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT; bool https = true; };

// The publisher certificate an update must carry. Empty while the project is
// unsigned: the digest is then the only check, and the log says so.
std::vector<std::string> publisher_thumbprints() {
  std::vector<std::string> out;
  const std::string all = CUE_PUBLISHER_THUMBPRINTS;
  std::string part;
  for (char c : all) {
    if (c == ',' || c == ';' || c == ' ') {
      if (!part.empty()) out.push_back(part);
      part.clear();
    } else part += (char)std::tolower((unsigned char)c);
  }
  if (!part.empty()) out.push_back(part);
  return out;
}

// The update comes from GitHub and nowhere else. A release that points its
// asset at another host is not followed: the digest of a release is published
// by the same account as the file, so the trust is in the host as much as in
// the hash.
bool allowed_host(const std::wstring& host) {
  return _wcsicmp(host.c_str(), L"github.com") == 0 ||
         _wcsicmp(host.c_str(), L"api.github.com") == 0 ||
         _wcsicmp(host.c_str(), L"objects.githubusercontent.com") == 0 ||
         _wcsicmp(host.c_str(), L"release-assets.githubusercontent.com") == 0;
}

bool crack(const std::wstring& url, Url& out) {
  URL_COMPONENTS uc{sizeof(uc)};
  wchar_t host[256] = {}, path[2048] = {};
  uc.lpszHostName = host; uc.dwHostNameLength = 255;
  uc.lpszUrlPath = path; uc.dwUrlPathLength = 2047;
  wchar_t extra[2048] = {};
  uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 2047;
  if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return false;
  out.host = host;
  out.path = std::wstring(path) + extra;
  out.port = uc.nPort;
  out.https = uc.nScheme == INTERNET_SCHEME_HTTPS;
  return out.https;  // never plain http
}

// One GET. Follows GitHub's redirect to its download host (https only). `sink` receives the
// body in chunks; `total` gets Content-Length when known.
template <class Sink>
bool http_get(const std::wstring& url, const wchar_t* accept, Sink sink, std::string& err, int64_t* total = nullptr) {
  Url u;
  if (!crack(url, u)) { err = "bad url"; return false; }
  if (!allowed_host(u.host)) { err = "host not allowed"; return false; }
  std::wstring agent = L"Cue/" + widen(CUE_VERSION) + L" (Windows; +https://github.com/" + widen(CUE_UPDATE_REPO) + L")";
  HINTERNET ses = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_NAME, 0);
  if (!ses) ses = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);  // before Windows 8.1
  if (!ses) { err = "open"; return false; }
  WinHttpSetTimeouts(ses, 8000, 8000, 15000, 30000);
  // The redirects are followed here, one at a time: a redirect to a host we do
  // not know must not be taken.
  DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  WinHttpSetOption(ses, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof redirect);
  bool ok = false;
  std::wstring current = url;
  for (int hop = 0; hop < 6 && !ok; ++hop) {
    if (!crack(current, u)) { err = "bad url"; break; }
    if (!allowed_host(u.host)) { err = "host not allowed"; break; }
    HINTERNET con = WinHttpConnect(ses, u.host.c_str(), u.port, 0);
    HINTERNET req = con ? WinHttpOpenRequest(con, L"GET", u.path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                        : nullptr;
    if (!req) { err = "request"; if (con) WinHttpCloseHandle(con); break; }
    std::wstring headers = std::wstring(L"Accept: ") + accept + L"\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
    DWORD status = 0, len = sizeof status;
    if (WinHttpSendRequest(req, headers.c_str(), (DWORD)-1, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(req, nullptr)) {
      WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                          &status, &len, WINHTTP_NO_HEADER_INDEX);
      if (status == 301 || status == 302 || status == 307 || status == 308) {
        // GitHub sends the file on to its own download host: an absolute
        // https address, and one we know.
        wchar_t location[2048] = {};
        DWORD loc_len = sizeof location;
        if (WinHttpQueryHeaders(req, WINHTTP_QUERY_LOCATION | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                 location, &loc_len, WINHTTP_NO_HEADER_INDEX) && loc_len > 1) {
          std::wstring next(location);
          Url check;
          if (crack(next, check) && allowed_host(check.host)) current = next;
          else err = "redirect not allowed";
        } else err = "redirect";
      } else if (status == 200) {
        if (total) {
          wchar_t cl[32] = {};
          DWORD cll = sizeof cl;
          *total = WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, cl, &cll,
                                       WINHTTP_NO_HEADER_INDEX) ? _wtoi64(cl) : 0;
        }
        std::vector<char> buf(64 * 1024);
        ok = true;
        for (;;) {
          DWORD got = 0;
          if (!WinHttpReadData(req, buf.data(), (DWORD)buf.size(), &got)) { ok = false; err = "read"; break; }
          if (got == 0) break;
          if (!sink(buf.data(), got)) { ok = false; err = "sink"; break; }
        }
      } else err = "http " + std::to_string(status);
    } else err = "send " + std::to_string(GetLastError());
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(con);
  }
  if (err.empty() && !ok) err = "no response";
  WinHttpCloseHandle(ses);
  return ok;
}

struct Sha256 {
  BCRYPT_ALG_HANDLE alg = nullptr;
  BCRYPT_HASH_HANDLE h = nullptr;
  bool ok = false;
  Sha256() {
    ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0 &&
         BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) >= 0;
  }
  ~Sha256() {
    if (h) BCryptDestroyHash(h);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
  }
  void add(const void* p, ULONG n) { if (ok) ok = BCryptHashData(h, (PUCHAR)p, n, 0) >= 0; }
  std::string hex() {
    unsigned char d[32];
    if (!ok || BCryptFinishHash(h, d, 32, 0) < 0) return {};
    char o[65];
    for (int i = 0; i < 32; ++i) std::snprintf(o + i * 2, 3, "%02x", d[i]);
    return std::string(o, 64);
  }
};

void post(HWND hwnd, UINT msg, UpdateMsg* m) {
  if (!PostMessageW(hwnd, msg, 0, (LPARAM)m)) delete m;
}

// The Authenticode signature of a file.
//
// Returns:
//   1  the file carries a valid signature (and, when thumbprints are
//      configured, one of them)
//   0  the file carries no signature at all
//  -1  the file carries a signature that is not trusted, or not one of ours
// `who` gets the name of the signer, for the log.
int publisher_signature(const std::wstring& file, std::string& who) {
  who.clear();
  WINTRUST_FILE_INFO fi{sizeof fi};
  fi.pcwszFilePath = file.c_str();
  WINTRUST_DATA td{sizeof td};
  td.dwUIChoice = WTD_UI_NONE;
  td.fdwRevocationChecks = WTD_REVOKE_NONE;  // the check must not need the network
  td.dwUnionChoice = WTD_CHOICE_FILE;
  td.pFile = &fi;
  td.dwStateAction = WTD_STATEACTION_VERIFY;
  GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
  const LONG status = WinVerifyTrust(nullptr, &action, &td);
  int result = -1;
  if (status != 0) {
    // no signature at all is not a failure, it is the state of an unsigned build
    if (GetLastError() == (DWORD)TRUST_E_NOSIGNATURE) result = 0;
  } else {
    // The signer's certificate: its thumbprint is what we compare, its name
    // goes to the log. The state is still open here, which is when the helpers
    // can read it.
    auto* state = WTHelperProvDataFromStateData(&td);
    auto* signer = state ? WTHelperGetProvSignerFromChain(state, 0, FALSE, 0) : nullptr;
    auto* cert = signer ? WTHelperGetProvCertFromChain(signer, 0) : nullptr;
    if (cert) {
      CERT_NAME_BLOB subject{};
      subject.pbData = cert->pCert->pCertInfo->Subject.pbData;
      subject.cbData = cert->pCert->pCertInfo->Subject.cbData;
      const DWORD name_size = CertNameToStrA(X509_ASN_ENCODING, &subject, CERT_X500_NAME_STR, nullptr, 0);
      if (name_size) {
        std::vector<char> name(name_size);
        if (CertNameToStrA(X509_ASN_ENCODING, &subject, CERT_X500_NAME_STR, name.data(), (DWORD)name.size()))
          who = name.data();
      }
      char hash[64] = {};
      DWORD hash_size = sizeof hash;
      if (CertGetCertificateContextProperty(cert->pCert, CERT_SHA1_HASH_PROP_ID, hash, &hash_size) && hash_size <= sizeof hash) {
        std::string thumb;
        for (DWORD i = 0; i + 1 < hash_size; ++i) {
          char b[4];
          std::snprintf(b, sizeof b, "%02x", (unsigned char)hash[i]);
          thumb += b;
        }
        const auto wanted = publisher_thumbprints();
        if (wanted.empty()) result = 1;  // signed, but by whom we do not pin
        else for (const auto& w : wanted) if (w == thumb) result = 1;
      }
    }
  }
  td.dwStateAction = WTD_STATEACTION_CLOSE;
  WinVerifyTrust(nullptr, &action, &td);
  return result;
}

// The update before it is run: its digest matched, and now its publisher.
// `err` gets the reason when the update must not be run.
bool publisher_ok(const std::wstring& file, std::string& err, std::string& note) {
  std::string who;
  const int signature = publisher_signature(file, who);
  const auto wanted = publisher_thumbprints();
  if (signature < 0) {
    err = signature == -1 && !who.empty() ? "the publisher is not trusted" : "the signature is not valid";
    if (!who.empty()) err += " (" + who + ")";
    return false;
  }
  if (signature == 0) {
    if (!wanted.empty()) {
      err = "the installer is not signed by a known publisher";
      return false;
    }
    note = "the installer carries no signature (the project does not sign its releases)";
    return true;
  }
  note = who.empty() ? "the installer is signed" : "the installer is signed by " + who;
  return true;
}
struct Job { HWND hwnd; UINT msg; bool manual; ReleaseInfo rel; };

DWORD WINAPI check_main(void* p) {
  Job* job = static_cast<Job*>(p);
  auto* m = new UpdateMsg;
  m->kind = UpdateMsg::Checked;
  m->manual = job->manual;
  std::string body, err;
  std::wstring url = L"https://api.github.com/repos/" + widen(CUE_UPDATE_REPO) + L"/releases/latest";
  bool got = http_get(url, L"application/vnd.github+json", [&](const char* d, DWORD n) {
    if (body.size() + n > 4 * 1024 * 1024) return false;  // a release page is a few KB
    body.append(d, n);
    return true;
  }, err);
  if (!got) { m->kind = UpdateMsg::Failed; m->error = "check: " + err; }
  else m->found = parse_release(body, m->rel);
  if (got && !m->found) m->error = "no installer in the latest release";
  post(job->hwnd, job->msg, m);
  delete job;
  return 0;
}

std::wstring temp_dir() {
  wchar_t t[MAX_PATH + 1] = {};
  GetTempPathW(MAX_PATH, t);
  unsigned char random[16];
  if (!t[0] || BCryptGenRandom(nullptr, random, sizeof random, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) return {};
  std::wstring d = std::wstring(t) + L"Cue-update-";
  const wchar_t* hex = L"0123456789abcdef";
  for (auto b : random) { d += hex[b >> 4]; d += hex[b & 15]; }
  return CreateDirectoryW(d.c_str(), nullptr) ? d : std::wstring();
}

std::wstring safe_name(const std::string& name) {
  std::wstring w = widen(name);
  for (auto& c : w)
    if (c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?' || c == L'"' || c == L'<' || c == L'>' || c == L'|')
      c = L'_';
  if (w.empty() || w.find(L"..") != std::wstring::npos) w = L"Cue-Setup.exe";
  return w;
}

DWORD WINAPI download_main(void* p) {
  Job* job = static_cast<Job*>(p);
  const ReleaseInfo& rel = job->rel;
  std::wstring dir = temp_dir(), final_path = dir + L"\\" + safe_name(rel.asset_name), part = final_path + L".part";
  HANDLE f = dir.empty() || rel.sha256.empty() ? INVALID_HANDLE_VALUE :
      CreateFileW(part.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  auto fail = [&](const std::string& why) {
    if(!dir.empty()) {DeleteFileW(part.c_str());RemoveDirectoryW(dir.c_str());}
    auto* m = new UpdateMsg;
    m->kind = UpdateMsg::Failed;
    m->manual = true;
    m->rel = rel;
    m->error = "download: " + why;
    post(job->hwnd, job->msg, m);
  };
  if (f == INVALID_HANDLE_VALUE) { fail(rel.sha256.empty() ? "missing sha256 digest" : "temp file"); delete job; return 0; }
  Sha256 sha;
  int64_t total = rel.size, got = 0;
  int last_pct = -1;
  bool mz = false;
  std::string err;
  bool ok = http_get(widen(rel.asset_url), L"application/octet-stream", [&](const char* d, DWORD n) {
    if (got == 0 && n >= 2) mz = d[0] == 'M' && d[1] == 'Z';
    DWORD wrote = 0;
    if (!WriteFile(f, d, n, &wrote, nullptr) || wrote != n) return false;
    sha.add(d, n);
    got += n;
    if (total > 0) {
      int pct = (int)std::min<int64_t>(99, got * 100 / total);
      if (pct != last_pct) {
        last_pct = pct;
        auto* m = new UpdateMsg;
        m->kind = UpdateMsg::Progress;
        m->percent = pct;
        post(job->hwnd, job->msg, m);
      }
    }
    return got < 512ll * 1024 * 1024;  // nothing we publish is that large
  }, err, total > 0 ? nullptr : &total);
  CloseHandle(f);
  std::string digest = sha.hex();
  if (!ok) { DeleteFileW(part.c_str()); fail(err); delete job; return 0; }
  if (!mz) { DeleteFileW(part.c_str()); fail("not an executable"); delete job; return 0; }
  if (rel.size > 0 && got != rel.size) { DeleteFileW(part.c_str()); fail("size mismatch"); delete job; return 0; }
  if (digest != rel.sha256) { DeleteFileW(part.c_str()); fail("sha256 mismatch"); delete job; return 0; }
  DeleteFileW(final_path.c_str());
  if (!MoveFileW(part.c_str(), final_path.c_str())) { fail("rename"); delete job; return 0; }
  // The digest matched. Now the publisher: the file is only run if it is
  // signed by one of ours (when the project signs its releases).
  std::string why, note;
  if (!publisher_ok(final_path, why, note)) {
    DeleteFileW(final_path.c_str());
    fail("publisher: " + why);
    delete job;
    return 0;
  }
  auto* m = new UpdateMsg;
  m->kind = UpdateMsg::Downloaded;
  m->manual = true;
  m->rel = rel;
  m->file = final_path;
  m->error = "verified sha256 " + digest + "; " + note;
  post(job->hwnd, job->msg, m);
  delete job;
  return 0;
}

void spawn(LPTHREAD_START_ROUTINE fn, Job* job) {
  HANDLE th = CreateThread(nullptr, 0, fn, job, 0, nullptr);
  if (th) {
    SetThreadPriority(th, THREAD_PRIORITY_BELOW_NORMAL);  // never in the way of playback
    CloseHandle(th);
  } else {
    auto* m = new UpdateMsg;
    m->kind = UpdateMsg::Failed;
    m->manual = job->manual;
    m->error = "thread";
    post(job->hwnd, job->msg, m);
    delete job;
  }
}
}  // namespace

void update_check_async(HWND hwnd, UINT msg, bool manual) { spawn(check_main, new Job{hwnd, msg, manual, {}}); }

void update_download_async(HWND hwnd, UINT msg, const ReleaseInfo& rel) {
  spawn(download_main, new Job{hwnd, msg, true, rel});
}

bool update_launch_installer(const std::wstring& installer, const std::wstring& install_dir) {
  std::wstring cmd = L"\"" + installer + L"\" --update --wait " + std::to_wstring(GetCurrentProcessId()) + L" --dir \"" +
                     install_dir + L"\"";
  STARTUPINFOW si{sizeof(si)};
  PROCESS_INFORMATION pi{};
  std::vector<wchar_t> buf(cmd.begin(), cmd.end());
  buf.push_back(0);
  if (!CreateProcessW(installer.c_str(), buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) return false;
  // let the installer come to the front once we close
  AllowSetForegroundWindow(pi.dwProcessId);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return true;
}

namespace {
DWORD WINAPI cleanup_updates(void*) {
  wchar_t temp[MAX_PATH+1]{};
  DWORD length=GetTempPathW(MAX_PATH,temp);
  if(!length||length>=MAX_PATH)return 0;
  std::wstring root=temp;
  FILETIME now{};GetSystemTimeAsFileTime(&now);
  ULARGE_INTEGER current;current.LowPart=now.dwLowDateTime;current.HighPart=now.dwHighDateTime;
  constexpr uint64_t age=7ull*24*3600*10000000;
  WIN32_FIND_DATAW dir{};
  HANDLE dirs=FindFirstFileW((root+L"Cue-update-*").c_str(),&dir);
  if(dirs==INVALID_HANDLE_VALUE)return 0;
  unsigned visited=0;
  do {
    if(++visited>64)break;
    if(!(dir.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||(dir.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||!update_temp_directory_name(dir.cFileName))continue;
    ULARGE_INTEGER changed;changed.LowPart=dir.ftLastWriteTime.dwLowDateTime;changed.HighPart=dir.ftLastWriteTime.dwHighDateTime;
    if(current.QuadPart<=changed.QuadPart||current.QuadPart-changed.QuadPart<age)continue;
    const std::wstring folder=root+dir.cFileName;
    WIN32_FIND_DATAW entry{};
    HANDLE files=FindFirstFileW((folder+L"\\*").c_str(),&entry);
    if(files!=INVALID_HANDLE_VALUE) {
      unsigned seen=0;
      do {
        if(++seen>16)break;
        if(entry.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))continue;
        std::wstring name=entry.cFileName;
        if(name.ends_with(L".exe")||name.ends_with(L".exe.part"))DeleteFileW((folder+L"\\"+name).c_str());
      }while(FindNextFileW(files,&entry));
      FindClose(files);
    }
    RemoveDirectoryW(folder.c_str()); // only succeeds if empty
  }while(FindNextFileW(dirs,&dir));
  FindClose(dirs);return 0;
}
}
void update_cleanup_async() {
  HANDLE worker=CreateThread(nullptr,0,cleanup_updates,nullptr,CREATE_SUSPENDED,nullptr);
  if(worker){
    SetThreadPriority(worker,THREAD_PRIORITY_BELOW_NORMAL);
    if(ResumeThread(worker)==(DWORD)-1) TerminateThread(worker,0);
    CloseHandle(worker);
  }
}
