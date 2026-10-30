#include "platform.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <cmath>

#include "../core/i18n.h"

std::wstring widen(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
  return w;
}

std::string narrow(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
  return s;
}

std::wstring exe_dir() {
  wchar_t buf[MAX_PATH * 4];
  DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof buf / sizeof buf[0]));
  std::wstring p(buf, n);
  size_t k = p.find_last_of(L"\\/");
  return k == std::wstring::npos ? L"." : p.substr(0, k);
}

std::wstring data_dir() {
  PWSTR base = nullptr;
  std::wstring dir;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &base))) {
    dir = base;
    CoTaskMemFree(base);
  } else {
    dir = exe_dir();
  }
  dir += L"\\tinyoffice";
  CreateDirectoryW(dir.c_str(), nullptr);
  std::wstring old = dir + L"\\Player";
  dir += L"\\Cue";
  // the app used to be called Player: its folder (settings, Recentes, positions) comes along once
  if (GetFileAttributesW(dir.c_str()) == INVALID_FILE_ATTRIBUTES && GetFileAttributesW(old.c_str()) != INVALID_FILE_ATTRIBUTES)
    if (!MoveFileExW(old.c_str(), dir.c_str(), 0) && CreateDirectoryW(dir.c_str(), nullptr))
      for (const wchar_t* f : {L"\\state.json", L"\\state.json.bak"})  // something still holds it open: copy what matters
        CopyFileW((old + f).c_str(), (dir + f).c_str(), TRUE);
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir;
}

std::vector<std::string> open_file_dialog(HWND owner, DialogKind kind) {
  std::vector<std::string> out;
  IFileOpenDialog* dlg = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return out;
  // the dialog itself (buttons, places, columns) is Windows' own, in Windows' language;
  // only the title and the type names below are Cue's
  const std::wstring media = widen(tr(str::win_filter_media)), audio = widen(tr(str::win_filter_audio)),
                     all = widen(tr(str::win_filter_all)), subtitles = widen(tr(str::win_filter_subtitles));
  COMDLG_FILTERSPEC video[] = {
      {media.c_str(), L"*.mkv;*.mp4;*.m4v;*.mov;*.avi;*.webm;*.wmv;*.flv;*.ts;*.m2ts;*.mts;*.mpg;*.mpeg;*.ogv;*.3gp;*.vob;*.ogg;*.oga;*.opus;*.mp3;*.m4a;*.aac;*.wav;*.flac;*.aif;*.aiff;*.wma;*.mka"},
      {audio.c_str(), L"*.mp4;*.ogg;*.oga;*.opus;*.mp3;*.m4a;*.aac;*.wav;*.flac;*.aif;*.aiff;*.wma;*.mka"},
      {all.c_str(), L"*.*"}};
  COMDLG_FILTERSPEC subs[] = {{subtitles.c_str(), L"*.srt;*.vtt;*.ass;*.ssa;*.sub;*.sup;*.idx"}, {all.c_str(), L"*.*"}};
  if (kind == DialogKind::Video) dlg->SetFileTypes(3, video);
  else dlg->SetFileTypes(2, subs);
  dlg->SetTitle(widen(tr(kind == DialogKind::Video ? str::win_open_title : str::win_subtitle_title)).c_str());
  if (SUCCEEDED(dlg->Show(owner))) {
    IShellItem* item = nullptr;
    if (SUCCEEDED(dlg->GetResult(&item))) {
      PWSTR path = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        out.push_back(narrow(path));
        CoTaskMemFree(path);
      }
      item->Release();
    }
  }
  dlg->Release();
  return out;
}

std::string pick_folder_dialog(HWND owner) {
  std::string out;
  IFileOpenDialog* dlg = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return out;
  DWORD opts = 0;
  dlg->GetOptions(&opts);
  dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
  dlg->SetTitle(widen(tr(str::remote_add_folder)).c_str());
  if (SUCCEEDED(dlg->Show(owner))) {
    IShellItem* item = nullptr;
    if (SUCCEEDED(dlg->GetResult(&item))) {
      PWSTR path = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        out = narrow(path);
        CoTaskMemFree(path);
      }
      item->Release();
    }
  }
  dlg->Release();
  return out;
}

std::string folder_display_name(const std::string& path) {
  std::string out;
  IShellItem* item = nullptr;
  if (SUCCEEDED(SHCreateItemFromParsingName(widen(path).c_str(), nullptr, IID_PPV_ARGS(&item)))) {
    PWSTR name = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &name))) {
      out = narrow(name);
      CoTaskMemFree(name);
    }
    item->Release();
  }
  if (out.empty()) {
    size_t k = path.find_last_of("\\/", path.size() > 1 ? path.size() - 2 : 0);
    out = k == std::string::npos ? path : path.substr(k + 1);
    while (!out.empty() && (out.back() == '\\' || out.back() == '/')) out.pop_back();
    if (out.empty()) out = path;
  }
  return out;
}

std::vector<std::string> default_media_folders() {
  std::vector<std::string> out;
  for (const KNOWNFOLDERID* id : {&FOLDERID_Videos, &FOLDERID_Downloads, &FOLDERID_Desktop}) {
    PWSTR p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(*id, KF_FLAG_DEFAULT, nullptr, &p)) && p) out.push_back(narrow(p));
    else out.push_back("");
    if (p) CoTaskMemFree(p);
  }
  return out;
}

void show_in_folder(const std::wstring& file) {
  // talking to Explorer can take a moment: on its own thread, so the video never waits
  auto* path = new std::wstring(file);
  HANDLE th = CreateThread(nullptr, 0, [](void* p) -> DWORD {
    auto* f = static_cast<std::wstring*>(p);
    bool init = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    bool ok = false;
    if (PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(f->c_str())) {
      ok = SUCCEEDED(SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0));
      ILFree(pidl);
    }
    if (!ok) {  // the file is gone: at least its folder
      std::wstring dir = f->substr(0, f->find_last_of(L"\\/"));
      ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    if (init) CoUninitialize();
    delete f;
    return 0;
  }, path, 0, nullptr);
  if (th) CloseHandle(th); else delete path;
}

std::vector<std::string> files_from_hdrop(HDROP drop) {
  std::vector<std::string> out;
  UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
  for (UINT i = 0; i < n; ++i) {
    UINT len = DragQueryFileW(drop, i, nullptr, 0);
    std::wstring w(len + 1, L'\0');
    DragQueryFileW(drop, i, w.data(), len + 1);
    w.resize(len);
    out.push_back(narrow(w));
  }
  return out;
}

std::vector<std::string> clipboard_files(HWND owner) {
  std::vector<std::string> out;
  if (!OpenClipboard(owner)) return out;
  if (HANDLE h = GetClipboardData(CF_HDROP)) {
    out = files_from_hdrop((HDROP)h);
  } else if (HANDLE t = GetClipboardData(CF_UNICODETEXT)) {
    // a path copied as text also works, if it points to a real file
    if (auto* p = (const wchar_t*)GlobalLock(t)) {
      std::wstring w = p;
      GlobalUnlock(t);
      while (!w.empty() && (w.back() == L'\n' || w.back() == L'\r' || w.back() == L' ')) w.pop_back();
      if (w.size() > 2 && w.front() == L'"' && w.back() == L'"') w = w.substr(1, w.size() - 2);
      DWORD attr = GetFileAttributesW(w.c_str());
      if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) out.push_back(narrow(w));
    }
  }
  CloseClipboard();
  return out;
}

static bool reg_set(HKEY root, const std::wstring& key, const wchar_t* name, const std::wstring& val) {
  HKEY k;
  if (RegCreateKeyExW(root, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
  LONG r = RegSetValueExW(k, name, 0, REG_SZ, (const BYTE*)val.c_str(), (DWORD)((val.size() + 1) * sizeof(wchar_t)));
  RegCloseKey(k);
  return r == ERROR_SUCCESS;
}

bool register_open_with(std::string* err) {
  wchar_t exe[MAX_PATH * 4];
  GetModuleFileNameW(nullptr, exe, (DWORD)(sizeof exe / sizeof exe[0]));
  std::wstring cmd = L"\"" + std::wstring(exe) + L"\" \"%1\"";
  const wchar_t* prog = L"Software\\Classes\\tinyoffice.Cue.video";
  // the file type's name in Explorer, in the interface language of the last registration
  bool ok = reg_set(HKEY_CURRENT_USER, prog, nullptr, widen(tr(str::win_progid))) &&
            reg_set(HKEY_CURRENT_USER, std::wstring(prog) + L"\\Application", L"ApplicationName", L"Cue") &&
            reg_set(HKEY_CURRENT_USER, std::wstring(prog) + L"\\DefaultIcon", nullptr, std::wstring(exe) + L",0") &&
            reg_set(HKEY_CURRENT_USER, std::wstring(prog) + L"\\shell\\open\\command", nullptr, cmd);
  std::wstring app = L"Software\\Classes\\Applications\\Cue.exe";
  ok = ok && reg_set(HKEY_CURRENT_USER, app + L"\\shell\\open\\command", nullptr, cmd) &&
       reg_set(HKEY_CURRENT_USER, app, L"FriendlyAppName", L"Cue");
  // Keep an existing legacy ProgID working if Windows still uses it as a default.
  // Hide its duplicate recommendation and change only our own Player registration.
  const std::wstring legacy = L"Software\\Classes\\tinyoffice.Player.video";
  HKEY old;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, legacy.c_str(), 0, KEY_READ, &old) == ERROR_SUCCESS) {
    RegCloseKey(old);
    wchar_t old_command[MAX_PATH * 4]{};
    DWORD bytes = sizeof old_command;
    RegGetValueW(HKEY_CURRENT_USER, (legacy + L"\\shell\\open\\command").c_str(), nullptr,
                 RRF_RT_REG_SZ, nullptr, old_command, &bytes);
    wchar_t player_command[MAX_PATH * 4]{};
    bytes = sizeof player_command;
    const std::wstring player = L"Software\\Classes\\Applications\\Player.exe";
    if (old_command[0] && RegGetValueW(HKEY_CURRENT_USER, (player + L"\\shell\\open\\command").c_str(), nullptr,
                                       RRF_RT_REG_SZ, nullptr, player_command, &bytes) == ERROR_SUCCESS &&
        std::wstring(old_command) == player_command) {
      reg_set(HKEY_CURRENT_USER, player, L"FriendlyAppName", L"Cue");
      reg_set(HKEY_CURRENT_USER, player, L"NoOpenWith", L"");
      reg_set(HKEY_CURRENT_USER, player + L"\\shell\\open\\command", nullptr, cmd);
    }
    reg_set(HKEY_CURRENT_USER, legacy + L"\\Application", L"ApplicationName", L"Cue");
    reg_set(HKEY_CURRENT_USER, legacy, L"NoOpenWith", L"");
    reg_set(HKEY_CURRENT_USER, legacy + L"\\DefaultIcon", nullptr, std::wstring(exe) + L",0");
    reg_set(HKEY_CURRENT_USER, legacy + L"\\shell\\open\\command", nullptr, cmd);
  }
  const wchar_t* exts[] = {L".mkv", L".mp4", L".m4v", L".mov", L".avi", L".webm", L".wmv", L".ts", L".m2ts", L".mpg", L".mpeg", L".ogv",
                         L".ogg", L".oga", L".opus", L".mp3", L".m4a", L".aac", L".wav", L".flac", L".aif", L".aiff", L".wma", L".mka"};
  const std::wstring empty_value;
  for (auto e : exts) {
    std::wstring owp = std::wstring(L"Software\\Classes\\") + e + L"\\OpenWithProgids";
    RegDeleteKeyValueW(HKEY_CURRENT_USER, owp.c_str(), L"tinyoffice.Player.video");
    ok = ok && reg_set(HKEY_CURRENT_USER, owp, L"tinyoffice.Cue.video", empty_value);
    ok = ok && reg_set(HKEY_CURRENT_USER, app + L"\\SupportedTypes", e, empty_value);
  }
  if (ok) ok = reg_set(HKEY_CURRENT_USER, app, L"CueMediaTypes", L"video-audio-1");
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
  if (!ok && err) *err = "register failed";  // log only
  return ok;
}

void refresh_open_with() {
  // Portable updates can leave Explorer's old Player registration behind.
  // Only the main process calls this; thumbnail helpers must never register.
  const wchar_t* app = L"Software\\Classes\\Applications\\Cue.exe";
  const wchar_t* prog = L"Software\\Classes\\tinyoffice.Cue.video";
  auto read = [](const std::wstring& key, const wchar_t* name) {
    wchar_t value[MAX_PATH * 4]{};
    DWORD bytes = sizeof value;
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), name, RRF_RT_REG_SZ, nullptr, value, &bytes) != ERROR_SUCCESS)
      return std::wstring{};
    return std::wstring(value);
  };
  auto exists = [](const wchar_t* key) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
    RegCloseKey(k);
    return true;
  };
  wchar_t exe[MAX_PATH * 4]{};
  DWORD len = GetModuleFileNameW(nullptr, exe, (DWORD)(sizeof exe / sizeof exe[0]));
  if (!len || len >= sizeof exe / sizeof exe[0]) return;
  const std::wstring command = L"\"" + std::wstring(exe) + L"\" \"%1\"";
  if (read(app, L"FriendlyAppName") == L"Cue" &&
      read(prog, nullptr) == widen(tr(str::win_progid)) &&
      read(app, L"CueMediaTypes") == L"video-audio-1" &&
      read(std::wstring(app) + L"\\shell\\open\\command", nullptr) == command &&
      read(std::wstring(prog) + L"\\Application", L"ApplicationName") == L"Cue" &&
      read(std::wstring(prog) + L"\\shell\\open\\command", nullptr) == command &&
      (!exists(L"Software\\Classes\\tinyoffice.Player.video") ||
       (read(L"Software\\Classes\\tinyoffice.Player.video\\Application", L"ApplicationName") == L"Cue" &&
        read(L"Software\\Classes\\tinyoffice.Player.video\\shell\\open\\command", nullptr) == command))) return;
  register_open_with(nullptr);
}


// Pixel art is only crisp at whole multiples (every art pixel the same number of screen
// pixels), so the scale is always an integer and never interpolated:
//  * 100/150/200/250/300%: the exact multiple (2/3/4/5/6 px per art px)
//  * 125/175/225%: halfway between two multiples. The larger one when the whole design
//    canvas still fits with it (maximized, fullscreen, big windows), the smaller otherwise
//  * very large windows at low scaling (a TV, 4K at 100%) step up, read from a distance
//  * never so large that the layout (320 x 180 art px) stops fitting
// A small bias toward the current scale keeps it from flickering while resizing.

bool system_reduce_motion() {
  BOOL anim = TRUE;
  SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &anim, 0);
  return !anim;
}

bool system_high_contrast() {
  HIGHCONTRASTW hc{sizeof(hc)};
  if (!SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0)) return false;
  return (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

// ── drop target ─────────────────────────────────────────────────────
HRESULT DropTarget::QueryInterface(REFIID riid, void** ppv) {
  if (riid == IID_IUnknown || riid == IID_IDropTarget) { *ppv = static_cast<IDropTarget*>(this); AddRef(); return S_OK; }
  *ppv = nullptr;
  return E_NOINTERFACE;
}
ULONG DropTarget::Release() {
  LONG r = InterlockedDecrement(&ref_);
  if (r == 0) delete this;
  return r;
}
POINT DropTarget::to_client(POINTL pt) const {
  POINT p{pt.x, pt.y};
  ScreenToClient(hwnd_, &p);
  return p;
}
HRESULT DropTarget::DragEnter(IDataObject* data, DWORD, POINTL pt, DWORD* effect) {
  FORMATETC fe{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
  ok_ = data && data->QueryGetData(&fe) == S_OK;
  *effect = ok_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
  if (on_enter) on_enter(ok_, to_client(pt));
  return S_OK;
}
HRESULT DropTarget::DragOver(DWORD, POINTL pt, DWORD* effect) {
  *effect = ok_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
  if (ok_ && on_over) on_over(to_client(pt));
  return S_OK;
}
HRESULT DropTarget::DragLeave() {
  if (on_leave) on_leave();
  return S_OK;
}
HRESULT DropTarget::Drop(IDataObject* data, DWORD, POINTL, DWORD* effect) {
  *effect = DROPEFFECT_NONE;
  if (on_leave) on_leave();
  FORMATETC fe{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
  STGMEDIUM st{};
  if (data && SUCCEEDED(data->GetData(&fe, &st))) {
    auto files = files_from_hdrop((HDROP)st.hGlobal);
    ReleaseStgMedium(&st);
    *effect = DROPEFFECT_COPY;
    if (on_drop) on_drop(files);
  }
  return S_OK;
}
