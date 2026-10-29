#include "updater.h"

#include <bcrypt.h>
#include <winhttp.h>

#include <cstdio>
#include <vector>

#include "../core/version.h"
#include "platform.h"

namespace {
struct Url { std::wstring host, path; INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT; bool https = true; };

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
  std::wstring agent = L"Cue/" + widen(CUE_VERSION) + L" (Windows; +https://github.com/" + widen(CUE_UPDATE_REPO) + L")";
  HINTERNET ses = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
  if (!ses) ses = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);  // before Windows 8.1
  if (!ses) { err = "open"; return false; }
  WinHttpSetTimeouts(ses, 8000, 8000, 15000, 30000);
  DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
  WinHttpSetOption(ses, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof redirect);
  bool ok = false;
  HINTERNET con = WinHttpConnect(ses, u.host.c_str(), u.port, 0);
  HINTERNET req = con ? WinHttpOpenRequest(con, L"GET", u.path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                      : nullptr;
  if (req) {
    std::wstring headers = std::wstring(L"Accept: ") + accept + L"\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
    if (WinHttpSendRequest(req, headers.c_str(), (DWORD)-1, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(req, nullptr)) {
      DWORD status = 0, len = sizeof status;
      WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                          &status, &len, WINHTTP_NO_HEADER_INDEX);
      if (status == 200) {
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
  } else err = "request";
  if (req) WinHttpCloseHandle(req);
  if (con) WinHttpCloseHandle(con);
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
  std::wstring d = std::wstring(t) + L"Cue-update";
  CreateDirectoryW(d.c_str(), nullptr);
  return d;
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
  HANDLE f = CreateFileW(part.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  auto fail = [&](const std::string& why) {
    auto* m = new UpdateMsg;
    m->kind = UpdateMsg::Failed;
    m->manual = true;
    m->rel = rel;
    m->error = "download: " + why;
    post(job->hwnd, job->msg, m);
  };
  if (f == INVALID_HANDLE_VALUE) { fail("temp file"); delete job; return 0; }
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
  if (!rel.sha256.empty() && digest != rel.sha256) { DeleteFileW(part.c_str()); fail("sha256 mismatch"); delete job; return 0; }
  DeleteFileW(final_path.c_str());
  if (!MoveFileW(part.c_str(), final_path.c_str())) { fail("rename"); delete job; return 0; }
  auto* m = new UpdateMsg;
  m->kind = UpdateMsg::Downloaded;
  m->manual = true;
  m->rel = rel;
  m->file = final_path;
  m->error = rel.sha256.empty() ? "verified by size" : "verified sha256 " + digest;
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
