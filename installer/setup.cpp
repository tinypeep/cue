// Cue-Setup.exe: installs, updates and uninstalls Cue for the current user.
//
//   Cue-Setup.exe                         install (or upgrade an existing install)
//   Cue-Setup.exe --silent                same, without a window (exit code 0 = ok)
//   Cue-Setup.exe --update --wait <pid> --dir <folder>
//                                         started by Cue itself: waits for it to close,
//                                         installs over <folder>, opens Cue again
//   <folder>\uninstall.exe --uninstall    the entry in Windows' "Apps" settings
//
// Per-user by default (%LOCALAPPDATA%\Programs\Cue): no administrator prompt, and Cue can
// update itself later without one. Everything Cue writes outside its folder is listed here
// so the uninstaller can take it back out.
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <windowsx.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "../src/core/i18n.h"
#include "../src/core/release.h"
#include "../src/win/ui_language.h"
#include "../src/core/version.h"
#include "../src/ui/anim.h"
#include "lzma/LzmaDec.h"
#include "payload.h"
#include "setup_ui.h"

namespace fs = std::filesystem;

namespace {
// ── small helpers ───────────────────────────────────────────────────
std::wstring widen(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, 0);
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
  return w;
}
std::string narrow(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, 0);
  WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
  return s;
}
std::wstring known_folder(REFKNOWNFOLDERID id) {
  wchar_t* p = nullptr;
  std::wstring out;
  if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &p))) out = p;
  CoTaskMemFree(p);
  return out;
}
std::wstring self_path() {
  std::wstring buf(MAX_PATH * 4, 0);
  DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
  buf.resize(n);
  return buf;
}
bool is_elevated() {
  HANDLE tok = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
  TOKEN_ELEVATION e{};
  DWORD len = 0;
  bool r = GetTokenInformation(tok, TokenElevation, &e, sizeof e, &len) && e.TokenIsElevated;
  CloseHandle(tok);
  return r;
}
std::wstring reg_read(HKEY root, const std::wstring& key, const wchar_t* name) {
  wchar_t v[MAX_PATH * 4]{};
  DWORD bytes = sizeof v;
  if (RegGetValueW(root, key.c_str(), name, RRF_RT_REG_SZ, nullptr, v, &bytes) != ERROR_SUCCESS) return {};
  return v;
}
bool reg_set(HKEY root, const std::wstring& key, const wchar_t* name, const std::wstring& val) {
  HKEY k;
  if (RegCreateKeyExW(root, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
  LONG r = RegSetValueExW(k, name, 0, REG_SZ, (const BYTE*)val.c_str(), (DWORD)((val.size() + 1) * sizeof(wchar_t)));
  RegCloseKey(k);
  return r == ERROR_SUCCESS;
}
bool reg_set_dword(HKEY root, const std::wstring& key, const wchar_t* name, DWORD v) {
  HKEY k;
  if (RegCreateKeyExW(root, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
  LONG r = RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE*)&v, sizeof v);
  RegCloseKey(k);
  return r == ERROR_SUCCESS;
}

const wchar_t* kUninstallKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Cue";
const wchar_t* kAppPathKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\Cue.exe";
const wchar_t* kMediaExts[] = {L".mkv", L".mp4", L".m4v", L".mov", L".avi", L".webm", L".wmv", L".ts", L".m2ts", L".mpg",
                               L".mpeg", L".ogv", L".ogg", L".oga", L".opus", L".mp3", L".m4a", L".aac", L".wav", L".flac",
                               L".aif", L".aiff", L".wma", L".mka"};

// ── payload ─────────────────────────────────────────────────────────
struct Payload {
  bool ok = false;
  PayloadFooter footer{};
};

Payload read_footer(const std::wstring& exe) {
  Payload p;
  HANDLE f = CreateFileW(exe.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (f == INVALID_HANDLE_VALUE) return p;
  LARGE_INTEGER size{};
  GetFileSizeEx(f, &size);
  if (size.QuadPart > (LONGLONG)sizeof(PayloadFooter)) {
    LARGE_INTEGER at;
    at.QuadPart = size.QuadPart - sizeof(PayloadFooter);
    DWORD got = 0;
    if (SetFilePointerEx(f, at, nullptr, FILE_BEGIN) && ReadFile(f, &p.footer, sizeof p.footer, &got, nullptr) &&
        got == sizeof p.footer && std::equal(kPayloadMagic, kPayloadMagic + 8, p.footer.magic) &&
        p.footer.offset + p.footer.size + sizeof(PayloadFooter) == (uint64_t)size.QuadPart)
      p.ok = true;
  }
  CloseHandle(f);
  return p;
}

void* lz_alloc(ISzAllocPtr, size_t n) { return std::malloc(n); }
void lz_free(ISzAllocPtr, void* p) { std::free(p); }
const ISzAlloc kAlloc = {lz_alloc, lz_free};

// ── the job ─────────────────────────────────────────────────────────
struct Job {
  SetupMode mode = SetupMode::Install;
  std::wstring dir, self;
  Payload payload;
  bool desktop = true, launch = true, keep = true;
  DWORD wait_pid = 0;
  std::atomic<double> progress{0};
  std::atomic<int> result{0};  // 0 running, 1 done, 2 failed, 3 needs administrator rights
  std::mutex mu;
  std::string status, error;
  void say(const std::string& s) { std::lock_guard<std::mutex> l(mu); status = s; }
  void fail(const std::string& e) { { std::lock_guard<std::mutex> l(mu); error = e; } result = 2; }
};

// Closes a running Cue (it saves where you were) and waits for it to go.
void close_running_cue(Job& job) {
  std::vector<HANDLE> procs;
  HWND w = nullptr;
  while ((w = FindWindowExW(nullptr, w, L"tinyoffice.Cue", nullptr))) {
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    if (HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid)) procs.push_back(h);
    PostMessageW(w, WM_CLOSE, 0, 0);
  }
  if (job.wait_pid)
    if (HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, job.wait_pid)) procs.push_back(h);
  if (procs.empty()) return;
  job.say(tr(str::setup_waiting));
  for (HANDLE h : procs) {
    WaitForSingleObject(h, 15000);
    CloseHandle(h);
  }
  Sleep(150);  // let the preview helper (killed with the player) release the engine DLL
}

// Writes a file, retrying for a moment if something still holds the old one.
bool write_file(const std::wstring& path, const unsigned char* data, size_t n, DWORD* err) {
  for (int attempt = 0; attempt < 40; ++attempt) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
      *err = GetLastError();
      if (*err == ERROR_SHARING_VIOLATION || *err == ERROR_LOCK_VIOLATION || (*err == ERROR_ACCESS_DENIED && attempt < 10)) {
        Sleep(100);
        continue;
      }
      return false;
    }
    size_t done = 0;
    bool ok = true;
    while (done < n) {
      DWORD chunk = (DWORD)std::min<size_t>(n - done, 1 << 24), wrote = 0;
      if (!WriteFile(f, data + done, chunk, &wrote, nullptr) || wrote != chunk) { ok = false; *err = GetLastError(); break; }
      done += chunk;
    }
    CloseHandle(f);
    return ok;
  }
  return false;
}

std::vector<std::wstring> read_manifest(const std::wstring& dir) {
  std::vector<std::wstring> out;
  std::ifstream f(fs::path(dir) / L"install.txt", std::ios::binary);
  std::string line;
  while (std::getline(f, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    if (line.find("..") != std::string::npos || line.find(':') != std::string::npos || line[0] == '/' || line[0] == '\\') continue;
    out.push_back(widen(line));
  }
  return out;
}

bool make_shortcut(const std::wstring& lnk, const std::wstring& target, const std::wstring& dir, const std::wstring& desc) {
  IShellLinkW* sl = nullptr;
  if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&sl))) return false;
  sl->SetPath(target.c_str());
  sl->SetWorkingDirectory(dir.c_str());
  sl->SetDescription(desc.c_str());
  sl->SetIconLocation(target.c_str(), 0);
  IPersistFile* pf = nullptr;
  bool ok = SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void**)&pf)) && SUCCEEDED(pf->Save(lnk.c_str(), TRUE));
  if (pf) pf->Release();
  sl->Release();
  return ok;
}

std::wstring start_menu_link() { return known_folder(FOLDERID_Programs) + L"\\Cue.lnk"; }
std::wstring desktop_link() { return known_folder(FOLDERID_Desktop) + L"\\Cue.lnk"; }

void run_hidden(const std::wstring& exe, const std::wstring& args, DWORD wait_ms) {
  std::wstring cmd = L"\"" + exe + L"\" " + args;
  STARTUPINFOW si{sizeof(si)};
  PROCESS_INFORMATION pi{};
  if (CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    WaitForSingleObject(pi.hProcess, wait_ms);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  }
}

// Opens Cue as the person, never with the installer's rights (an elevated Cue could not
// receive files dragged from Explorer).
void open_cue(const std::wstring& dir) {
  std::wstring exe = dir + L"\\Cue.exe";
  if (is_elevated()) {
    ShellExecuteW(nullptr, L"open", L"explorer.exe", (L"\"" + exe + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
    return;
  }
  STARTUPINFOW si{sizeof(si)};
  PROCESS_INFORMATION pi{};
  std::wstring cmd = L"\"" + exe + L"\"";
  if (CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) {
    AllowSetForegroundWindow(pi.dwProcessId);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  }
}

// ── install ─────────────────────────────────────────────────────────
void install(Job& job) {
  HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  auto done = [&] { if (SUCCEEDED(com)) CoUninitialize(); };
  job.say(tr(str::setup_preparing));
  close_running_cue(job);

  // the folder, and whether we may write there
  int r = SHCreateDirectoryExW(nullptr, job.dir.c_str(), nullptr);
  if (r != ERROR_SUCCESS && r != ERROR_ALREADY_EXISTS && r != ERROR_FILE_EXISTS) {
    if (r == ERROR_ACCESS_DENIED && !is_elevated()) { job.result = 3; done(); return; }
    job.fail(i18n::format(str::setup_mkdir_failed, {{"code", std::to_string(r)}}));
    done();
    return;
  }
  {
    std::wstring probe = job.dir + L"\\.cue-write-test";
    HANDLE f = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
      DWORD e = GetLastError();
      if (e == ERROR_ACCESS_DENIED && !is_elevated()) { job.result = 3; done(); return; }
      job.fail(tr(str::setup_no_write));
      done();
      return;
    }
    CloseHandle(f);
  }

  // read and unpack the archive (0 .. 55%)
  job.say(tr(str::setup_opening));
  const PayloadFooter& ft = job.payload.footer;
  std::vector<unsigned char> packed(ft.size), plain(ft.unpacked);
  {
    HANDLE f = CreateFileW(job.self.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    LARGE_INTEGER at;
    at.QuadPart = (LONGLONG)ft.offset;
    DWORD got = 0;
    size_t total = 0;
    bool ok = f != INVALID_HANDLE_VALUE && SetFilePointerEx(f, at, nullptr, FILE_BEGIN);
    while (ok && total < packed.size()) {
      DWORD want = (DWORD)std::min<size_t>(packed.size() - total, 1 << 22);
      ok = ReadFile(f, packed.data() + total, want, &got, nullptr) && got > 0;
      total += got;
    }
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    if (!ok || packed.size() < 13) { job.fail(tr(str::setup_incomplete)); done(); return; }
  }
  {
    CLzmaDec dec;
    LzmaDec_Construct(&dec);
    if (LzmaDec_AllocateProbs(&dec, packed.data(), LZMA_PROPS_SIZE, &kAlloc) != SZ_OK) {
      job.fail(tr(str::setup_invalid));
      done();
      return;
    }
    dec.dic = plain.data();
    dec.dicBufSize = plain.size();
    LzmaDec_Init(&dec);
    size_t in_pos = 13;  // 5 bytes of properties + 8 of size
    ELzmaStatus status = LZMA_STATUS_NOT_SPECIFIED;
    bool ok = true;
    while (dec.dicPos < plain.size()) {
      SizeT in_len = std::min<size_t>(packed.size() - in_pos, 1 << 20);
      SRes res = LzmaDec_DecodeToDic(&dec, plain.size(), packed.data() + in_pos, &in_len, LZMA_FINISH_ANY, &status);
      in_pos += in_len;
      if (res != SZ_OK || (in_len == 0 && dec.dicPos < plain.size() && status != LZMA_STATUS_NOT_FINISHED)) { ok = false; break; }
      if (status == LZMA_STATUS_FINISHED_WITH_MARK) break;
      job.progress = 0.55 * dec.dicPos / (double)plain.size();
    }
    LzmaDec_FreeProbs(&dec, &kAlloc);
    if (!ok || dec.dicPos != plain.size() || crc32_update(0, plain.data(), plain.size()) != ft.crc) {
      job.fail(tr(str::setup_corrupt));
      done();
      return;
    }
  }

  // write the files (55 .. 90%)
  std::vector<std::wstring> previous = read_manifest(job.dir);
  std::vector<std::wstring> written;
  size_t at = 0, bytes_done = 0;
  auto u16 = [&](size_t i) { return (size_t)plain[i] | ((size_t)plain[i + 1] << 8); };
  auto u64 = [&](size_t i) { uint64_t v = 0; for (int k = 7; k >= 0; --k) v = (v << 8) | plain[i + k]; return v; };
  for (uint64_t n = 0; n < ft.files; ++n) {
    if (at + 2 > plain.size()) break;
    size_t plen = u16(at);
    at += 2;
    if (at + plen + 8 > plain.size()) break;
    std::string rel((const char*)plain.data() + at, plen);
    at += plen;
    uint64_t size = u64(at);
    at += 8;
    if (at + size > plain.size() || rel.find("..") != std::string::npos || rel.find(':') != std::string::npos) {
      job.fail(tr(str::setup_invalid));
      done();
      return;
    }
    std::wstring wrel = widen(rel);
    std::replace(wrel.begin(), wrel.end(), L'/', L'\\');
    std::wstring dst = job.dir + L"\\" + wrel;
    SHCreateDirectoryExW(nullptr, fs::path(dst).parent_path().c_str(), nullptr);
    job.say(i18n::format(str::setup_copying, {{"file", rel.substr(rel.find_last_of('/') == std::string::npos ? 0 : rel.find_last_of('/') + 1)}}));
    DWORD err = 0;
    if (!write_file(dst, plain.data() + at, (size_t)size, &err)) {
      if (err == ERROR_ACCESS_DENIED && !is_elevated()) { job.result = 3; done(); return; }
      job.fail(i18n::format(str::setup_write_failed, {{"file", rel}, {"code", std::to_string(err)}}));
      done();
      return;
    }
    written.push_back(wrel);
    at += size;
    bytes_done += size;
    job.progress = 0.55 + 0.35 * bytes_done / (double)std::max<size_t>(1, plain.size());
  }
  plain.clear();
  plain.shrink_to_fit();

  // what the previous version had and this one does not
  std::set<std::wstring> now_set;
  for (auto& w : written) { std::wstring l = w; CharLowerW(l.data()); now_set.insert(l); }
  for (auto& old : previous) {
    std::wstring l = old;
    CharLowerW(l.data());
    if (!now_set.count(l) && l != L"uninstall.exe") DeleteFileW((job.dir + L"\\" + old).c_str());
  }

  // the uninstaller: this same program, without the archive
  job.say(tr(str::setup_registering));
  {
    std::ifstream in(fs::path(job.self), std::ios::binary);
    std::vector<char> stub((size_t)ft.offset);
    in.read(stub.data(), (std::streamsize)stub.size());
    DWORD err = 0;
    if (in) write_file(job.dir + L"\\uninstall.exe", (const unsigned char*)stub.data(), stub.size(), &err);
  }
  {
    std::ofstream m(fs::path(job.dir) / L"install.txt", std::ios::binary | std::ios::trunc);
    m << "# Cue " << CUE_VERSION << " - files installed here (the uninstaller reads this list)\n";
    for (auto& w : written) m << narrow(w) << "\n";
    m << "uninstall.exe\n";
  }
  job.progress = 0.93;

  // shortcuts, Apps & features, App Paths
  std::wstring exe = job.dir + L"\\Cue.exe";
  make_shortcut(start_menu_link(), exe, job.dir, widen(tr(str::setup_shortcut)));
  if (job.desktop) make_shortcut(desktop_link(), exe, job.dir, widen(tr(str::setup_shortcut)));
  else if (job.mode == SetupMode::Install) DeleteFileW(desktop_link().c_str());
  std::wstring un = L"\"" + job.dir + L"\\uninstall.exe\" --uninstall";
  uint64_t kb = 0;
  for (auto& w : written) {
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (GetFileAttributesExW((job.dir + L"\\" + w).c_str(), GetFileExInfoStandard, &a)) kb += a.nFileSizeLow / 1024;
  }
  reg_set(HKEY_CURRENT_USER, kUninstallKey, L"DisplayName", L"Cue");
  reg_set(HKEY_CURRENT_USER, kUninstallKey, L"DisplayVersion", widen(CUE_VERSION));
  reg_set(HKEY_CURRENT_USER, kUninstallKey, L"Publisher", L".tinypeep");
  reg_set(HKEY_CURRENT_USER, kUninstallKey, L"DisplayIcon", exe + L",0");
  reg_set(HKEY_CURRENT_USER, kUninstallKey, L"InstallLocation", job.dir);
  reg_set(HKEY_CURRENT_USER, kUninstallKey, L"UninstallString", un);
  reg_set(HKEY_CURRENT_USER, kUninstallKey, L"QuietUninstallString", un + L" --silent");
  reg_set(HKEY_CURRENT_USER, kUninstallKey, L"URLInfoAbout", L"https://github.com/" + widen(CUE_UPDATE_REPO));
  reg_set(HKEY_CURRENT_USER, kUninstallKey, L"URLUpdateInfo", L"https://github.com/" + widen(CUE_UPDATE_REPO) + L"/releases");
  reg_set_dword(HKEY_CURRENT_USER, kUninstallKey, L"NoModify", 1);
  reg_set_dword(HKEY_CURRENT_USER, kUninstallKey, L"NoRepair", 1);
  reg_set_dword(HKEY_CURRENT_USER, kUninstallKey, L"EstimatedSize", (DWORD)kb);
  reg_set(HKEY_CURRENT_USER, kAppPathKey, nullptr, exe);
  reg_set(HKEY_CURRENT_USER, kAppPathKey, L"Path", job.dir);
  job.progress = 0.96;

  // "Abrir com" in Explorer: Cue registers itself (the same code it runs on every start)
  job.say(tr(str::setup_open_with));
  run_hidden(exe, L"--register", 6000);
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
  job.progress = 1.0;
  job.say(tr(str::setup_done));
  job.result = 1;
  done();
}

// ── uninstall ───────────────────────────────────────────────────────
void uninstall(Job& job) {
  HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  job.say(tr(str::setup_closing));
  close_running_cue(job);
  job.progress = 0.1;
  std::vector<std::wstring> files = read_manifest(job.dir);
  if (files.empty()) files = {L"Cue.exe", L"libmpv-2.dll"};  // a folder without a list: only what is surely ours
  job.say(tr(str::setup_removing));
  std::set<std::wstring> dirs;
  for (size_t i = 0; i < files.size(); ++i) {
    std::wstring p = job.dir + L"\\" + files[i];
    SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
    DeleteFileW(p.c_str());
    for (fs::path d = fs::path(p).parent_path(); d.wstring().size() > job.dir.size(); d = d.parent_path()) dirs.insert(d.wstring());
    job.progress = 0.1 + 0.6 * (i + 1) / files.size();
  }
  DeleteFileW((job.dir + L"\\install.txt").c_str());
  std::vector<std::wstring> sorted(dirs.begin(), dirs.end());
  std::sort(sorted.begin(), sorted.end(), [](const std::wstring& a, const std::wstring& b) { return a.size() > b.size(); });
  for (auto& d : sorted) RemoveDirectoryW(d.c_str());  // only the empty ones go
  RemoveDirectoryW(job.dir.c_str());

  job.say(tr(str::setup_cleaning));
  // shortcuts that point at this install
  DeleteFileW(start_menu_link().c_str());
  DeleteFileW(desktop_link().c_str());
  // Apps & features, App Paths, "Abrir com"
  RegDeleteTreeW(HKEY_CURRENT_USER, kUninstallKey);
  RegDeleteTreeW(HKEY_CURRENT_USER, kAppPathKey);
  RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\tinyoffice.Cue.video");
  RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\Applications\\Cue.exe");
  for (auto e : kMediaExts)
    RegDeleteKeyValueW(HKEY_CURRENT_USER, (std::wstring(L"Software\\Classes\\") + e + L"\\OpenWithProgids").c_str(),
                       L"tinyoffice.Cue.video");
  // the subtitle fonts Cue installed for this user
  std::wstring fonts = known_folder(FOLDERID_LocalAppData) + L"\\Microsoft\\Windows\\Fonts";
  const wchar_t* files_f[3] = {L"CueOmni.ttf", L"CueClean.ttf", L"CueReadable.ttf"};
  const wchar_t* names_f[3] = {L"Cue Omni (TrueType)", L"Cue Clean (TrueType)", L"Cue Readable (TrueType)"};
  for (int i = 0; i < 3; ++i) {
    std::wstring f = fonts + L"\\" + files_f[i];
    RemoveFontResourceExW(f.c_str(), 0, nullptr);
    RegDeleteKeyValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", names_f[i]);
    DeleteFileW(f.c_str());
  }
  SendNotifyMessageW(HWND_BROADCAST, WM_FONTCHANGE, 0, 0);
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
  job.progress = 0.9;
  if (!job.keep) {
    job.say(tr(str::setup_deleting_prefs));
    std::error_code ec;
    fs::remove_all(fs::path(known_folder(FOLDERID_RoamingAppData)) / L"tinyoffice" / L"Cue", ec);
  }
  job.progress = 1.0;
  job.say(tr(str::setup_done));
  job.result = 1;
  if (SUCCEEDED(com)) CoUninitialize();
}

DWORD WINAPI worker_main(void* p) {
  Job* job = static_cast<Job*>(p);
  if (job->mode == SetupMode::Uninstall) uninstall(*job);
  else install(*job);
  return 0;
}

// ── the window ──────────────────────────────────────────────────────
struct Ui {
  HWND hwnd = nullptr;
  SetupView view;
  Canvas canvas;
  Job job;
  HANDLE worker = nullptr;
  bool working = false, elevated_retry = false;
  double s = 2;
  UINT dpi = 96;
  LARGE_INTEGER qpf{}, qp0{};
  double last = 0, done_at = -1;
  std::vector<std::wstring> relaunch_args;
  double now() const {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return double(t.QuadPart - qp0.QuadPart) / double(qpf.QuadPart);
  }
};
Ui* g = nullptr;

double scale_for(UINT dpi) {
  double s = std::floor(dpi * 2.0 / 96.0 + 0.25);
  return std::clamp(s, 2.0, 8.0);
}

void start_work() {
  g->view.desktop = g->job.desktop = g->view.desktop;
  g->job.launch = g->view.launch;
  g->job.keep = g->view.keep;
  g->job.result = 0;
  g->job.progress = 0;
  g->view.progress = 0;
  g->view.set_stage(SetupStage::Working, g->now());
  g->working = true;
  g->worker = CreateThread(nullptr, 0, worker_main, &g->job, 0, nullptr);
}

void relaunch_elevated() {
  std::wstring args;
  for (auto& a : g->relaunch_args) args += L"\"" + a + L"\" ";
  args += L"--dir \"" + g->job.dir + L"\" --auto --elevated";
  if (!g->job.desktop) args += L" --no-desktop";
  if (!g->job.launch) args += L" --no-launch";
  SHELLEXECUTEINFOW sei{sizeof(sei)};
  sei.lpVerb = L"runas";
  sei.lpFile = g->job.self.c_str();
  sei.lpParameters = args.c_str();
  sei.nShow = SW_SHOWNORMAL;
  sei.hwnd = g->hwnd;
  if (ShellExecuteExW(&sei)) { DestroyWindow(g->hwnd); return; }
  g->job.fail(tr(str::setup_needs_admin));
  g->view.error = g->job.error;
  g->view.set_stage(SetupStage::Failed, g->now());
}

void pick_folder() {
  IFileOpenDialog* dlg = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return;
  DWORD opts = 0;
  dlg->GetOptions(&opts);
  dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
  dlg->SetTitle(widen(tr(str::setup_choose_folder)).c_str());
  if (SUCCEEDED(dlg->Show(g->hwnd))) {
    IShellItem* item = nullptr;
    if (SUCCEEDED(dlg->GetResult(&item))) {
      wchar_t* path = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        std::wstring d = path;
        while (!d.empty() && (d.back() == L'\\' || d.back() == L'/')) d.pop_back();
        std::wstring leaf = fs::path(d).filename().wstring();
        CharLowerW(leaf.data());
        if (leaf != L"cue") d += L"\\Cue";  // a folder of its own inside the chosen one
        g->job.dir = d;
        g->view.dir = narrow(d);
      }
      CoTaskMemFree(path);
      item->Release();
    }
  }
  dlg->Release();
}

void act(SetupAct a) {
  switch (a) {
    case SetupAct::None: break;
    case SetupAct::Min: ShowWindow(g->hwnd, SW_MINIMIZE); break;
    case SetupAct::Close:
      if (!g->working) DestroyWindow(g->hwnd);
      break;
    case SetupAct::ToggleDesktop: g->view.desktop = !g->view.desktop; break;
    case SetupAct::ToggleLaunch: g->view.launch = !g->view.launch; break;
    case SetupAct::ToggleKeep: g->view.keep = !g->view.keep; break;
    case SetupAct::ChangeDir: pick_folder(); break;
    case SetupAct::Secondary: if (!g->working) DestroyWindow(g->hwnd); break;
    case SetupAct::Primary:
      if (g->view.stage == SetupStage::Ask || g->view.stage == SetupStage::Failed) start_work();
      else if (g->view.stage == SetupStage::Done) {
        if (g->job.mode != SetupMode::Uninstall) open_cue(g->job.dir);
        DestroyWindow(g->hwnd);
      }
      break;
  }
}

void paint_now() {
  double t = g->now();
  g->view.render(g->canvas, t);
  InvalidateRect(g->hwnd, nullptr, FALSE);
}

void on_tick() {
  double t = g->now(), dt = std::clamp(t - g->last, 0.0, 0.05);
  g->last = t;
  if (g->working) {
    g->view.progress = g->job.progress.load();
    { std::lock_guard<std::mutex> l(g->job.mu); g->view.status = g->job.status; }
    int r = g->job.result.load();
    if (r != 0) {
      WaitForSingleObject(g->worker, 2000);
      CloseHandle(g->worker);
      g->worker = nullptr;
      g->working = false;
      if (r == 1) {
        g->view.set_stage(SetupStage::Done, t);
        g->done_at = t;
        if (g->job.mode != SetupMode::Uninstall && (g->job.launch || g->job.mode == SetupMode::Update)) open_cue(g->job.dir);
      } else if (r == 3) {
        relaunch_elevated();
        return;
      } else {
        { std::lock_guard<std::mutex> l(g->job.mu); g->view.error = g->job.error; }
        g->view.set_stage(SetupStage::Failed, t);
      }
    }
  }
  // after an update (or an install that opens Cue), the window leaves on its own
  if (g->done_at > 0 && g->job.mode != SetupMode::Uninstall && (g->job.launch || g->job.mode == SetupMode::Update) &&
      t - g->done_at > (g->job.mode == SetupMode::Update ? 1.1 : 1.8)) {
    DestroyWindow(g->hwnd);
    return;
  }
  g->view.tick(t, dt);
  paint_now();
}

std::string key_name(WPARAM vk) {
  switch (vk) {
    case VK_TAB: return "tab";
    case VK_RETURN: return "enter";
    case VK_SPACE: return "space";
    case VK_ESCAPE: return "esc";
    default: return "";
  }
}

void apply_frame(HWND h) {
  BOOL dark = TRUE;
  DwmSetWindowAttribute(h, 20, &dark, sizeof dark);
  DWORD corner = 2;  // round
  DwmSetWindowAttribute(h, 33, &corner, sizeof corner);
  COLORREF edge = RGB(35, 35, 35), cap = RGB(3, 3, 3);
  DwmSetWindowAttribute(h, 34, &edge, sizeof edge);
  DwmSetWindowAttribute(h, 35, &cap, sizeof cap);
  MARGINS m{1, 1, 1, 1};
  DwmExtendFrameIntoClientArea(h, &m);
}

LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_NCCALCSIZE:
      if (w) {
        auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(l);
        InflateRect(&p->rgrc[0], -1, -1);  // one native pixel of border, no title bar
        return 0;
      }
      break;
    case WM_NCHITTEST: {
      POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
      ScreenToClient(h, &p);
      return g->view.is_background(p.x, p.y) ? HTCAPTION : HTCLIENT;
    }
    case WM_SIZE: {
      int cw = LOWORD(l), ch = HIWORD(l);
      if (cw > 0 && ch > 0) { g->view.set_size(cw, ch, g->s); paint_now(); }
      return 0;
    }
    case WM_DPICHANGED: {
      g->dpi = HIWORD(w);
      g->s = scale_for(g->dpi);
      RECT* r = reinterpret_cast<RECT*>(l);
      SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(h, &ps);
      BITMAPINFO bi{};
      bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
      bi.bmiHeader.biWidth = g->canvas.w;
      bi.bmiHeader.biHeight = -g->canvas.h;
      bi.bmiHeader.biPlanes = 1;
      bi.bmiHeader.biBitCount = 32;
      bi.bmiHeader.biCompression = BI_RGB;
      if (g->canvas.w > 0)
        SetDIBitsToDevice(dc, 0, 0, g->canvas.w, g->canvas.h, 0, 0, 0, g->canvas.h, g->canvas.px.data(), &bi, DIB_RGB_COLORS);
      EndPaint(h, &ps);
      return 0;
    }
    case WM_TIMER: on_tick(); return 0;
    case WM_MOUSEMOVE: {
      TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0};
      TrackMouseEvent(&t);
      act(g->view.mouse_move(GET_X_LPARAM(l), GET_Y_LPARAM(l), g->now()));
      return 0;
    }
    case WM_NCMOUSEMOVE: {
      TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE | TME_NONCLIENT, h, 0};
      TrackMouseEvent(&t);
      POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
      ScreenToClient(h, &p);
      g->view.mouse_move(p.x, p.y, g->now());
      break;
    }
    case WM_MOUSELEAVE:
    case WM_NCMOUSELEAVE: {
      POINT p;
      GetCursorPos(&p);
      RECT r;
      GetWindowRect(h, &r);
      if (!PtInRect(&r, p)) g->view.mouse_leave(g->now());
      return 0;
    }
    case WM_LBUTTONDOWN: act(g->view.mouse_down(GET_X_LPARAM(l), GET_Y_LPARAM(l), g->now())); return 0;
    case WM_KEYDOWN: {
      std::string k = key_name(w);
      if (!k.empty()) act(g->view.key(k, GetKeyState(VK_SHIFT) < 0, g->now()));
      return 0;
    }
    case WM_CLOSE:
      if (g->working) return 0;  // never half-installed
      DestroyWindow(h);
      return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

int run_silent() {
  if (g->job.mode != SetupMode::Uninstall && !g->job.payload.ok) return 2;
  worker_main(&g->job);
  if (g->job.mode == SetupMode::Update && g->job.result == 1) open_cue(g->job.dir);
  return g->job.result == 1 ? 0 : 1;
}
}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
  using SetCtxFn = BOOL(WINAPI*)(HANDLE);
  if (auto fn = reinterpret_cast<SetCtxFn>(reinterpret_cast<void*>(
          GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"))))
    fn((HANDLE)-4);
  SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
  static Ui ui;
  g = &ui;
  QueryPerformanceFrequency(&ui.qpf);
  QueryPerformanceCounter(&ui.qp0);
  HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  bool want_uninstall = false, want_update = false, silent = false, auto_start = false, temp_copy = false;
  std::wstring dir_arg;
  for (int i = 1; i < argc; ++i) {
    std::wstring a = argv[i];
    if (a == L"--uninstall") want_uninstall = true;
    else if (a == L"--update") want_update = true;
    else if (a == L"--silent") silent = true;
    else if (a == L"--auto") auto_start = true;
    else if (a == L"--temp-copy") temp_copy = true;
    else if (a == L"--no-desktop") ui.job.desktop = false;
    else if (a == L"--no-launch") ui.job.launch = false;
    else if (a == L"--wait" && i + 1 < argc) ui.job.wait_pid = (DWORD)_wtoi(argv[++i]);
    else if (a == L"--dir" && i + 1 < argc) dir_arg = argv[++i];
    if (a == L"--update" || a == L"--uninstall" || a == L"--silent") ui.relaunch_args.push_back(a);
    if (a == L"--wait" && i < argc) { ui.relaunch_args.push_back(a); ui.relaunch_args.push_back(argv[i]); }
  }
  LocalFree(argv);

  // the installer speaks the language Cue would: a choice saved in Cue's preferences (updates,
  // reinstalls, the uninstaller), otherwise the first supported Windows display language
  apply_ui_language(saved_language_pref(known_folder(FOLDERID_RoamingAppData) + L"\\tinyoffice\\Cue\\state.json"),
                    windows_ui_languages());
  ui.job.self = self_path();
  ui.job.payload = read_footer(ui.job.self);
  if (!ui.job.payload.ok) want_uninstall = true;  // the plain uninstall.exe, double-clicked

  std::wstring registered = reg_read(HKEY_CURRENT_USER, kUninstallKey, L"InstallLocation");
  std::string old_version = narrow(reg_read(HKEY_CURRENT_USER, kUninstallKey, L"DisplayVersion"));
  // C:\Cue by default: at the root of the system drive, easy to find. Windows lets every user
  // create a folder there and own it, so installing and updating still need no administrator.
  wchar_t drive[MAX_PATH] = L"C:";
  if (!GetEnvironmentVariableW(L"SystemDrive", drive, MAX_PATH)) wcscpy(drive, L"C:");
  std::wstring default_dir = std::wstring(drive) + L"\\Cue";
  ui.job.dir = !dir_arg.empty() ? dir_arg : !registered.empty() ? registered : default_dir;
  while (!ui.job.dir.empty() && (ui.job.dir.back() == L'\\' || ui.job.dir.back() == L'/')) ui.job.dir.pop_back();

  if (want_uninstall) {
    ui.job.mode = SetupMode::Uninstall;
    if (dir_arg.empty()) ui.job.dir = fs::path(ui.job.self).parent_path().wstring();
    // the uninstaller cannot delete itself while it runs: it continues from a copy in %TEMP%
    if (!temp_copy) {
      wchar_t tmp[MAX_PATH + 1] = {};
      GetTempPathW(MAX_PATH, tmp);
      std::wstring copy = std::wstring(tmp) + L"Cue-uninstall-" + std::to_wstring(GetCurrentProcessId()) + L".exe";
      if (CopyFileW(ui.job.self.c_str(), copy.c_str(), FALSE)) {
        // the copy waits for this process to end, so it can delete this very file too
        std::wstring cmd = L"\"" + copy + L"\" --uninstall --temp-copy --wait " + std::to_wstring(GetCurrentProcessId()) +
                           L" --dir \"" + ui.job.dir + L"\"" + (silent ? L" --silent" : L"");
        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(copy.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
          AllowSetForegroundWindow(pi.dwProcessId);
          CloseHandle(pi.hThread);
          CloseHandle(pi.hProcess);
          return 0;
        }
      }
    }
  } else if (want_update) {
    ui.job.mode = SetupMode::Update;
    auto_start = true;
    if (!old_version.empty() && compare_versions(old_version, CUE_VERSION) >= 0) old_version.clear();
  } else {
    bool present = GetFileAttributesW((ui.job.dir + L"\\Cue.exe").c_str()) != INVALID_FILE_ATTRIBUTES;
    ui.job.mode = present ? SetupMode::Upgrade : SetupMode::Install;
    // a reinstall (or a test build being replaced by the release): no "old → new" arrow
    if (present && !old_version.empty() && compare_versions(old_version, CUE_VERSION) >= 0) old_version.clear();
  }

  int code = 0;
  if (silent) {
    code = run_silent();
  } else {
    // the window: 312 x 184 px of art, at the monitor's whole scale
    POINT cp;
    GetCursorPos(&cp);
    HMONITOR mon = MonitorFromPoint(cp, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    UINT mdpi = 96, dy = 96;
    using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
    if (HMODULE sh = LoadLibraryW(L"shcore.dll"))
      if (auto fn = reinterpret_cast<GetDpiForMonitorFn>(reinterpret_cast<void*>(GetProcAddress(sh, "GetDpiForMonitor"))))
        fn(mon, 0, &mdpi, &dy);
    ui.dpi = mdpi;
    ui.s = scale_for(mdpi);
    int cw = (int)(SetupView::art_w() * ui.s), ch = (int)(SetupView::kH * ui.s);
    int ww = cw + 2, wh = ch + 2;
    int x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - ww) / 2;
    int y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - wh) / 2;

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wc.lpszClassName = L"tinyoffice.Cue.Setup";
    RegisterClassExW(&wc);

    ui.view.mode = ui.job.mode;
    ui.view.version = CUE_VERSION_SHORT;
    ui.view.old_version = short_version(old_version);
    ui.view.dir = narrow(ui.job.dir);
    ui.view.desktop = ui.job.desktop;
    ui.view.launch = ui.job.launch;
    ui.view.keep = true;
    ui.view.set_size(cw, ch, ui.s);
    reduce_motion() = [] { BOOL anim = TRUE; SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &anim, 0); return !anim; }();

    const std::wstring title = widen(tr(ui.job.mode == SetupMode::Uninstall ? str::setup_title_uninstall : str::setup_title_install));
    ui.hwnd = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, title.c_str(), WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                              x, y, ww, wh, nullptr, nullptr, inst, nullptr);
    if (!ui.hwnd) return 1;
    apply_frame(ui.hwnd);
    SetWindowPos(ui.hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    {
      // whatever frame the system kept, the client area is exactly the design's size
      RECT rc, wr;
      GetClientRect(ui.hwnd, &rc);
      GetWindowRect(ui.hwnd, &wr);
      int dw = cw - rc.right, dh = ch - rc.bottom;
      if (dw || dh)
        SetWindowPos(ui.hwnd, nullptr, 0, 0, wr.right - wr.left + dw, wr.bottom - wr.top + dh, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
      GetClientRect(ui.hwnd, &rc);
      ui.view.set_size(rc.right, rc.bottom, ui.s);
    }
    ui.view.start(ui.now());
    ui.last = ui.now();
    paint_now();
    ShowWindow(ui.hwnd, show);
    SetForegroundWindow(ui.hwnd);
    SetTimer(ui.hwnd, 1, 15, nullptr);
    if (auto_start) start_work();
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    code = ui.job.result == 1 ? 0 : 1;
  }
  // a temporary uninstaller copy removes itself once it has closed
  if (temp_copy) {
    std::wstring cmd = L"cmd.exe /c ping 127.0.0.1 -n 3 > nul & del /f /q \"" + ui.job.self + L"\"";
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
    }
  }
  if (SUCCEEDED(com)) CoUninitialize();
  return code;
}
