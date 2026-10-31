#include "app.h"
#include "../core/file_io.h"
#include "single_instance.h"

#include <dwmapi.h>
#include <wincodec.h>
#include <shlobj.h>
#include <shellapi.h>
#include <windowsx.h>
#include <timeapi.h>
#include <mmsystem.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <set>
#include <climits>
#include <cmath>
#include <ctime>
#include <fstream>
#include <mutex>

#include "../core/sub_style.h"
#include "../core/util.h"
#include "../core/window_policy.h"
#include "../core/resize_hit.h"
#include "../core/blur_geometry.h"
#include "../core/video_window.h"
#include "../core/viewport_gate.h"
#include "../core/fullscreen_geometry.h"
#include "blur_shader.h"
#include "../core/audio_mode.h"
#include "../core/preview_size.h"
#include "licenses.h"
#include "ui_language.h"
#include "../core/i18n.h"
#include "../core/version.h"

namespace {
std::string subtitle_position_string(double pos) {
  char value[32]; std::snprintf(value, sizeof value, "%.3f", pos); return value;
}
void frame_saved_sound() {
  static const std::vector<char> wav = [] {
    constexpr int rate = 22050, count = 1764;
    std::vector<char> data(44 + count * 2, 0);
    auto put16=[&](int offset, unsigned value){data[offset]=(char)value;data[offset+1]=(char)(value>>8);};
    auto put32=[&](int offset, unsigned value){for(int i=0;i<4;++i)data[offset+i]=(char)(value>>(8*i));};
    std::memcpy(data.data(), "RIFF", 4); put32(4, data.size()-8); std::memcpy(data.data()+8,"WAVEfmt ",8);
    put32(16,16);put16(20,1);put16(22,1);put32(24,rate);put32(28,rate*2);put16(32,2);put16(34,16);
    std::memcpy(data.data()+36,"data",4);put32(40,count*2);
    for(int i=0;i<count;++i){double t=(double)i/rate;double envelope=std::sin(3.14159265*i/count)*std::exp(-45*t);
      short sample=(short)(900*envelope*std::sin(2*3.14159265*880*t));put16(44+i*2,(unsigned short)sample);}
    return data;
  }();
  PlaySoundA(wav.data(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT | SND_NOSTOP);
}
enum Obs : uint64_t {
  O_PAUSE = 1, O_TIME, O_DUR, O_VOL, O_MUTE, O_TRACKS, O_SID, O_AID, O_SUBDELAY, O_SUBVIS, O_IDLE, O_PATH, O_SUBTEXT, O_SPEED,
  O_VIDEO_PARAMS, O_HWDEC, O_EOF, O_OSDW, O_OSDH
};
constexpr uint64_t kReplySeek = 100, kReplyShot = 101, kReplyOverlay = 1000, kReplyOverlayRemove = 2000;  // +layer
constexpr uint64_t kHookLoad = 200, kHookPreloaded = 201, kHookUnload = 202;

// Every key the player reacts to is routed back to us; nothing of mpv's own bindings remains.
const char* kBindings[][2] = {
    {"SPACE", "space"},          {"LEFT", "left"},       {"RIGHT", "right"},           {"UP", "up"},
    {"DOWN", "down"},            {"Shift+LEFT", "s_left"}, {"Shift+RIGHT", "s_right"}, {"ENTER", "enter"},
    {"KP_ENTER", "enter"},       {"ESC", "esc"},         {"TAB", "tab"},               {"Shift+TAB", "s_tab"},
    {"m", "m"},                  {"M", "m"},             {"f", "f"},                   {"F", "f"},
    {"s", "s"},                  {"S", "s"},             {"z", "z"},                   {"Z", "z"},
    {"x", "x"},                  {"X", "x"},             {"Ctrl+o", "c_o"},            {"Ctrl+O", "c_o"},
    {"Ctrl+v", "c_v"},           {"Ctrl+V", "c_v"},      {"MBTN_LEFT", "mb_left"},     {"MBTN_RIGHT", "mb_right"},
    {"WHEEL_UP", "wheel_up"},    {"WHEEL_DOWN", "wheel_down"}, {"MBTN_LEFT_DBL", "ignore"}, {"PLAYPAUSE", "space"},
    {"PLAY", "space"},           {"PAUSE", "space"},     {"MUTE", "ignore"},                {"VOLUME_UP", "ignore"},
    {"Ctrl+=", "c_plus"},        {"Ctrl++", "c_plus"},   {"Ctrl+-", "c_minus"},        {"Ctrl+0", "c_zero"},
    {"Ctrl+KP_ADD", "c_plus"},   {"Ctrl+KP_SUBTRACT", "c_minus"}, {"Ctrl+KP0", "c_zero"},
    {"VOLUME_DOWN", "ignore"},     {"[", "lbracket"},      {"]", "rbracket"},            {"\\", "backslash"},
};

COLORREF cref(Rgb c) { return RGB(c.r, c.g, c.b); }

// The engine reports its failure as a catalog key (it runs before the person could change the
// language); the words are looked up when shown, so they follow a language change.
std::string engine_text(const std::string& err) {
  for (str::Id id : {str::engine_missing_dll, str::engine_too_old, str::engine_failed})
    if (err == i18n::key_name(id)) return tr(id);
  return err.empty() ? tr(str::engine_failed) : err;
}
}  // namespace

static HWND g_app_hwnd = nullptr;  // where the engine's window forwards its input

double App::now() const {
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  return double(t.QuadPart - qp0_.QuadPart) / double(qpf_.QuadPart);
}

// Called from the UI thread and the engine thread: one file, opened once, a line at a time.
void App::log(const std::string& msg) {
  static std::mutex mu;
  static std::ofstream f;
  std::lock_guard<std::mutex> lock(mu);
  if (!f.is_open()) f.open(data_dir_ / "cue.log", std::ios::app);
  std::time_t t = std::time(nullptr);
  char b[32];
  std::tm tmv{};
  localtime_s(&tmv, &t);
  std::strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S", &tmv);
  f << b << "  +" << (long long)(now() * 1000) << "ms  " << msg << "\n";
  f.flush();
}

// ── startup ─────────────────────────────────────────────────────────
bool App::init(HINSTANCE inst, const std::vector<std::string>& args, int show) {
  inst_ = inst;
  QueryPerformanceFrequency(&qpf_);
  QueryPerformanceCounter(&qp0_);
  data_dir_ = std::filesystem::path(data_dir());
  for (auto& a : args) if (a == "--log") engine_log_ = true;
  // Prepare the worker now, but map the DLL only after the first paint. DLL
  // initialization holds Windows' loader lock and can stall window creation
  // on the UI thread even when LoadLibrary itself runs on a worker.
  engine_go_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (engine_go_) engine_thread_ = CreateThread(nullptr, 0, engine_main, this, 0, nullptr);
  // Loading the engine is part of opening the file, so it keeps normal priority.
  state_path_ = data_dir_ / "state.json";
  store_.load(state_path_);  // early: the window opens at its size, with the person's settings
  apply_language(false);     // the saved choice, or Windows' display languages (read once)
  play_.in.controls_free = store_.controls_free;
  play_.in.loop = store_.loop;
  play_.in.ui_scale = store_.ui_scale;
  play_.in.ambient = store_.ambient;
  play_.in.ambient_strength = store_.ambient_strength;
  play_.set_double_click_time(GetDoubleClickTime() / 1000.0);
  log("start");
  log("settings " + store_.load_status);
  reduce_motion() = system_reduce_motion();
  set_high_contrast(system_high_contrast());
  for (auto& a : args) if (!a.empty() && a[0] != '-') pending_open_ = a;
  // the welcome comes on a plain first start; opened straight into a file, it waits for next time
  onboarding_ = !store_.onboarded && pending_open_.empty();
  for (auto& a : args) if (a == "--welcome") onboarding_ = pending_open_.empty();
  // what's new, once per version, for people who already knew Cue; a first start gets the
  // welcome instead (it tells the same story), so that version counts as seen
  if (!store_.onboarded) store_.news_seen = store_.news_closed = CUE_VERSION;
  home_.news = compare_versions(CUE_VERSION, store_.news_closed) > 0;  // the line at the bottom, until closed
  news_due_ = store_.onboarded && !onboarding_ && pending_open_.empty() && compare_versions(CUE_VERSION, store_.news_seen) > 0;
  for (auto& a : args) if (a == "--news") news_due_ = !onboarding_ && pending_open_.empty();
  ob_.ambient = store_.ambient;
  ob_.ui_scale = store_.ui_scale;
  // a version found on an earlier run is offered right away; the details come with the next check
  if (!store_.update_available.empty() && compare_versions(store_.update_available, CUE_VERSION) > 0 &&
      store_.update_available != store_.update_skipped) {
    upd_ = Upd::Available;
    upd_rel_.version = store_.update_available;
  }
  log(std::string("version ") + CUE_VERSION);

  WNDCLASSEXW wc{sizeof(wc)};
  wc.lpfnWndProc = wndproc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hIcon = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0));
  wc.hIconSm = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                          GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
  wc.hbrBackground = nullptr;
  wc.lpszClassName = L"tinyoffice.Cue";
  RegisterClassExW(&wc);
  WNDCLASSEXW vc{sizeof(vc)};
  vc.lpfnWndProc = videoproc;
  vc.hInstance = inst;
  vc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  vc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
  vc.lpszClassName = L"tinyoffice.Cue.video";
  RegisterClassExW(&vc);

  // size from the monitor the cursor is on: 480×270 px of art, clamped to the work area
  POINT cp;
  GetCursorPos(&cp);
  HMONITOR mon = MonitorFromPoint(cp, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO mi{sizeof(mi)};
  GetMonitorInfoW(mon, &mi);
  using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
  GetDpiForMonitorFn dpi_fn = nullptr;
  if (HMODULE sh = LoadLibraryW(L"shcore.dll"))
    dpi_fn = reinterpret_cast<GetDpiForMonitorFn>(reinterpret_cast<void*>(GetProcAddress(sh, "GetDpiForMonitor")));
  auto monitor_dpi = [&](HMONITOR m) {
    UINT dx = 96, dy = 96;
    if (dpi_fn) dpi_fn(m, 0, &dx, &dy);
    return dx;
  };
  UINT mdpi = monitor_dpi(mon);
  // first run: a comfortable 16:9 about two thirds of the screen (1280x720 on a 1080p
  // screen at 100%), never smaller than 960x540 DIPs nor taller than the work area allows
  int aw = mi.rcWork.right - mi.rcWork.left, ah = mi.rcWork.bottom - mi.rcWork.top;
  double k = mdpi / 96.0;
  int ww = (int)std::lround(std::clamp(aw * 0.67, 960 * k, 1600 * k));
  int wh = ww * 9 / 16;
  if (ww > aw * 0.9) { ww = (int)(aw * 0.9); wh = ww * 9 / 16; }
  if (wh > ah * 0.84) { wh = (int)(ah * 0.84); ww = wh * 16 / 9; }
  int fw = ww, fh = wh;  // frameless: the client is the window
  int x = mi.rcWork.left + (aw - fw) / 2, y = mi.rcWork.top + (ah - fh) / 2;
  bool start_max = false;
  // afterwards: where it was left, if that place is still on a screen
  if (store_.win.has && restore_saved_window(!pending_open_.empty(), store_.win.w, store_.win.h)) {
    RECT wr{store_.win.x, store_.win.y, store_.win.x + store_.win.w, store_.win.y + store_.win.h};
    if (HMONITOR wm = MonitorFromRect(&wr, MONITOR_DEFAULTTONULL)) {
      MONITORINFO wmi{sizeof(wmi)};
      GetMonitorInfoW(wm, &wmi);
      RECT in;
      if (IntersectRect(&in, &wr, &wmi.rcWork) && in.right - in.left > 160 && in.bottom - in.top > 90) {
        // saved at another scaling? keep the same size on screen
        double rk = monitor_dpi(wm) / (double)store_.win.dpi;
        x = store_.win.x; y = store_.win.y;
        fw = std::min<int>((int)std::lround(store_.win.w * rk), wmi.rcWork.right - wmi.rcWork.left);
        fh = std::min<int>((int)std::lround(store_.win.h * rk), wmi.rcWork.bottom - wmi.rcWork.top);
        start_max = false; // home always opens as a normal horizontal window
      }
    }
  }

  hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"Cue", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, fw, fh,
                          nullptr, nullptr, inst, this);
  if (hwnd_) SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
  if (!hwnd_) { log("could not create window: " + std::to_string(GetLastError())); return false; }
  log("window ok");
  video_ = CreateWindowExW(0, vc.lpszClassName, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0, hwnd_, nullptr, inst, this);
  SetWindowLongPtrW(video_, GWLP_USERDATA, (LONG_PTR)this);
  g_app_hwnd = hwnd_;
  dpi_ = GetDpiForWindow(hwnd_);
  if (pacer_.start(hwnd_, WM_APP_VSYNC)) log("vsync pacer ok");
  refresh_display_rate();
  make_cursors();
  {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    s_ = scale_for(rc.right - rc.left, rc.bottom - rc.top, store_.ui_scale);
  }
  apply_theme_frame();

  drop_ = new DropTarget(hwnd_);
  drop_->on_enter = [this](bool ok, POINT p) {
    if (!ok || mode_ != Mode::Home || onboarding_) return;
    home_.drag_over(p.x, p.y, now());
    set_edge_highlight(true);
    ensure_anim();
  };
  drop_->on_over = [this](POINT p) { if (mode_ == Mode::Home && !onboarding_) { home_.drag_over(p.x, p.y, now()); ensure_anim(); } };
  drop_->on_leave = [this]() { home_.drag_leave(now()); set_edge_highlight(false); ensure_anim(); };
  drop_->on_drop = [this](const std::vector<std::string>& f) {
    // a video dropped on the home gets eaten on its way in
    if (mode_ == Mode::Home) for (auto& x : f) if (!is_subtitle_path(x)) { swallow_next_ = true; break; }
    handle_files(f);
    swallow_next_ = false;
  };
  RegisterDragDrop(hwnd_, drop_);
  scene_.create(inst);
  scene_.on_action = [this](RemoteAction a) { remote_scene_action(a); };
  scene_.on_closed = [this]() {
    remote_pair_deadline_ = 0;
    remote_.set_pairing(false, "");
    gray_ = gray_target_ = 0;
    if (gray_sent_ != 0 && mpv_ok_) { mpv_.set_double_async("saturation", 0); gray_sent_ = 0; }
    ensure_anim();
  };
  scene_.on_tick = [this](double t) {
    scene_gray(t);
    double pair_now = now();  // App deadlines use time since launch; the overlay uses absolute QPC time.
    if (remote_.running() && !scene_.view.closing() && scene_.view.waiting() &&
        remote_pair_deadline_ > 0 && pair_now >= remote_pair_deadline_) {
      remote_code_ = new_pairing_code();
      remote_pair_deadline_ = pair_now + remote::kPairLifetime;
      remote_.set_pairing(!remote_code_.empty(), remote_code_);
      scene_.view.code = remote_code_;
      scene_.view.set_link(remote_code_.empty() ? "" : "http://" + scene_.view.address + "/#c=" + remote_code_);
    }
  };
  scene_.on_leaving = [this]() {
    gray_target_ = 0;  // colour comes back with the exit
    if (scene_paused_ && mode_ == Mode::Play && loaded_) request_pause(false);
    scene_paused_ = false;
  };
  news_host_.create(inst);
  news_host_.on_action = [this](SceneAct a) {
    news_host_.close(news_host_.now());
    if (a.a == (int)NewsAct::Try) open_remote_scene();
  };
  news_host_.on_closed = [this]() { ensure_anim(); };

  // everything the engine needs, copied while nobody else is using it; then it may go on
  engine_cfg_.wid = (int64_t)(intptr_t)video_;
  engine_cfg_.slang = store_.slang;
  engine_cfg_.alang = store_.alang;
  engine_cfg_.volume = store_.volume;
  engine_cfg_.muted = store_.muted;
  engine_cfg_.log = engine_log_;
  refresh_recents(false); // draw saved rows before starting filesystem checks
  show_update_state();
  if (onboarding_) { ob_.set_size(cw_, ch_, s_); ob_.start(now()); log("welcome"); }
  SetTimer(hwnd_, T_POLL, 33, nullptr);
  SetTimer(hwnd_, T_MAINTENANCE, 5000, nullptr);
  ShowWindow(hwnd_, start_max && show != SW_SHOWMINIMIZED && show != SW_MINIMIZE ? SW_SHOWMAXIMIZED : show);
  UpdateWindow(hwnd_);
  log("first paint");
  // Overlap the unchanged home departure with engine/file preparation, rather
  // than adding it after the engine has already become ready.
  if (engine_thread_) SetEvent(engine_go_);
  else on_engine_ready(start_engine());
  if (!pending_open_.empty()) { home_.leave(now(), false); ensure_anim(); }
  refresh_recents();
  // the phone remote, when it was left on: shortly after the start, never in its way
  if (store_.remote_on) SetTimer(hwnd_, T_REMOTE_START, 1500, nullptr);
  if (news_due_) SetTimer(hwnd_, T_NEWS, 450, nullptr);  // after the home has settled in
  return true;
}

int App::run() {
  MSG m;
  while (GetMessageW(&m, nullptr, 0, 0) > 0) {
    if (licenses_message(&m)) continue;
    TranslateMessage(&m);
    DispatchMessageW(&m);
  }
  return (int)m.wParam;
}

void App::apply_theme_frame() {
  apply_window_corners(true);
  BOOL dark = TRUE;
  DwmSetWindowAttribute(hwnd_, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof dark);
  COLORREF cap = cref(P().bg), txt = cref(P().dim), edge = 0xFFFFFFFE;  // DWMWA_COLOR_NONE: no outline
  DwmSetWindowAttribute(hwnd_, 35 /*DWMWA_CAPTION_COLOR*/, &cap, sizeof cap);
  DwmSetWindowAttribute(hwnd_, 36 /*DWMWA_TEXT_COLOR*/, &txt, sizeof txt);
  DwmSetWindowAttribute(hwnd_, 34 /*DWMWA_BORDER_COLOR*/, &edge, sizeof edge);
}

void App::apply_window_corners(bool force) {
  // The custom non-client frame can prevent DWM's automatic rounding.
  // Opt in explicitly; DWM clips the whole window, including the video child.
  // No window region or per-frame mask: retain native shadow and resize/Snap.
  DWORD preference = fullscreen_ || IsZoomed(hwnd_) ? 1 /*DWMWCP_DONOTROUND*/ : 2 /*DWMWCP_ROUND*/;
  if (!force && corner_preference_ == preference) return;
  corner_preference_ = preference; // guard reentrant frame/size messages
  // Symmetric native frame gives DWM one consistent surface to round.
  MARGINS frame = preference == 2 ? MARGINS{1,1,1,1} : MARGINS{0,0,0,0};
  DwmExtendFrameIntoClientArea(hwnd_, &frame);
  DwmSetWindowAttribute(hwnd_, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &preference, sizeof preference);
}

void App::set_edge_highlight(bool on) {
  // Omni: dragging something over the window lights the edge up to `dim`
  COLORREF edge = on ? cref(P().dim) : 0xFFFFFFFE;  // lit while dragging, otherwise no outline
  DwmSetWindowAttribute(hwnd_, 34, &edge, sizeof edge);
}

void App::write_input_conf(const std::wstring& path) {
  std::string desired = "# Generated by Cue. Every binding is handled by the app itself.\n";
  for (auto& b : kBindings) desired += std::string(b[0]) + " script-binding main/" + b[1] + "\n";
  if (file_contents_equal(std::filesystem::path(path),desired)) return;
  std::ofstream f(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
  f << desired;
}

// Registers Cue's subtitle fonts for the current user only (no admin, no system change).
// Windows 10 1809+ lets DirectWrite, and therefore libass, see per-user fonts.
void App::install_user_fonts() {
  wchar_t* local = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) return;
  std::wstring dir = std::wstring(local) + L"\\Microsoft\\Windows\\Fonts";
  CoTaskMemFree(local);
  SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
  const wchar_t* files[3] = {L"CueOmni.ttf", L"CueClean.ttf", L"CueReadable.ttf"};
  const wchar_t* names[3] = {L"Cue Omni (TrueType)", L"Cue Clean (TrueType)", L"Cue Readable (TrueType)"};
  // the same fonts from when the app was called Player: taken out, so Windows lists them once
  const wchar_t* old_files[3] = {L"PlayerOmni.ttf", L"PlayerClean.ttf", L"PlayerReadable.ttf"};
  const wchar_t* old_names[3] = {L"Player Omni (TrueType)", L"Player Clean (TrueType)", L"Player Readable (TrueType)"};
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", 0, nullptr, 0,
                      KEY_READ | KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) return;
  bool changed = false;
  for (int i = 0; i < 3; ++i) {
    if (RegQueryValueExW(key, old_names[i], nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
      std::wstring old = dir + L"\\" + old_files[i];
      RemoveFontResourceExW(old.c_str(), 0, nullptr);
      RegDeleteValueW(key, old_names[i]);
      DeleteFileW(old.c_str());
      changed = true;
    }
  }
  for (int i = 0; i < 3; ++i) {
    std::wstring src = exe_dir() + L"\\fonts\\" + files[i], dst = dir + L"\\" + files[i];
    WIN32_FILE_ATTRIBUTE_DATA a{}, b{};
    if (!GetFileAttributesExW(src.c_str(), GetFileExInfoStandard, &a)) continue;
    bool same = GetFileAttributesExW(dst.c_str(), GetFileExInfoStandard, &b) && a.nFileSizeLow == b.nFileSizeLow;
    if (!same) {
      if (!CopyFileW(src.c_str(), dst.c_str(), FALSE)) { log("font copy failed"); continue; }
      AddFontResourceExW(dst.c_str(), 0, nullptr);
      changed = true;
    }
    // written only when missing or different: most starts touch nothing
    wchar_t cur[MAX_PATH * 2] = {};
    DWORD sz = sizeof cur - sizeof(wchar_t), type = 0;
    bool reg_ok = RegQueryValueExW(key, names[i], nullptr, &type, (BYTE*)cur, &sz) == ERROR_SUCCESS && type == REG_SZ &&
                  dst == cur;
    if (!reg_ok)
      RegSetValueExW(key, names[i], 0, REG_SZ, (const BYTE*)dst.c_str(), (DWORD)((dst.size() + 1) * sizeof(wchar_t)));
  }
  RegCloseKey(key);
  if (changed) {
    // tells the other apps without waiting for any of them (this used to wait up to 200 ms per window)
    SendNotifyMessageW(HWND_BROADCAST, WM_FONTCHANGE, 0, 0);
    log("fonts installed for this user");
  }
}

DWORD WINAPI App::engine_main(void* p) {
  auto* self = static_cast<App*>(p);
  WaitForSingleObject(self->engine_go_, INFINITE);
  // The shell is already visible. Load while its departure animation runs.
  bool loaded = self->mpv_.load(exe_dir(), &self->engine_err_);
  self->log(loaded ? "engine DLL mapped" : "engine DLL failed");
  bool ok = loaded && self->start_engine();
  PostMessageW(self->hwnd_, WM_APP_ENGINE, ok ? 1 : 0, 0);
  return 0;
}

// Runs on the engine thread (or inline if that thread could not start). Touches only mpv_,
// engine_cfg_, engine_err_, prop_name_ and has_background_box_, which the UI thread leaves
// alone until WM_APP_ENGINE.
bool App::start_engine() {
  log("loading engine");
  if (!mpv_.loaded() && !mpv_.load(exe_dir(), &engine_err_)) { log(engine_err_); return false; }
  if (!mpv_.create(hwnd_, WM_APP_MPV, engine_cfg_.wid, &engine_err_)) { log(engine_err_); return false; }
  std::wstring conf = data_dir_.wstring() + L"\\input.conf";
  write_input_conf(conf);
  auto o = [&](const char* k, const std::string& v) { mpv_.option(k, v); };
  // config dir = the app folder: it holds no mpv.conf, only fonts\ (older mpv reads ~~/fonts)
  o("config", "yes");
  o("config-dir", narrow(exe_dir()));
  o("terminal", "no");
  auto cloud_path = data_dir_ / "blur-cloud-1.0.hook";
  if (!file_contents_equal(cloud_path,kBlurCloudShader)) { std::ofstream shader(cloud_path, std::ios::binary); shader << kBlurCloudShader; }
  o("vo", "gpu-next");
  o("glsl-shaders", narrow(cloud_path.wstring()));
  o("hwdec", "auto-safe");  // hardware decoding when it is safe, software otherwise
  o("idle", "yes");
  o("force-window", "no");
  o("keep-open", "yes");  // the end of a film pauses on its last frame; the home is Esc or the arrow away
  o("osc", "no");
  o("load-scripts", "no");
  o("load-stats-overlay", "no");
  o("load-console", "no");
  o("load-auto-profiles", "no");
  o("save-position-on-quit", "no");
  o("ytdl", "no");
  o("input-default-bindings", "no");
  o("input-builtin-bindings", "no");
  o("input-conf", narrow(conf));
  o("input-vo-keyboard", "yes");
  o("input-cursor", "yes");
  o("input-media-keys", "yes");
  o("window-dragging", "no");
  o("cursor-autohide", "no");          // the app hides the cursor itself (works on any mpv)
  o("video-sync", "display-resample");  // draws at the display rate: the UI animates at 60 Hz+, not 24
  o("osd-level", "1");
  o("osd-bar", "no");
  o("osd-on-seek", "no");
  o("osd-playing-msg", "");
  o("hr-seek", "yes");
  // HDR on an SDR screen: tone mapping without the per-frame peak analysis (a compute pass over
  // every 4K frame); the picture stays right, the GPU does far less
  o("hdr-compute-peak", "no");
  o("audio-client-name", "Cue");
  o("sub-auto", "fuzzy");
  o("sub-file-paths", "subs;Subs;legendas;Legendas;legenda");
  o("embeddedfonts", "yes");
  o("sub-fonts-dir", narrow(exe_dir() + L"\\fonts"));          // mpv 0.37+
  // The bundled fonts are already available through ~~/fonts. Updating the
  // per-user Windows registration is maintenance, not a prerequisite for video.
  auto shader_cache = data_dir_ / "shader-cache";
  CreateDirectoryW(shader_cache.c_str(), nullptr);
  o("gpu-shader-cache-dir", narrow(shader_cache.wstring()));
  wchar_t loc[LOCALE_NAME_MAX_LENGTH] = {};
  GetUserDefaultLocaleName(loc, LOCALE_NAME_MAX_LENGTH);
  std::string lang = narrow(loc);  // "pt-BR"
  std::string base = lang.substr(0, lang.find('-'));
  // the language picked by hand last time comes first, then the system's, then the usual ones
  o("slang", (engine_cfg_.slang.empty() ? "" : engine_cfg_.slang + ",") + lang + "," + base + ",por,pob,en,eng");
  if (!engine_cfg_.alang.empty()) o("alang", engine_cfg_.alang);
  o("audio-pitch-correction", "yes");  // other speeds keep natural voices
  o("screenshot-format", "png");
  o("screenshot-png-compression", "5");
  o("screenshot-tag-colorspace", "yes");
  o("volume-max", std::to_string((int)kVolMax));
  o("volume", std::to_string((int)engine_cfg_.volume));
  o("mute", engine_cfg_.muted ? "yes" : "no");
  o("msg-level", "all=error");
  // mpv writes its log file at verbose level whatever msg-level says: every start and every file
  // opened paid for hundreds of lines on disk. Only with `Cue.exe --log` now.
  if (engine_cfg_.log) o("log-file", narrow((data_dir_ / "mpv.log").wstring()));
  log("engine initialize");
  int r = mpv_.api().initialize(mpv_.handle());
  if (r < 0) {
    engine_err_ = i18n::key_name(str::engine_failed);
    log(std::string("mpv_initialize: ") + mpv_.api().error_string(r));
    mpv_.destroy();
    return false;
  }
  mpv_.observe("pause", MPV_FORMAT_FLAG, O_PAUSE);
  mpv_.observe("time-pos", MPV_FORMAT_DOUBLE, O_TIME);
  mpv_.observe("duration", MPV_FORMAT_DOUBLE, O_DUR);
  mpv_.observe("volume", MPV_FORMAT_DOUBLE, O_VOL);
  mpv_.observe("mute", MPV_FORMAT_FLAG, O_MUTE);
  mpv_.observe("track-list", MPV_FORMAT_NODE, O_TRACKS);
  mpv_.observe("sid", MPV_FORMAT_STRING, O_SID);
  mpv_.observe("aid", MPV_FORMAT_STRING, O_AID);
  mpv_.observe("sub-delay", MPV_FORMAT_DOUBLE, O_SUBDELAY);
  mpv_.observe("sub-visibility", MPV_FORMAT_FLAG, O_SUBVIS);
  mpv_.observe("idle-active", MPV_FORMAT_FLAG, O_IDLE);
  mpv_.observe("path", MPV_FORMAT_STRING, O_PATH);
  mpv_.observe("sub-text", MPV_FORMAT_STRING, O_SUBTEXT);
  mpv_.observe("speed", MPV_FORMAT_DOUBLE, O_SPEED);
  mpv_.observe("hwdec-current", MPV_FORMAT_STRING, O_HWDEC);
  mpv_.observe("video-out-params",MPV_FORMAT_NODE,O_VIDEO_PARAMS);
  mpv_.observe("eof-reached", MPV_FORMAT_FLAG, O_EOF);
  mpv_.observe("osd-width", MPV_FORMAT_INT64, O_OSDW);
  mpv_.observe("osd-height", MPV_FORMAT_INT64, O_OSDH);
  mpv_.hook_add("on_load", kHookLoad);
  mpv_.hook_add("on_preloaded", kHookPreloaded);
  mpv_.hook_add("on_unload", kHookUnload);
  resolve_props();
  log("engine ok");
  return true;
}

// Back on the UI thread: from here on the engine is ours.
void App::on_engine_ready(bool ok) {
  if (engine_thread_) { CloseHandle(engine_thread_); engine_thread_ = nullptr; }
  if (closing_app_) return;
  if (!ok) {
    if (engine_err_.empty()) engine_err_ = i18n::key_name(str::engine_failed);
    engine_failed_ = true;
    home_.cancel_leave(now());
    home_.engine_problem = engine_text(engine_err_);
    if (!pending_open_.empty()) { pending_open_.clear(); home_.show_notice(home_.engine_problem, now()); }
    InvalidateRect(hwnd_, nullptr, FALSE);
    ensure_anim();
    return;
  }
  mpv_.api().set_wakeup_callback(mpv_.handle(), [](void* ctx) {
    static_cast<App*>(ctx)->wake_mpv();
  }, this);
  mpv_ok_ = true;
  drain_mpv();  // whatever the engine said while it was starting
  if (!pending_open_.empty()) { auto p = pending_open_; pending_open_.clear(); open(p); }
}

void App::start_maintenance() {
  if (maintenance_started_) return;
  HANDLE th = CreateThread(nullptr, 0, [](void* p) -> DWORD {
    auto* self = static_cast<App*>(p);
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    refresh_open_with();
    self->install_user_fonts();
    if (SUCCEEDED(com)) CoUninitialize();
    return 0;
  }, this, 0, nullptr);
  if (th) { maintenance_started_ = true; CloseHandle(th); }
  // after the start has settled: at most one quiet look at GitHub every 15 days
  if (update_due((int64_t)std::time(nullptr), store_.update_checked)) check_updates(false);
}

// ── updates ─────────────────────────────────────────────────────────
// Always on top: for a film in a corner while working. For this session only (a window that
// stays above everything should never come back by surprise on the next start).
void App::toggle_pin() {
  pinned_ = !pinned_;
  SetWindowPos(hwnd_, pinned_ ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  ambient_.set_topmost(pinned_);
  ambient_.follow(hwnd_);
  home_.chrome.pinned = play_.chrome.pinned = ob_.chrome.pinned = pinned_;
  say(tr(pinned_ ? str::win_pinned : str::win_unpinned));
  play_.changed(now());
  ensure_anim();
}

// The size, named, where the person is looking (the shortcuts change it without a menu).
void App::say_ui_size() {
  static const str::Id names[kUiSizes] = {str::size_small, str::size_medium, str::size_large, str::size_larger};
  say(i18n::format(str::size_now, {{"size", tr(names[ui_size_index(store_.ui_scale)])}}));
}

void App::say(const std::string& text) {
  double t = now();
  if (mode_ == Mode::Play) play_.toast(text, nullptr, t);
  else home_.show_notice(text, t);
  ensure_anim();
}

void App::show_update_state() {
  bool known = upd_ == Upd::Available || upd_ == Upd::Downloading || upd_ == Upd::Launching ||
               (upd_ == Upd::Checking && upd_install_after_check_);
  std::string v = short_version(upd_rel_.version);
  home_.update_version = known ? v : "";
  home_.update_busy = upd_ == Upd::Downloading || upd_ == Upd::Launching || upd_ == Upd::Checking;
  home_.update_progress = upd_ == Upd::Downloading && upd_pct_ >= 0 ? upd_pct_ / 100.0 : -1;
  play_.in.update_ready = known;
  switch (upd_) {
    case Upd::Idle: play_.in.update_label = tr(str::update_check); break;
    case Upd::Checking: play_.in.update_label = tr(str::update_checking); break;
    case Upd::Available: play_.in.update_label = i18n::format(str::update_to, {{"version", v}}); break;
    case Upd::Downloading:
      play_.in.update_label = i18n::format(str::update_downloading, {{"version", v}, {"percent", std::to_string(std::max(0, upd_pct_))}});
      break;
    case Upd::Launching: play_.in.update_label = tr(str::update_launching); break;
  }
  play_.changed(now());
  if (mode_ == Mode::Home && !onboarding_) InvalidateRect(hwnd_, nullptr, FALSE);
  ensure_anim();
}

void App::check_updates(bool manual) {
  if (upd_ == Upd::Checking || upd_ == Upd::Downloading || upd_ == Upd::Launching) return;
  upd_manual_ = manual;
  Upd before = upd_;
  upd_ = Upd::Checking;
  if (before == Upd::Available && !manual) upd_ = Upd::Available;  // a quiet refresh keeps the offer on screen
  log(manual ? "update check (asked)" : "update check");
  update_check_async(hwnd_, WM_APP_UPDATE, manual);
  show_update_state();
}

void App::start_update() {
  if (upd_ == Upd::Downloading || upd_ == Upd::Launching) return;
  if (upd_rel_.asset_url.empty()) {  // only the version is known: ask GitHub for the file first
    upd_install_after_check_ = true;
    upd_ = Upd::Idle;
    check_updates(true);
    return;
  }
  upd_ = Upd::Downloading;
  upd_pct_ = 0;
  log("update download " + upd_rel_.asset_name);
  update_download_async(hwnd_, WM_APP_UPDATE, upd_rel_);
  show_update_state();
}

void App::on_update(UpdateMsg* m) {
  switch (m->kind) {
    case UpdateMsg::Checked: {
      store_.update_checked = (int64_t)std::time(nullptr);
      bool newer = m->found && compare_versions(m->rel.version, CUE_VERSION) > 0;
      log("update check: " + (m->found ? "latest " + m->rel.version : m->error));
      if (newer) {
        upd_rel_ = m->rel;
        store_.update_available = m->rel.version;
        bool skipped = m->rel.version == store_.update_skipped && !m->manual;
        upd_ = skipped ? Upd::Idle : Upd::Available;
        save_state();
        if (upd_install_after_check_) { upd_install_after_check_ = false; start_update(); break; }
        if (m->manual) say(i18n::format(str::update_available, {{"version", short_version(m->rel.version)}}));
      } else {
        upd_ = Upd::Idle;
        upd_install_after_check_ = false;
        store_.update_available.clear();
        save_state();
        if (m->manual) say(tr(m->found ? str::update_latest : str::update_no_installer));
      }
      break;
    }
    case UpdateMsg::Progress:
      if (upd_ == Upd::Downloading) upd_pct_ = m->percent;
      break;
    case UpdateMsg::Downloaded:
      log("update downloaded, " + m->error);
      upd_ = Upd::Launching;
      show_update_state();
      save_position();
      if (update_launch_installer(m->file, exe_dir())) {
        log("installer started");
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);  // it waits for us to close, then installs and reopens Cue
      } else {
        upd_ = Upd::Available;
        say(tr(str::update_launch_failed));
      }
      break;
    case UpdateMsg::Failed:
      log("update failed: " + m->error);
      if (upd_ == Upd::Downloading) { upd_ = Upd::Available; say(tr(str::update_download_failed)); }
      else if (upd_ == Upd::Checking || upd_install_after_check_) {
        upd_ = upd_rel_.version.empty() ? Upd::Idle : Upd::Available;
        if (m->manual || upd_install_after_check_) {
          // say what actually happened: no release yet is not the same as no internet
          const std::string& e = m->error;
          if (e.find("http 404") != std::string::npos) say(tr(str::update_none_published));
          else if (e.find("http 403") != std::string::npos || e.find("http 429") != std::string::npos)
            say(tr(str::update_rate_limited));
          else if (e.find("http ") != std::string::npos) say(tr(str::update_bad_response));
          else say(tr(str::update_offline));
        }
        upd_install_after_check_ = false;
      }
      break;
  }
  show_update_state();
}



// ── mpv events ──────────────────────────────────────────────────────
void App::wake_mpv() {
  if (!mpv_wake_pending_.exchange(true))
    if (!PostMessageW(hwnd_, WM_APP_MPV, 0, 0)) mpv_wake_pending_ = false;
}

void App::drain_mpv() {
  // Clear before draining so a concurrent wake cannot be lost.
  mpv_wake_pending_ = false;
  if (!mpv_ok_ || !mpv_.ready()) return;  // not before the engine thread hands it over
  bool touched = false, overlay_ack = false;
  int processed = 0;
  while (processed++ < 128) {
    mpv_event* e = mpv_.wait();
    if (!e || e->event_id == MPV_EVENT_NONE) break;
    switch (e->event_id) {
      case MPV_EVENT_GET_PROPERTY_REPLY:
        if(e->reply_userdata==media_geometry_.current_request()) {
          bool changed=geometry_query_dirty_;
          geometry_query_dirty_=false;
          video_geometry_reply(e);
          if(changed) request_video_geometry(); // do not lose a reconfiguration received during this query
        }
        break;
      case MPV_EVENT_VIDEO_RECONFIG:
        request_video_geometry();
        break;
      case MPV_EVENT_SET_PROPERTY_REPLY:
        if (e->reply_userdata == pause_request_ && pause_pending_) {
          pause_pending_=false;
          if(e->error>=0) play_.in.paused=pause_target_;
          play_.changed(now()); touched=true;
        }
        break;
      case MPV_EVENT_PROPERTY_CHANGE:
        on_property(e->reply_userdata, (mpv_event_property*)e->data);
        // Keep the clock current, but a hidden toolbar needs no text/layout work.
        touched |= e->reply_userdata != O_TIME || (!IsIconic(hwnd_) && play_.needs_frame());
        break;
      case MPV_EVENT_CLIENT_MESSAGE: on_client_message((mpv_event_client_message*)e->data); break;
      case MPV_EVENT_FILE_LOADED: on_file_loaded(); break;
      case MPV_EVENT_END_FILE: on_end_file((mpv_event_end_file*)e->data); break;
      case MPV_EVENT_HOOK: {
        auto* h = (mpv_event_hook*)e->data;
        if (e->reply_userdata == kHookPreloaded) {
          configure_audio_mode();
          mpv_.hook_continue(h->id);
          break;
        }
        if (e->reply_userdata == kHookUnload) {
          // Unhook before the engine destroys/recreates its output, including
          // video -> audio replacements and failures before FILE_LOADED.
          release_video_window();
          mpv_.hook_continue(h->id);
          break;
        }
        // resume naturally: the start position is set before the file even opens
        std::string p = mpv_.get_string("path");
        double rp = store_.resume ? store_.resume_point(p) : -1;
        resume_toast_ = rp;
        if (rp > 0) mpv_.set("file-local-options/start", std::to_string(rp));
        // what the person chose for this file last time: subtitle delay and visibility
        if (const RecentItem* r = store_.find(p)) {
          if (std::fabs(r->sub_delay) > 0.001) mpv_.set("file-local-options/sub-delay", std::to_string(r->sub_delay));
          mpv_.set("sub-visibility", r->sub_visible ? "yes" : "no");
        } else {
          mpv_.set("sub-visibility", "yes");
        }
        tracks_applied_ = false;
        mpv_.set("pause", "no");  // a new file always plays, even if the last one ended paused
        mpv_.set("loop-file", store_.loop ? "inf" : "no");
        eof_ = false;
        play_.in.ended = false;
        mpv_.hook_continue(h->id);
        break;
      }
      case MPV_EVENT_COMMAND_REPLY:
        if (e->reply_userdata >= kReplyOverlay && e->reply_userdata < kReplyOverlay + PlayView::L_COUNT) {
          Ov& o = ov_[e->reply_userdata - kReplyOverlay];
          o.added(e->error >= 0);
          overlay_ack = true;
        }
        if (e->reply_userdata >= kReplyOverlayRemove && e->reply_userdata < kReplyOverlayRemove + PlayView::L_COUNT) {
          int i = (int)(e->reply_userdata - kReplyOverlayRemove);
          ov_[i].removed(e->error >= 0);
          // It may have become visible again while removal was queued.
          play_.layers[i].dirty = e->error >= 0 || play_.layers[i].visible;
          overlay_ack = true;
        }
        if (e->reply_userdata == kReplyShot) {
          if (e->error >= 0) { play_.toast_folder(tr(str::toast_frame_saved), now()); frame_saved_sound(); }  // with a button to its folder
          else { last_frame_.clear(); play_.toast(tr(str::toast_frame_failed), nullptr, now()); }
          ensure_anim();
        }
        if (e->reply_userdata == kReplySeek) {
          seek_in_flight_ = false;
          if (seek_pending_ >= 0) {
            double t = seek_pending_;
            seek_pending_ = -1;
            seek_in_flight_ = true;
            mpv_.cmd({"seek", std::to_string(t), "absolute+keyframes"}, kReplySeek);
          }
        }
        break;
      case MPV_EVENT_PLAYBACK_RESTART:
        audio_switch_restart();
        if(!media_geometry_.shape().valid()) request_video_geometry();
        if (loaded_ && !first_frame_) {
          log("first video frame (open " + std::to_string((int)((now() - open_started_) * 1000)) + "ms)");
          first_frame_ = true;
          background_warmup_.first_frame(now());
          try_start_iris();
        }
        break;
      case MPV_EVENT_SHUTDOWN: mpv_ok_ = false; break;
      default: break;
    }
  }
  if (mode_ == Mode::Play && next_advance_.return_home(media_idle_, loaded_)) go_home();
  if (processed > 128) wake_mpv(); // return to input between large event batches
  // a layer that changed while its previous picture was in flight goes out now
  if (overlay_ack) {
    upload_overlays();
    if (mode_ == Mode::Play)
      for (auto& L : play_.layers) if (L.dirty) { ensure_anim(); break; }
  }
  // replies to our own async writes change nothing on screen: skip them
  if (mode_ == Mode::Play && touched && !IsIconic(hwnd_)) {
    if (!next_ep_.empty() && !next_cancelled_ && !store_.loop && remote_timer_ != -1 && !scene_.visible() && play_.in.dur > 10) {
      double left = play_.in.dur - play_.in.time;
      double wall_left = left / std::max(.1, play_.in.speed);
      int seconds = wall_left <= 10 ? std::max(0, (int)std::ceil(wall_left)) : -1;
      if (seconds != next_count_) {
        next_count_ = seconds;
        play_.next_notice(seconds < 0 ? "" : (next_auto_ ? std::string(tr(str::player_up_next)) + " " + std::to_string(seconds) + "s" : std::string(tr(str::player_up_next_manual))), now());
      }
    }
    play_.changed(now());
    run_actions();
    // time-pos changes on every video frame: with nothing on screen there is nothing to draw
    if (play_.needs_frame()) ensure_anim();
  }
}

void App::on_property(uint64_t id, const mpv_event_property* p) {
  auto dbl = [&](double def) { return (p->format == MPV_FORMAT_DOUBLE && p->data) ? *(double*)p->data : def; };
  auto flag = [&](bool def) { return (p->format == MPV_FORMAT_FLAG && p->data) ? *(int*)p->data != 0 : def; };
  auto str = [&]() { return (p->format == MPV_FORMAT_STRING && p->data && *(char**)p->data) ? std::string(*(char**)p->data) : std::string(); };
  PlayInput& in = play_.in;
  switch (id) {
    case O_PAUSE: if (!pause_pending_ && !audio_switch_.stage) in.paused = flag(false); if (in.paused) save_position(); break;
    case O_OSDW: case O_OSDH:
      if (p->format == MPV_FORMAT_INT64 && p->data) (id==O_OSDW?osd_w_:osd_h_)=(int)*(int64_t*)p->data;
      break;
    case O_TIME:
      if (audio_switch_.stage && audio_switch_.stage < 5) break;
      in.time = dbl(in.time);
      last_time_ = in.time;
      if (what_until_ >= 0 && in.time >= what_until_ + 0.4) end_what(true);
      // A replacement can coalesce playback-restart notifications. A valid
      // advancing media clock after loading also releases the warmup gate.
      if (loaded_ && !first_frame_ && !in.paused && now() - loaded_at_ > 1.2 &&
          p->format == MPV_FORMAT_DOUBLE && p->data) {
        first_frame_ = true;
        background_warmup_.first_frame(now());
        try_start_iris();
      }
      break;
    case O_DUR:
      in.dur = dbl(0);
      if (loaded_ && in.dur > 0) store_.set_position(path_, last_time_, in.dur);
      break;
    case O_VOL: in.volume = dbl(in.volume); store_.volume = in.volume; break;
    case O_MUTE: in.muted = flag(false); store_.muted = in.muted; break;
    case O_SUBDELAY:
      in.sub_delay = dbl(0);
      if (loaded_) if (RecentItem* r = store_.find(path_)) { r->sub_delay = in.sub_delay; save_state(); }
      break;
    case O_SUBVIS:
      if (snap_.subs_hidden) break;  // the snap hid them for a moment; the person's choice stands
      in.sub_visible = flag(true);
      if (what_until_ >= 0 || now() - what_restored_at_ < 1.5) break;  // shown for a moment by the remote: not a choice
      if (loaded_ && now() - loaded_at_ > 1.0) if (RecentItem* r = store_.find(path_)) { r->sub_visible = in.sub_visible; save_state(); }
      break;
    case O_SID:
    case O_AID: {
      std::string v = str();
      int64_t idv = (v.empty() || v == "no") ? 0 : std::atoll(v.c_str());
      // while tracks play together the engine reports none selected: aid keeps the last single
      // track, the one to return to when the mix ends; the "no" of a forced reselection is not a
      // choice either (it must never be remembered as "no audio" for the file)
      if (id == O_AID && (amix_ || (aid_reselect_ && idv != aid_reselect_))) break;
      if (id == O_AID) aid_reselect_ = 0;
      if (id == O_SID && sid_reselect_ && idv != sid_reselect_) break;  // the refresh's passing "no"
      if (id == O_SID) sid_reselect_ = 0;
      (id == O_SID ? in.sid : in.aid) = idv;
      // a choice made by hand (not the engine's pick at load) is remembered for the file,
      // and its language becomes the preferred one for the next files
      // (audio is never chosen "off" by hand: a "no" is the engine losing its device, not a choice)
      if (loaded_ && tracks_applied_ && !amix_ && !(id == O_AID && idv == 0) && now() - loaded_at_ > 1.0 && what_until_ < 0 && now() - what_restored_at_ > 1.5) {
        if (RecentItem* r = store_.find(path_)) {
          std::string lang;
          for (auto& t : id == O_SID ? in.subs : in.audios) if (t.id == idv) lang = t.lang;
          if (id == O_SID) { r->sid = idv; r->sid_lang = lang; if (!lang.empty()) store_.slang = lang; }
          else { r->aid = idv; r->aid_lang = lang; if (!lang.empty()) store_.alang = lang; }
          save_state();
        }
      }
      break;
    }
    case O_SPEED:
      // while the speed slider is held, the engine's echoes lag behind the hand: the hand wins
      if (!play_.dragging()) in.speed = dbl(1.0);
      break;
    case O_HWDEC: hwdec_ = str(); break;
    case O_VIDEO_PARAMS:
      request_video_geometry(); // read the complete CURRENT tuple, never merge scalar events
      break;
    case O_SUBTEXT: {
      std::string t = str();
      int lines = 1, longest = 0, cur = 0;
      for (size_t i = 0; i < t.size();) {
        if (t[i] == '\n') { ++lines; longest = std::max(longest, cur); cur = 0; ++i; continue; }
        next_cp(t, i);
        ++cur;
      }
      longest = std::max(longest, cur);
      in.sub_shown = !t.empty();
      if (!t.empty()) { in.sub_lines = lines; in.sub_chars = std::max(8, longest); }
      in.sub_text.clear();
      for (size_t st = 0; !t.empty() && st <= t.size();) {
        size_t en = t.find('\n', st);
        if (en == std::string::npos) en = t.size();
        in.sub_text.push_back(t.substr(st, en - st));
        st = en + 1;
      }
      return;  // no dock refresh needed
    }
    case O_TRACKS: {
      if (p->format != MPV_FORMAT_NODE || !p->data) break;
      auto all = Mpv::parse_tracks((mpv_node*)p->data);
      in.subs.clear();
      in.audios.clear();
      int ext = 0;
      SubKind kind = SubKind::None;
      for (auto& t : all) {
        if (t.type == "sub") { in.subs.push_back(t); if (t.external) ++ext; if (t.selected) kind = sub_kind(t.codec); }
        if (t.type == "audio") in.audios.push_back(t);
      }
      bool kind_changed = kind != in.kind;
      in.kind = kind;
      if (loaded_ && ext > ext_subs_ && now() - loaded_at_ > 1.5) play_.toast("CC", "check", now(), true);  // CC ✓
      ext_subs_ = ext;
      if (kind_changed && loaded_) apply_style();
      if (loaded_ && !tracks_applied_ && !all.empty()) apply_media_tracks();
      break;
    }
    case O_IDLE:
      media_idle_ = flag(false); // decide after the event batch, including END_FILE/EOF

      break;
    case O_PATH: observed_path_ = str(); break;
    case O_EOF: {
      bool e = flag(false);
      if (e && !eof_ && loaded_) {
        // the film ended: it stays on its last frame, paused, controls in view; next time it starts over
        store_.set_position(path_, 0, in.dur);
        save_state();
        play_.activity(now());
        // the remote's sleep timer said "at the end of this episode"; else, with a phone in
        // hand, the next episode follows
        if (remote_timer_ == -1) { remote_timer_ = 0; next_cancelled_ = true; say(tr(str::remote_goodnight)); }
        else queue_next_episode();
      }
      eof_ = e;
      if (play_.in.ended != (e && loaded_)) { play_.in.ended = e && loaded_; play_.changed(now()); ensure_anim(); }
      break;
    }
  }
}

// Once the tracks of a file are known: give back the ones chosen for it last time, but only
// if a track with that number still has the same language (a different file stays safe).
void App::apply_media_tracks() {
  tracks_applied_ = true;
  const RecentItem* r = store_.find(path_);
  // all the tracks together: when chosen for this file before, or when the file is plainly a
  // recording in parts and nothing was chosen for it yet (said once, changed in the audio menu)
  if (play_.in.audios.size() >= 2) {
    if (r && r->aid == -2) {
      // remembered as "-2" with the track ids in aid_lang ("" = all of them)
      std::vector<int64_t> ids;
      for (size_t i = 0; i < r->aid_lang.size();) {
        size_t e = r->aid_lang.find(',', i);
        if (e == std::string::npos) e = r->aid_lang.size();
        int64_t id = std::atoll(r->aid_lang.substr(i, e - i).c_str());
        for (auto& t : play_.in.audios) if (t.id == id) ids.push_back(id);
        i = e + 1;
      }
      if (r->aid_lang.empty() || ids.size() >= 2) {
        set_audio_mix(r->aid_lang.empty() ? all_audio_ids() : ids, false);
        pick_subs_only(r);
        return;
      }
    }
    if ((!r || r->aid < 0) && looks_like_multitrack_recording(play_.in.audios)) {
      set_audio_mix(all_audio_ids(), false);
      say(i18n::format(str::audio_mixed, {{"n", std::to_string(play_.in.audios.size())}}));
      if (r) { pick_subs_only(r); }
      return;
    }
  }
  if (!r) return;
  auto pick = [&](const std::vector<Track>& v, int64_t want, const std::string& lang, const char* prop) {
    if (want < 0) return;
    if (want == 0) { mpv_.set_async(prop, "no"); return; }
    for (auto& t : v)
      if (t.id == want && t.lang == lang) { mpv_.set_async(prop, std::to_string(want)); return; }
  };
  pick(play_.in.subs, r->sid, r->sid_lang, "sid");
  if (!audio_only_ && r->aid > 0) pick(play_.in.audios, r->aid, r->aid_lang, "aid");  // never "no audio" from memory
}

void App::pick_subs_only(const RecentItem* r) {
  if (r->sid == 0) mpv_.set_async("sid", "no");
  else if (r->sid > 0)
    for (auto& t : play_.in.subs)
      if (t.id == r->sid && t.lang == r->sid_lang) { mpv_.set_async("sid", std::to_string(r->sid)); break; }
}

// One track, heard on its own. Coming out of a mix the engine may still think this track is the
// selected one (its aid never changed while the tracks were mixed), and choosing it again would
// do nothing: no decoder, no sound. So it is let go first, then chosen.
void App::select_audio(int64_t id) {
  if (id <= 0) return;
  bool was_mix = amix_;
  if (was_mix || audio_switch_.stage || (audio_only_ && waveform_active_)) {
    std::string graph = audio_only_ && waveform_active_ ? audio_waveform_graph(id) : "";
    if (!change_audio_graph(graph, id)) return;
    aid_reselect_ = id;
  } else {
    if (play_.in.aid == id) return;
    mpv_.set_async("aid", std::to_string(id));
  }
  amix_ = play_.in.amix = false;
  play_.in.amix_ids.clear();
  play_.in.aid = id;
  // the engine may report no change at all (same track before and after the mix): the file
  // remembers this choice here, not from that event
  if (was_mix)
    if (RecentItem* r = store_.find(path_)) {
      r->aid = id;
      r->aid_lang.clear();
      for (auto& t : play_.in.audios) if (t.id == id) r->aid_lang = t.lang;
      save_state();
    }
  play_.changed(now());
  remote_publish();
}

// Rebuilding the graph flushes demux/decoder state. A seek in the same command
// list can be consumed by that flush and land at the NEXT keyframe. Wait for the
// rebuild to restart, THEN seek, keeping the picture covered until that seek ends.
bool App::change_audio_graph(const std::string& graph, int64_t single_track) {
  if (!loaded_ || eof_)
    return mpv_.cmd_batch(audio_switch_commands(graph, single_track));
  if (fullscreen_transition_) settle_fullscreen_motion();
  auto& a = audio_switch_;
  if (!a.stage || a.stage == 5) {
    a.position = mpv_.get_double("time-pos", play_.in.time);
    a.paused = pause_pending_ ? pause_target_ : play_.in.paused;
    a.stage = 1; a.at = now(); a.retries = 0;
    mpv_.set("pause", "yes");
    snap_subtitles(true);
  }
  a.graph = graph; a.track = single_track; a.dirty = true;
  if (IsIconic(hwnd_)) {
    a.dirty = false; a.stage = 3; a.at = now();
    mpv_.cmd_batch(audio_switch_commands(a.graph, a.track));
  }
  ensure_anim();
  return true;
}

void App::cancel_audio_switch() {
  if (!audio_switch_.stage) return;
  bool paused = audio_switch_.paused;
  audio_switch_ = {};
  end_snap();
  if (mpv_ok_) mpv_.set("pause", paused ? "yes" : "no");
}

// The picture while the sound changes language: it drains to black and white and breaks into
// big pixels, which then shuffle (like letters turning over) while the engine rebuilds and finds
// its place again; then the pixels settle, smaller and smaller, and the colour comes back last.
// The shuffle also hides the moment the engine shows a frame from further on.
namespace {
constexpr double kSwitchOut = 0.16, kSwitchIn = 0.32, kSwitchDim = 0.32;
double snap_px(double amount, int height);  // the mosaic's block size (defined with the fullscreen motion)
}

bool App::step_audio_switch(double t) {
  auto& a = audio_switch_;
  if (!a.stage) return false;
  bool rm = reduce_motion();
  auto shuffle = [&] {
    // two coarse block sizes taking turns, a dozen times a second
    int beat = (int)std::floor((t - a.at) / 0.085);
    set_snap(kSwitchDim, snap_px(beat % 2 ? 0.84 : 1.0, ch_), 1.0);
  };
  if (a.stage == 1) {
    double k = rm ? 1.0 : clamp01((t - a.at) / kSwitchOut);
    set_snap(kSwitchDim * k, snap_px(0.3 + 0.7 * k * k, ch_), smoothstep(0.0, 0.7, k));
    if (k >= 1) { a.stage = 2; a.at = t; }
  } else if (a.stage == 2 && t - a.at < .05) {
    shuffle();
  } else if (a.stage == 2 && t - a.at >= .05) {
    // Give the shader's black picture a couple of presentation frames first.
    a.dirty = false; a.stage = 3; a.at = t;
    if (!mpv_.cmd_batch(audio_switch_commands(a.graph, a.track))) {
      cancel_audio_switch();
      return false;
    }
  } else if ((a.stage == 3 || a.stage == 4) && t - a.at > 4) {
    // A failed decoder must not leave the picture scrambled indefinitely.
    cancel_audio_switch();
    return false;
  } else if (a.stage == 3 || a.stage == 4) {
    if (!rm) shuffle();
  } else if (a.stage == 5) {
    double k = rm ? 1.0 : clamp01((t - a.at) / kSwitchIn);
    double blocks = 1 - ease_out(k);
    set_snap(kSwitchDim * (1 - ease_out(clamp01(k * 1.4))), snap_px(blocks, ch_), 1 - smoothstep(0.5, 1.0, k));
    if (k >= 1) {
      a.stage = 0;
      end_snap();
      // the person's choice, said once more: whatever the engine went through, a subtitle that
      // is on in the menu is on on screen
      if (play_.in.sid > 0 && play_.in.sub_visible) mpv_.set_flag_async("sub-visibility", true);
      return false;
    }
  }
  return true;
}

// After the audio graph is rebuilt and the engine has jumped back, the subtitle that was on
// screen can be lost (its line began before the place the engine came back to, and is not read
// again): the track still says "on", nothing shows. Letting go of the track and taking it again
// makes the engine read the current line anew. Its passing "no" is not the person's choice.
void App::refresh_subtitles() {
  int64_t sid = play_.in.sid;
  if (sid <= 0 || !mpv_ok_) return;
  sid_reselect_ = sid;
  mpv_.cmd_batch({{"set", "sid", "no"}, {"set", "sid", std::to_string(sid)}});
}

void App::audio_switch_restart() {
  auto& a = audio_switch_;
  if (a.stage != 3 && a.stage != 4) return;
  if (a.dirty) { a.stage = 2; a.at = now(); return; }
  double pos = mpv_.get_double("time-pos", -1);
  if (a.stage == 3 || (std::fabs(pos - a.position) > .075 && a.retries < 3)) {
    a.stage = 4; a.at = now(); ++a.retries;
    mpv_.cmd({"seek", std::to_string(a.position), "absolute+exact"});
    return;
  }
  a.stage = 5; a.at = now();
  play_.in.time = last_time_ = pos;
  refresh_subtitles();
  mpv_.set("pause", a.paused ? "yes" : "no");
  if (IsIconic(hwnd_)) { a.stage = 0; end_snap(); }
  ensure_anim();
}

std::vector<int64_t> App::all_audio_ids() const {
  std::vector<int64_t> ids;
  for (auto& t : play_.in.audios) ids.push_back(t.id);
  return ids;
}

// Several tracks at once. The engine rebuilds its audio for each change, so an unchanged set
// is never sent again; the panel, the phone and the saved choice follow the same list.
void App::set_audio_mix(std::vector<int64_t> ids, bool remember) {
  std::sort(ids.begin(), ids.end());
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  if (ids.size() < 2) return;
  if (!amix_ || ids != play_.in.amix_ids) {
    std::string g = audio_mix_graph(ids, audio_only_ && waveform_active_);
    if (g.empty()) return;
    if (!change_audio_graph(g, 0)) return;
  }
  amix_ = play_.in.amix = true;
  play_.in.amix_ids = ids;
  if (remember)
    if (RecentItem* r = store_.find(path_)) {
      r->aid = -2;
      r->aid_lang.clear();
      if (ids.size() < play_.in.audios.size())
        for (size_t i = 0; i < ids.size(); ++i) r->aid_lang += (i ? "," : "") + std::to_string(ids[i]);
      save_state();
    }
  play_.changed(now());
  remote_publish();
}

// The mix is this file's: the next one starts plain (and finds its own tracks).
void App::clear_audio_mix() {
  cancel_audio_switch();
  aid_reselect_ = 0;
  if (!amix_) return;
  amix_ = play_.in.amix = false;
  play_.in.amix_ids.clear();
  if (mpv_ok_) mpv_.set("lavfi-complex", "");
}

void App::configure_audio_mode() {
  mpv_node node{};
  std::vector<Track> tracks;
  if (mpv_.api().get_property(mpv_.handle(), "track-list", MPV_FORMAT_NODE, &node) >= 0) {
    tracks = Mpv::parse_tracks(&node);
    mpv_.api().free_node_contents(&node);
  }
  const RecentItem* recent = store_.find(path_);
  int64_t id = audio_visual_track(tracks, recent ? recent->aid : -1,
                                  recent && !recent->aid_lang.empty() ? recent->aid_lang : store_.alang);
  audio_only_ = id > 0;
  play_.in.audio_only = audio_only_;
  waveform_active_ = false;
  // File-local options are restored by mpv on unload: a following film must
  // never inherit the waveform, low-resolution scaler or audio clock.
  if (!audio_only_) return;
  mpv_.set("file-local-options/force-window", "yes"); // controls even without a video stream
  mpv_.set("file-local-options/audio-display", "no");
  mpv_.set("file-local-options/video-sync", "audio"); // visualization never retimes the sound
  mpv_.set("file-local-options/keep-open", "always");
  mpv_.set("file-local-options/aid", std::to_string(id));
  play_.in.aid = id;
  if (!waveform_fallback_) {
    mpv_.set("file-local-options/scale", "nearest");
    mpv_.set("file-local-options/dscale", "nearest");
    waveform_active_ = mpv_.set("file-local-options/lavfi-complex", audio_waveform_graph(id));
    if (!waveform_active_) log("audio waveform filter unavailable; keeping audio controls");
  }
}

void App::on_client_message(const mpv_event_client_message* m) {
  if (m->num_args < 3 || std::string(m->args[0]) != "key-binding") return;
  std::string name = m->args[1];
  char st = m->args[2][0];
  bool press = st == 'd' || st == 'r' || st == 'p';
  // Buttons come directly from Win32, including WM_LBUTTONDBLCLK as a second
  // down. mpv's synthetic double-click bindings must not inject extra clicks.
  if (name == "mb_left" || name == "mb_right") return;
  if (!press) return;
  if (name == "wheel_up" || name == "wheel_down") { play_.wheel(name == "wheel_up" ? 1 : -1, now()); run_actions(); ensure_anim(); return; }
  if (name == "ignore") return;
  if (name == "lbracket") name = "[";
  else if (name == "rbracket") name = "]";
  else if (name == "backslash") name = "\\";
  bool shift = name.rfind("s_", 0) == 0, ctrl = name.rfind("c_", 0) == 0;
  std::string k = (shift || ctrl) ? name.substr(2) : name;
  key(k, shift, ctrl);
}

// Read the engine's already-probed metadata only when requested; no new decoder,
// file scan, stat on a network path, background timer or ffprobe process.
void App::refresh_file_info() {
  MediaDetails info;
  info.name = file_name(path_);
  info.audio_only = audio_only_;
  if (mpv_ok_ && loaded_) {
    info.bytes = mpv_.get_double("file-size", -1);
    info.duration = mpv_.get_double("duration", play_.in.dur);
    info.container = mpv_.get_string("file-format");
    if (!audio_only_) {
      info.width = (int)mpv_.get_double("current-tracks/video/demux-w", 0);
      info.height = (int)mpv_.get_double("current-tracks/video/demux-h", 0);
      if (info.width <= 0) info.width = (int)mpv_.get_double("video-params/w", 0);
      if (info.height <= 0) info.height = (int)mpv_.get_double("video-params/h", 0);
      info.fps = mpv_.get_double("current-tracks/video/demux-fps", 0);
      info.video_codec = mpv_.get_string("current-tracks/video/codec");
    }
    info.audio_codec = mpv_.get_string("current-tracks/audio/codec");
    info.audio_layout = mpv_.get_string("audio-params/hr-channels");
    info.channels = (int)mpv_.get_double("audio-params/channel-count", 0);
    info.sample_rate = mpv_.get_double("audio-params/samplerate", 0);
  }
  play_.in.file_info = media_info_fields(info);
  ++play_.in.file_info_revision;
  play_.changed(now());
}

void App::on_file_loaded() {
  // The path property may still describe the previous file when FILE_LOADED
  // arrives. open() already owns the requested path; never replace it with a stale observation.
  closing_video_ = false;
  hide_video_pending_ = false;
  loaded_ = true;
  loaded_at_ = now();
  log("file loaded");
  double dur = play_.in.dur;
  store_.touch(path_, dur, (int64_t)std::time(nullptr));
  play_.in.style = store_.style_for(path_);
  play_.in.default_style = store_.default_style;
  play_.place = store_.dock;
  ext_subs_ = 0;
  for (auto& t : play_.in.subs) if (t.external) ++ext_subs_;
  set_title(file_name(path_));
  play_.resize(cw_, ch_, portrait_scale(s_, cw_, ch_, store_.ui_scale));
  play_.reset(now());
  // black over the picture until its first frame is ready, then the pixel iris opens it
  first_frame_ = false;
  if (audio_only_) {
    // Audio does not have to wait for a native video frame to reveal its controls.
    first_frame_ = true;
    background_warmup_.first_frame(now());
  }
  geom_done_ = false;   // a new file may want a different window shape
  logged_hw_ = false;
  accent_k_target_ = 0;  // neutral until this film's first colours are read
  accent_req_t_ = -10;
  user_sized_ = false;
  request_video_geometry();
  play_.begin_cover();
  play_.prepare(now());
  upload_overlays();
  if (mode_ != Mode::Play) {
    // the home finishes its goodbye first (a dropped file gets swallowed), then the video shows
    if (home_.leaving() && !home_.leave_done(now())) reveal_pending_ = true;
    else reveal_play();
  }
  if (resume_toast_ > 0) play_.toast(fmt_time(resume_toast_), "resume", now());
  resume_toast_ = -1;
  if (!tracks_applied_ && (!play_.in.subs.empty() || !play_.in.audios.empty())) apply_media_tracks();
  for (auto& s : pending_subs_) mpv_.cmd({"sub-add", s, "select"});
  pending_subs_.clear();
  apply_style();
  save_state();
  end_what(false);
  find_next_episode();
  remote_publish();
  if (!audio_only_) SetTimer(hwnd_, T_THUMB, (UINT)(PlaybackWarmup::kAtmosphereDelay * 1000), nullptr);
  else KillTimer(hwnd_, T_THUMB);
  ensure_anim();
}

void App::reveal_play() {
  reveal_pending_ = false;
  if (mode_ == Mode::Play) return;
  mode_ = Mode::Play;
  home_.drag_leave(now());
  set_edge_highlight(false);
  upload_overlays();
  ShowWindow(video_, SW_SHOW);
  home_canvas_ = Canvas{};  // the hidden home no longer needs a full-window bitmap
  home_back_ = Canvas{};
  SetTimer(hwnd_, T_POLL, 33, nullptr);
  SetTimer(hwnd_, T_SAVE, 10000, nullptr);
  try_start_iris();
  ensure_anim();
}

void App::try_start_iris() {
  if (mode_ == Mode::Play && play_.iris_waiting() && (first_frame_ || now() - loaded_at_ > 1.2)) {
    play_.start_iris(now());
    log("iris start");
    ensure_anim();
  }
}

void App::on_end_file(const mpv_event_end_file* e) {
  if (e && e->reason == MPV_END_FILE_REASON_ERROR && audio_only_ && waveform_active_ && !waveform_fallback_) {
    release_video_window();
    waveform_fallback_ = true;
    waveform_active_ = false;
    loaded_ = false;
    log("waveform initialization failed; retrying audio without visualization");
    mpv_.cmd({"loadfile", path_, "replace"});
    return;
  }
  if (!loaded_) {
    reveal_pending_ = false;
    if (mode_ == Mode::Home) home_.cancel_leave(now());
    if (e && e->reason == MPV_END_FILE_REASON_ERROR) {
      home_.show_notice(tr(str::notice_open_failed), now());
      log("could not open: " + path_);
      ensure_anim();
    }
    return;
  }
  if (e && e->reason == MPV_END_FILE_REASON_EOF) queue_next_episode();
  save_position(e && e->reason == MPV_END_FILE_REASON_EOF);
  loaded_ = false;
  // the engine may destroy its video window next: let go of it first
  if (e && (e->reason == MPV_END_FILE_REASON_EOF || e->reason == MPV_END_FILE_REASON_ERROR)) release_video_window();
}

void App::save_position(bool finished) {
  if (path_.empty() || !loaded_) return;
  if (store_.set_position(path_, finished ? 0 : last_time_, play_.in.dur)) save_state();
}

void App::save_state() { SetTimer(hwnd_, T_PERSIST, 400, nullptr); }

namespace {
struct SaveJob { Store snapshot; std::filesystem::path path; };
DWORD WINAPI save_worker(void* context) {
  auto* job = static_cast<SaveJob*>(context);
  bool ok = false;
  try { ok = job->snapshot.save(job->path); } catch (...) { /* report through the thread result */ }
  delete job;
  return ok ? 0 : 1;
}
}

void App::save_now(bool synchronous) {
  KillTimer(hwnd_, T_PERSIST);
  if (save_thread_) {
    // Never overlap writers to state.json.tmp. Slow disks do not block controls.
    if (WaitForSingleObject(save_thread_, synchronous ? INFINITE : 0) == WAIT_TIMEOUT) {
      SetTimer(hwnd_, T_PERSIST, 100, nullptr);
      return;
    }
    DWORD result = 1;
    GetExitCodeThread(save_thread_, &result);
    CloseHandle(save_thread_);
    save_thread_ = nullptr;
    if (result) log("could not save previous state");
  }
  if (!synchronous) {
    auto* job = new SaveJob{store_, state_path_}; // worker owns an immutable snapshot
    save_thread_ = CreateThread(nullptr, 0, save_worker, job, 0, nullptr);
    if (save_thread_) return;
    delete job;
  }
  // Closing/logoff: the window is already hidden; persist the latest state last.
  if (!store_.save(state_path_)) log("could not save state");
}

// ── what's new ──────────────────────────────────────────────────────
void App::show_news(bool asked) {
  if (asked) news_due_ = true;
  // only over a calm home: a file on its way, the welcome or the pairing scene come first
  if (!news_due_ || news_host_.visible() || mode_ != Mode::Home || onboarding_ || !pending_open_.empty() || scene_.visible() || IsIconic(hwnd_)) return;
  news_due_ = false;
  store_.news_seen = CUE_VERSION;
  save_state();
  news_.version = CUE_VERSION_SHORT;
  news_host_.show(&news_, hwnd_, s_, news_host_.now());
  log("news");
}

// Closed by hand: it comes back only with the next version.
void App::close_news_line() {
  home_.news = false;
  store_.news_closed = CUE_VERSION;
  save_state();
}

// ── opening ─────────────────────────────────────────────────────────
void App::open(std::string path, std::vector<std::string> subs) {
  // Own the argument before clearing next_ep_ (manual, phone and EOF callers
  // can all pass that member). Empty input must not tear down current playback.
  if (path.empty()) { log("ignored empty open request"); return; }
  next_advance_.clear();
  next_thumb_job_.reset(); next_thumb_requested_ = false; play_.set_next_details("");
  media_idle_ = false;
  news_due_ = false;
  if (news_host_.visible()) news_host_.close(news_host_.now());
  settle_fullscreen_motion();  // a fullscreen movement in progress lands at once
  thumb_stop();
  reset_atmosphere();
  if (onboarding_) {
    onboarding_ = false;
    store_.onboarded = true;
    save_state();
    home_.enter(now(), true);
  }
  if (mode_ == Mode::Home && mpv_ok_) { home_.leave(now(), swallow_next_); ensure_anim(); }
  swallow_next_ = false;
  if (!mpv_ok_) {
    // engine_err_ belongs to the engine thread until it reports back: only our own flag is read here
    if (engine_failed_) { home_.show_notice(home_.engine_problem, now()); ensure_anim(); }
    else pending_open_ = path;
    return;
  }
  if (loaded_) save_position();
  next_cancelled_ = false; next_count_ = -1; next_auto_ = false; next_ep_.clear(); play_.in.has_next = false; KillTimer(hwnd_, T_NEXT);
  loaded_ = false; pause_pending_=false; ++pause_request_;
  audio_only_ = waveform_active_ = waveform_fallback_ = false;
  play_.in.audio_only = false;
  play_.in.file_info.clear();
  ++play_.in.file_info_revision;
  vid_w_ = vid_h_ = vid_rot_ = 0;
  media_geometry_.reset();
  geometry_query_pending_=false;
  geometry_query_dirty_=false;
  geom_.on=false; geom_done_=false; user_sized_=false; pending_ar_=0;
  KillTimer(hwnd_,T_GEOM);
  path_ = path;
  open_started_ = now();
  log("open requested");
  observed_path_.clear();
  pending_subs_ = std::move(subs);
  clear_audio_mix();
  mpv_.cmd({"loadfile", path, "replace"});
}

void App::handle_files(const std::vector<std::string>& files) {
  std::vector<std::string> media, subs;
  for (auto& f : files) (is_subtitle_path(f) ? subs : media).push_back(f);
  if (!media.empty()) { open(media[0], subs); return; }
  if (subs.empty()) return;
  if (mode_ == Mode::Play && loaded_) {
    for (auto& s : subs) mpv_.cmd({"sub-add", s, "select"});  // "CC ✓" comes from the track list
  } else {
    home_.show_notice(tr(str::notice_open_video_first), now());
    ensure_anim();
  }
}

void App::go_home() {
  next_thumb_job_.reset(); next_thumb_requested_ = false; play_.set_next_details("");
  media_idle_ = false;
  next_advance_.clear();
  scene_paused_ = false;
  if (scene_.visible()) scene_.close(now());
  KillTimer(hwnd_, T_NEXT);
  next_count_ = -1; next_cancelled_ = true;
  play_.next_notice("", now());
  thumb_stop();
  clear_audio_mix();
  end_what(false);
  next_ep_.clear();
  background_warmup_.reset();
  reveal_pending_ = false;
  home_.enter(now());
  mode_ = Mode::Home;  // `loaded_` stays: the end-file event may still arrive and must save the position
  remove_overlays();
  KillTimer(hwnd_, T_SAVE);
  SetTimer(hwnd_, T_POLL, 33, nullptr);  // the eyes follow the cursor on the home
  // the video window is hidden only once the engine has finished destroying its own window
  hide_video_pending_ = true;
  hide_video_since_ = now();
  try_hide_video();
  settle_fullscreen_motion();  // a fullscreen movement in progress lands at once
  if (fullscreen_) perform_fullscreen_switch();
  // Both portrait and ultrawide films must give the home its normal 16:9 canvas.
  pending_ar_=0;
  adapt_window(16.0 / 9.0);
  set_title("Cue");
  refresh_recents();
  home_.set_size(cw_, ch_, s_);
  InvalidateRect(hwnd_, nullptr, FALSE);
  ensure_anim();
}

// Checking whether files still exist can take seconds (a sleeping external drive, a network
// share), so it runs on a worker thread; the list is drawn at once and corrected when it answers.
struct RecentsCheck { HWND hwnd; UINT msg; unsigned gen; std::vector<std::wstring> paths; };
static DWORD WINAPI recents_worker(void* p) {
  auto* job = static_cast<RecentsCheck*>(p);
  auto* missing = new std::vector<std::wstring>;
  for (auto& w : job->paths)
    if (GetFileAttributesW(w.c_str()) == INVALID_FILE_ATTRIBUTES) missing->push_back(w);
  if (!PostMessageW(job->hwnd, job->msg, job->gen, (LPARAM)missing)) delete missing;
  delete job;
  return 0;
}

void App::refresh_recents(bool check) {
  if (check && !store_.recents.empty()) {
    auto* job = new RecentsCheck{hwnd_, WM_APP_RECENTS, ++recents_gen_, {}};
    for (auto& r : store_.recents) job->paths.push_back(widen(r.path));
    if (HANDLE th = CreateThread(nullptr, 0, recents_worker, job, 0, nullptr)) CloseHandle(th);
    else delete job;
  }
  std::vector<HomeRow> rows;
  shown_paths_.clear();
  for (auto& r : store_.recents) {
    if (rows.size() >= 6) break;
    if (missing_.count(widen(r.path))) continue;  // gone: not shown, not deleted
    shown_paths_.push_back(r.path);
    HomeRow hr;
    hr.name = file_name(r.path);
    double rp = store_.resume_point(r.path);
    if (rp > 0) { hr.right = fmt_time(rp); hr.progress = r.dur > 0 ? rp / r.dur : 0; }
    else if (r.dur > 0) hr.right = fmt_time(r.dur);
    rows.push_back(hr);
  }
  home_.eggs_seen = store_.eggs_seen;
  home_.set_rows(rows);
}

// ── subtitle style ──────────────────────────────────────────────────
// Asks the engine, once and while it is idle, which property names it understands
// (they changed across mpv versions). After this, style writes never wait.
void App::resolve_props() {
  SubStyle a, b;
  b.bg_op = 0;
  for (const SubStyle& st : {a, b})
    for (auto& ps : sub_style_props(st, SubKind::Text)) {
      if (prop_name_.count(ps.names[0])) continue;
      std::string used;
      for (auto& n : ps.names) {
        char* v = mpv_.api().get_property_string(mpv_.handle(), n.c_str());
        if (v) { mpv_.api().free(v); used = n; break; }
      }
      prop_name_[ps.names[0]] = used;
    }
  // background-box arrived in mpv 0.38; older engines get opaque-box
  std::string ver = mpv_.get_string("mpv-version");  // "mpv 0.32.0-515-g..." / "mpv v0.39.0"
  int maj = 0, min = 0;
  size_t k = ver.find_first_of("0123456789");
  if (k != std::string::npos) std::sscanf(ver.c_str() + k, "%d.%d", &maj, &min);
  has_background_box_ = maj > 0 || min >= 38;
  log("engine " + ver);
}

void App::apply_style(bool live) {
  if (!mpv_ok_) return;
  for (auto& ps : sub_style_props(play_.in.style, play_.in.kind)) {
    if (ps.names[0] == "sub-pos") continue;
    std::string key = ps.names[0];
    auto known = prop_name_.find(key);
    if (live && known != prop_name_.end()) {
      // while a slider moves: only what changed, never waiting for the engine
      if (applied_[key] == ps.value) continue;
      if (!known->second.empty()) mpv_.set_async(known->second.c_str(), ps.value);
      applied_[key] = ps.value;
      continue;
    }
    // names were resolved once at startup (resolve_props): every write is asynchronous
    if (known == prop_name_.end() || known->second.empty()) continue;
    std::string value = ps.value;
    if (key == "sub-border-style" && value == "background-box" && !has_background_box_) value = "opaque-box";
    // never resend a value the engine already has: a subtitle font write makes it reload
    // its fonts, and that froze the picture on every resize and fullscreen change
    auto it = applied_.find(key);
    if (it == applied_.end() || it->second != value) mpv_.set_async(known->second.c_str(), value);
    applied_[key] = value;
  }
  (void)live;
  std::string pos = subtitle_position_string(play_.lifted_pos());
  if (applied_["sub-pos"] != pos) mpv_.set_async("sub-pos", pos);
  applied_["sub-pos"] = pos;
}

// ── actions from the playback UI ────────────────────────────────────
void App::request_pause(bool paused) {
  if (audio_switch_.stage && audio_switch_.stage < 5) {
    audio_switch_.paused = paused; play_.in.paused = paused;
    return;
  }
  pause_target_=paused; pause_pending_=true;
  if(pause_request_ < (1ull<<32)) pause_request_=1ull<<32;
  ++pause_request_; int flag=paused;
  if(mpv_.api().set_property_async(mpv_.handle(),pause_request_,"pause",MPV_FORMAT_FLAG,&flag)<0)
    pause_pending_=false;
}

void App::run_actions() {
  auto acts = play_.take_actions();
  double t = now();
  bool live_style = false;
  for (auto& a : acts) {
    switch (a.a) {
      case PA::NextEpisode: if (!next_ep_.empty()) open(next_ep_); break;
      case PA::CancelNext: next_advance_.clear(); next_cancelled_ = true; next_count_ = -1; play_.next_notice("", t); break;
      case PA::TogglePause:
        if (eof_) {  // play at the end: from the beginning
          eof_ = false;
          play_.in.ended = false;
          mpv_.cmd({"seek", "0", "absolute"});
          request_pause(false);
        } else {
          request_pause(a.v>0);
        }
        break;
      case PA::SeekRel: cancel_audio_switch(); mpv_.cmd({"seek", std::to_string(a.v), "relative"}); break;
      case PA::SeekAbs:
        cancel_audio_switch();
        if (seek_in_flight_) seek_pending_ = a.v;  // one seek in flight: scrubbing never queues up
        else { seek_in_flight_ = true; mpv_.cmd({"seek", std::to_string(a.v), "absolute+keyframes"}, kReplySeek); }
        break;
      case PA::SeekAbsExact: cancel_audio_switch(); seek_pending_ = -1; mpv_.cmd({"seek", std::to_string(a.v), "absolute+exact"}); break;
      case PA::Volume: mpv_.set_double_async("volume", a.v); play_.in.volume = a.v; break;
      case PA::ToggleMute: mpv_.cmd({"cycle", "mute"}); break;
      case PA::Fullscreen: toggle_fullscreen(); break;
      case PA::SelectSub:
        if (a.id == 0) mpv_.set_async("sid", "no");
        else {
          // re-selecting the current track would reload it and lose the line on screen
          if (a.id != play_.in.sid) mpv_.set_async("sid", std::to_string(a.id));
          mpv_.set_flag_async("sub-visibility", true);
        }
        break;
      case PA::SelectAudio: select_audio(a.id); play_.announce_audio(t); break;
      case PA::AudioMix: {
        // a.id: the chosen tracks as a mask of their places in the list (0: every track)
        std::vector<int64_t> ids;
        uint64_t mask = (uint64_t)a.id;
        for (size_t i = 0; i < play_.in.audios.size() && i < 64; ++i)
          if (!mask || (mask >> i & 1)) ids.push_back(play_.in.audios[i].id);
        set_audio_mix(ids, true);
        play_.announce_audio(t);
        play_.changed(t);
        break;
      }
      case PA::AddSubFile: {
        auto f = open_file_dialog(hwnd_, DialogKind::Subtitle);
        for (auto& s : f) mpv_.cmd({"sub-add", s, "select"});
        break;
      }
      case PA::OpenFile: { auto f = open_file_dialog(hwnd_, DialogKind::Video); if (!f.empty()) handle_files(f); break; }
      case PA::CloseVideo: release_video_window(); save_position(); mpv_.cmd({"stop"}); break;
      case PA::StyleLive: live_style = true; break;  // coalesced: applied once per frame
      case PA::Style:
        apply_style();
        store_.set_style_for(path_, play_.in.style);
        save_state();
        break;
      case PA::StyleDefault:
        store_.default_style = play_.in.style;
        play_.in.default_style = play_.in.style;
        save_state();
        play_.changed(t);
        break;
      case PA::SubDelay: {
        double v = play_.in.sub_delay + a.v;
        mpv_.set_double_async("sub-delay", v);
        play_.in.sub_delay = v;
        play_.toast(i18n::format(str::toast_sub_delay, {{"delay", fmt_delay(v)}}), nullptr, t);
        break;
      }
      case PA::SubVisible:
        if (a.v > 0 && play_.in.sid == 0 && !play_.in.subs.empty()) mpv_.set_async("sid", std::to_string(play_.in.subs[0].id));
        mpv_.set_flag_async("sub-visibility", a.v > 0);
        if (!play_.in.subs.empty() && !play_.panel_open()) play_.toast(tr(a.v > 0 ? str::toast_sub_on : str::toast_sub_off), nullptr, t);
        break;
      case PA::Placement: store_.dock = play_.place; save_state(); break;
      case PA::WinMin: ShowWindow(hwnd_, SW_MINIMIZE); break;
      case PA::WinPin: toggle_pin(); break;
      case PA::WinMax: toggle_maximize(); break;
      case PA::WinClose: PostMessageW(hwnd_, WM_CLOSE, 0, 0); break;
      case PA::WinDragBegin: begin_window_drag(); break;
      case PA::WinDragEnd: break;
      case PA::Preview: if (!preview_.failed) thumb_request(a.v); break;
      case PA::Speed:
        mpv_.set_double_async("speed", a.v);  // pitch stays natural (audio-pitch-correction)
        play_.in.speed = a.v;
        break;
      case PA::SaveFrame: save_frame(a.v > 0); break;
      case PA::OpenFrameFolder: if (!last_frame_.empty()) show_in_folder(last_frame_); break;
      case PA::FileInfo: refresh_file_info(); break;
      case PA::OpenLicenses: open_licenses(hwnd_, exe_dir()); break;
      case PA::OpenLinkedIn:
      case PA::OpenGitHub: {
        const wchar_t* url = a.a == PA::OpenLinkedIn ? L"https://www.linkedin.com/in/gabrielbenzoni" : L"https://github.com/tinypeep";
        if ((INT_PTR)ShellExecuteW(hwnd_, L"open", url, nullptr, nullptr, SW_SHOWNORMAL) <= 32)
          play_.toast(tr(str::toast_link_failed), nullptr, t);
        break;
      }
      case PA::Loop:
        // repeat the file forever (a music video, a loop); stays on for the next files too
        store_.loop = a.v > 0;
        play_.in.loop = store_.loop;
        mpv_.set_async("loop-file", store_.loop ? "inf" : "no");
        if (store_.loop && eof_) {  // switched on at the end: it starts over right away
          eof_ = false;
          play_.in.ended = false;
          mpv_.cmd({"seek", "0", "absolute"});
          request_pause(false);
        }
        save_state(); play_.changed(now()); ensure_anim();
        break;
      case PA::SubEdit:
        if (a.v > 0) {
          // placing the subtitle: the film holds still on this line, and loses its colour
          sub_edit_resume_ = !play_.in.paused && !eof_;
          if (sub_edit_resume_) request_pause(true);
          auto [x, y] = sub_center_out();
          subfx_ = {true, t, -1, x, y, x, y};
          sub_lift_.target = 1.045;  // picked up
        } else if (subfx_.on) {
          if (sub_edit_resume_) request_pause(false);
          sub_edit_resume_ = false;
          auto [x, y] = sub_center_out();
          subfx_.t_out = t; subfx_.bx = x; subfx_.by = y;
          sub_lift_.target = 1.0;  // put down
        }
        if (reduce_motion()) {
          if (subfx_.t_in >= 0) subfx_.t_in = t - 10;
          if (subfx_.t_out >= 0) subfx_.t_out = t - 10;
          sub_lift_.snap(1.0);
        }
        step_sub_fx(t, 0);
        ensure_anim();
        break;
      case PA::ControlsFree:
        store_.controls_free = a.v > 0;
        play_.in.controls_free = store_.controls_free;
        save_state(); play_.changed(now()); play_.activity(now()); ensure_anim();
        break;
      case PA::UiScale: set_ui_scale((int)a.v); break;
      case PA::Language: set_language_pref((int)a.v); break;
      case PA::Remote: open_remote_scene(); break;
      case PA::Update:
        if (upd_ == Upd::Available) start_update();
        else check_updates(true);
        break;
      case PA::AmbientStrength:
        store_.ambient_strength = std::clamp(a.v, 0.05, 1.0);
        play_.in.ambient_strength = store_.ambient_strength;
        save_state();  // debounced: dragging the slider writes once at the end
        if (ambient_.active()) {
          // applied right away (only the opacity changes, no repaint)
          double tn = now(), adt = std::clamp(tn - atmo_t_, 0.0, 0.1);
          atmo_t_ = tn;
          step_atmosphere(adt);
        }
        break;
      case PA::Ambient:
        store_.ambient = a.v > 0;
        play_.in.ambient = store_.ambient;
        save_state();
        play_.changed(t);
        break;
      case PA::SubLift: {
        std::string pos = subtitle_position_string(a.v);
        if (applied_["sub-pos"] != pos) { mpv_.set_async("sub-pos", pos); applied_["sub-pos"] = pos; }
        break;
      }
    }
  }
  if (live_style) apply_style(true);
}

// ── overlays: the playback UI is composited by mpv itself ──────────
void App::update_blur_cloud() {
  if (!mpv_ok_) return;
  auto cloud = play_.blur_cloud();
  if (mode_ != Mode::Play || audio_only_ || !first_frame_ || vid_w_ <= 0 || vid_h_ <= 0)
    cloud.alpha = 0;
  double tn = now();

  auto u = cloud_uniforms(cloud, cw_, ch_, vid_w_, vid_h_, vid_rot_, dpi_);
  char b[512];
  std::snprintf(b, sizeof b,
    "cloud_alpha=%.4f,cloud_x=%.6f,cloud_y=%.6f,cloud_rx=%.6f,cloud_ry=%.6f,cloud_sx=%.6f,cloud_sy=%.6f",
    cloud.alpha, u.x,u.y,u.rx,u.ry,u.sx,u.sy);
  // Ignore geometry while inactive: no redraws or option churn in clean playback.
  std::string options = cloud.alpha > 0 ? b : "cloud_alpha=0";
  bool fx = snap_.on || subfx_.on;
  if (snap_.on) {
    char f[200];
    std::snprintf(f, sizeof f, ",cue_on=1,cue_dark=%.4f,cue_px=%.1f,cue_gray=%.4f,cue_ar=10,cue_br=0", snap_.dark, snap_.px, snap_.gray);
    options += f;
  } else if (fx) {
    // subtitle edit: grey and a little darker (so the line being placed stands out), spreading
    // from the subtitle in blocks of 8 interface pixels, and the colour coming back the same way
    auto ring = [](double t0, double dur, double tn) {
      if (t0 < 0) return 0.0;
      double x = std::clamp((tn - t0) / dur, 0.0, 1.0);
      return 1 - (1 - x) * (1 - x);
    };
    double ar = ring(subfx_.t_in, .42, tn), br = ring(subfx_.t_out, .45, tn);
    char f[360];
    std::snprintf(f, sizeof f,
      ",cue_on=1,cue_dark=0.32,cue_px=1.0,cue_gray=1,cue_cell=%.0f,cue_ax=%.5f,cue_ay=%.5f,cue_ar=%.4f,cue_bx=%.5f,cue_by=%.5f,cue_br=%.4f",
      std::max(4.0, std::round(8 * play_.in.s)), subfx_.ax, subfx_.ay, ar, subfx_.bx, subfx_.by, br);
    options += f;
  } else if (snap_.was_on) {
    options += ",cue_on=0";
  }
  cloud_pending_ = false;
  if (options == cloud_options_) return;
  if ((cloud.alpha > 0 || fx) && !cloud_cadence_.ready(tn, .0015)) { cloud_pending_ = true; return; }
  snap_.was_on = fx;
  cloud_cadence_.mark(tn);
  cloud_options_ = options;
  mpv_.set_async("glsl-shader-opts", options);
}

void App::upload_overlays() {
  if (!mpv_ok_) return;
  update_blur_cloud();
  for (int i = 0; i < PlayView::L_COUNT; ++i) {
    Layer& L = play_.layers[i];
    if (!L.dirty) continue;
    Ov& o = ov_[i];
    if (o.inflight || o.removing) continue;  // preserve the renderer's lease until its acknowledgement
    // at most 60 pictures a second per layer: the engine redraws its OSD for each one, and a
    // tooltip gliding at 120 Hz over a 4K film was enough to make the video hitch
    double tn = now();
    if (L.visible && o.shown && !o.cadence.ready(tn, .0015)) continue;
    o.cadence.mark(tn);
    L.dirty = false;
    if (L.visible && L.c.w > 0 && L.c.h > 0) {
      int b = o.on_screen == 0 ? 1 : 0;  // never the buffer the engine is showing
      o.buf[b] = L.c.px;
      int x = std::max(0, L.x), y = std::max(0, L.y);
      o.inflight = true;
      o.sending = b;
      int r = mpv_.cmd({"overlay-add", std::to_string(i), std::to_string(x), std::to_string(y),
                        "&" + std::to_string((uintptr_t)o.buf[b].data()), "0", "bgra", std::to_string(L.c.w),
                        std::to_string(L.c.h), std::to_string(L.c.w * 4)},
                       kReplyOverlay + i);
      if (r < 0) { o.inflight = false; o.sending = -1; }
      o.shown = true;
    } else if (o.shown) {
      o.removing = mpv_.cmd({"overlay-remove", std::to_string(i)}, kReplyOverlayRemove + i) >= 0;
      if (!o.removing) L.dirty = true;
    }
  }
}

bool App::overlays_pending() const {
  if (cloud_pending_) return true;
  for (int i = 0; i < PlayView::L_COUNT; ++i)
    if (play_.layers[i].dirty && !ov_[i].inflight && !ov_[i].removing) return true;
  return false;
}

void App::remove_overlays() {
  for (int i = 0; i < PlayView::L_COUNT; ++i) {
    play_.layers[i].visible = false;
    play_.layers[i].dirty = true;
  }
  upload_overlays();
}

// ── frames and timers ───────────────────────────────────────────────
void App::refresh_display_rate() {
  pacer_.refresh_interval(hwnd_);
  double iv = pacer_.interval();
  // The engine redraws its OSD for every overlay picture: uploads stay near 60 Hz, on a whole
  // divisor of the display's rate so they still land on its frames (144 Hz -> 72, 120 -> 60).
  int k = std::max(1, (int)std::ceil(1.0 / iv / 90.0 - 0.05));
  play_interval_ = iv * k;
  for (auto& o : ov_) o.cadence.period = play_interval_;
  cloud_cadence_.period = play_interval_;
}

void App::ensure_anim() {
  if (anim_on_) return;
  anim_on_ = true;
  // fine timer resolution only while something animates (it costs power system-wide);
  // display-locked ticks do not need it at all
  KillTimer(hwnd_, T_FINE);
  if (!pacer_.ok() && !fine_timer_) { timeBeginPeriod(1); fine_timer_ = true; }
  last_tick_ = now();
  home_paint_at_ = -1; // a new input gets an immediate paint
  KillTimer(hwnd_, T_WAKE);
  animation_cadence_.reset();
  frame();
}

void App::arm_animation_timer() {
  if (pacer_.ok()) pacer_.arm();
  else SetTimer(hwnd_, T_ANIM, animation_cadence_.wait_ms(now()), nullptr);
}

void App::frame() {
  if (IsIconic(hwnd_)) {
    // Playback continues in mpv; a minimized interface has no pixels to animate.
    anim_on_ = false;
    KillTimer(hwnd_, T_ANIM);
    KillTimer(hwnd_, T_WAKE);
    if (fine_timer_) { timeEndPeriod(1); fine_timer_ = false; }
    return;
  }
  double t = now();
  // home: every display frame; playback: the overlay rhythm (uploads cannot go faster anyway)
  animation_cadence_.period = (mode_ == Mode::Home || fullscreen_transition_) && pacer_.ok() ? pacer_.interval() : play_interval_;
  // display ticks arrive a hair early or late: half a frame of slack keeps every one of them
  double slack = pacer_.ok() ? pacer_.interval() * 0.45 : 0.0;
  if (!animation_cadence_.ready(t, slack)) { if (pacer_.ok()) pacer_.arm(); return; }
  animation_cadence_.mark(t);
  double dt = std::min(0.05, std::max(0.0, t - last_tick_));
  last_tick_ = t;
  bool moving = geom_.on;
  moving |= step_fullscreen_transition(t);
  moving |= step_audio_switch(t);
  moving |= step_sub_fx(t, dt);
  step_geom(t);
  if (mode_ == Mode::Home && onboarding_) {
    moving = ob_.tick(t, dt);
    if (ob_.finished(t)) {
      onboarding_ = false;
      home_.enter(t, ob_.face_handoff());  // the eyes already flew to their place (from the first screen)
      moving = true;
    }
  }
  if (mode_ == Mode::Home && onboarding_) {
    double raster = pacer_.ok() ? pacer_.interval() : 1.0 / 60;
    if ((double)cw_ * ch_ > 4.2e6) raster = std::max(raster, 1.0 / 60);
    if (!moving || home_paint_at_ < 0 || t - home_paint_at_ >= raster * 0.55) {
      present_home(t);
      home_paint_at_ = t;
    }
  } else if (mode_ == Mode::Home) {
    moving |= home_.tick(t, dt);
    // Rasterize once per display frame; very large canvases (4K and up) stay at 60 Hz,
    // where a full software frame plus its copy to the screen would cost more than it shows.
    double raster = pacer_.ok() ? pacer_.interval() : 1.0 / 60;
    if ((double)cw_ * ch_ > 4.2e6) raster = std::max(raster, 1.0 / 60);
    if (!moving || home_paint_at_ < 0 || t - home_paint_at_ >= raster * 0.55) {
      present_home(t);
      home_paint_at_ = t;
    }
    if (reveal_pending_) { moving = true; if (home_.leave_done(t)) reveal_play(); }
  } else {
    if (play_.iris_waiting()) { try_start_iris(); moving = true; }
    if (play_.dragging() && !play_.win_dragging()) {
      POINT p = cursor_client();
      if (p.x != last_cursor_.x || p.y != last_cursor_.y) { last_cursor_ = p; play_.mouse_move(p.x, p.y, t); }
    }
    moving |= play_.tick(t, dt);
    run_actions();
    upload_overlays();
    moving |= overlays_pending();  // a held-back picture still has to go out
  }
  if (!moving) {
    anim_on_ = false;
    KillTimer(hwnd_, T_ANIM);
    if (fine_timer_) SetTimer(hwnd_, T_FINE, 1500, nullptr);  // released after a quiet moment, not every burst
    schedule_wake();
  } else arm_animation_timer();
}

// Draws the next home (or welcome) frame off screen, then hands Windows only the rectangle
// that actually changed: a blink repaints the eyes, not 1280 x 720 pixels. The copy to the
// screen and the compositor's work shrink with it. Shown right after the display's frame.
void App::present_home(double t) {
  if (onboarding_) ob_.render(home_back_, t);
  else home_.render(home_back_, t);
  const Canvas& a = home_back_;
  const Canvas& b = home_canvas_;
  RECT dirty{0, 0, a.w, a.h};
  if (a.w == b.w && a.h == b.h && !b.px.empty()) {
    int y0 = -1, y1 = -1;
    const size_t row = (size_t)a.w;
    for (int y = 0; y < a.h; ++y)
      if (std::memcmp(&a.px[y * row], &b.px[y * row], row * 4) != 0) { if (y0 < 0) y0 = y; y1 = y; }
    if (y0 < 0) { std::swap(home_canvas_, home_back_); return; }  // nothing on screen changed
    int x0 = a.w, x1 = -1;
    for (int y = y0; y <= y1; ++y) {
      const uint32_t* ra = &a.px[y * row];
      const uint32_t* rb = &b.px[y * row];
      int l = 0;
      while (l < x0 && ra[l] == rb[l]) ++l;
      if (l < x0) x0 = l;
      int r = a.w - 1;
      while (r > x1 && ra[r] == rb[r]) --r;
      if (r > x1) x1 = r;
    }
    if (x1 < x0) { std::swap(home_canvas_, home_back_); return; }
    dirty = {x0, y0, x1 + 1, y1 + 1};
  }
  std::swap(home_canvas_, home_back_);
  InvalidateRect(hwnd_, &dirty, FALSE);
  UpdateWindow(hwnd_);
}

void App::schedule_wake() {
  if (mode_ != Mode::Home) return;
  double d = onboarding_ ? ob_.next_event(now()) : home_.next_event(now());
  UINT ms = (UINT)std::clamp(d * 1000.0, 10.0, 60000.0);
  SetTimer(hwnd_, T_WAKE, ms, nullptr);
}

POINT App::cursor_client() const {
  POINT p;
  GetCursorPos(&p);
  ScreenToClient(hwnd_, &p);
  return p;
}

void App::mouse_button(int which, bool down, int x, int y) {
  if (mode_ != Mode::Play || closing_video_) return;
  double t = now();
  if (which == 1) play_.right_click(x, y, t);
  else if (down) play_.mouse_down(x, y, t);
  else play_.mouse_up(x, y, t);
  run_actions();
  ensure_anim();
}

void App::key(const std::string& k, bool shift, bool ctrl) {
  double t = now();
  if (scene_.visible()) { if (k == "esc") scene_.close(t); return; }
  if (k == "esc" && next_count_ >= 0 && !next_cancelled_) {
    next_advance_.clear(); next_cancelled_ = true; next_count_ = -1; play_.next_notice("", t); ensure_anim(); return;
  }
  if (ctrl && k == "o") {
    auto f = open_file_dialog(hwnd_, DialogKind::Video);
    if (!f.empty()) handle_files(f);
    return;
  }
  if (ctrl && k == "v") { handle_files(clipboard_files(hwnd_)); return; }
  if (ctrl && k == "t") { toggle_pin(); return; }  // always on top, like Windows' own Win+Ctrl+T
  // interface size, anywhere: ctrl + / ctrl - / ctrl 0
  if (ctrl && (k == "plus" || k == "minus" || k == "zero")) {
    set_ui_scale(k == "zero" ? kDefaultUiSize : next_ui_size(store_.ui_scale, k == "plus" ? 1 : -1));
    say_ui_size();
    return;
  }
  if (mode_ == Mode::Home && onboarding_) {
    on_onboarding(ob_.key(k, shift, t));
    ensure_anim();
    return;
  }
  if (mode_ == Mode::Home) {
    HomeAction a = home_.key(k, shift, t);
    if (a.a == HomeAct::OpenDialog) { auto f = open_file_dialog(hwnd_, DialogKind::Video); if (!f.empty()) handle_files(f); }
    if (a.a == HomeAct::OpenRecent) {
      if (a.index >= 0 && a.index < (int)shown_paths_.size()) open(shown_paths_[a.index]);
    }
    if (a.a == HomeAct::ClearRecents) { store_.clear_recents(); save_state(); refresh_recents(); }
    if (a.a == HomeAct::Update) start_update();
    if (a.a == HomeAct::Language) set_language_pref(a.index);
    if (a.a == HomeAct::Remote) open_remote_scene();
    if (a.a == HomeAct::News) show_news(true);
    if (a.a == HomeAct::NewsClose) close_news_line();
    ensure_anim();
    return;
  }
  play_.key(k, shift, ctrl, t);
  run_actions();
  ensure_anim();
}

void App::toggle_fullscreen() {
  if (fullscreen_transition_ || audio_switch_.stage || mode_ != Mode::Play) return;
  begin_fullscreen_motion();
}

// ── fullscreen: the snap ────────────────────────────────────────────
// Everything happens inside the video's own shader, in the same picture the engine
// presents, so two images can never be on screen together (the old compositor copy and the
// real window disagreed for a frame at every hand-over: the flash).
//   out  (110 ms): the picture breaks into ever bigger pixels, loses its colour, goes dark
//   cut           the window changes size while the picture is black (nothing to stretch)
//   in   (200 ms): from black, the pixels halve 32 → 16 → 8 → 4 → 2 → 1 and snap into
//                  focus like a magnet, the colour comes back a beat later
// Pixel steps are the Cue's own language (the opening iris, the eyes, the type), and they
// read the same at 24, 60 or 144 frames per second.
namespace {
constexpr double kSnapOut = 0.11, kSnapIn = 0.20, kSnapHold = 0.045;
// physical pixel sizes for the mosaic at 1080 lines; scaled to the window's height
constexpr double kSnapSteps[] = {1, 2, 4, 8, 16, 32};
double snap_px(double amount, int height) {  // 0 = sharp, 1 = biggest blocks
  int i = std::clamp((int)std::floor(amount * 6.0), 0, 5);
  double k = std::max(1.0, height / 1080.0);
  return i == 0 ? 1.0 : std::round(kSnapSteps[i] * k);
}
}  // namespace

double App::video_aspect() const {
  double w = (double)vid_w_, h = (double)vid_h_;
  if (vid_rot_ % 180 == 90) std::swap(w, h);
  return w > 0 && h > 0 ? w / h : 0;
}

std::pair<double, double> App::sub_center_out() const {
  Rect r = play_.subtitle_rect();
  if (r.w <= 0 || vid_w_ <= 0 || vid_h_ <= 0) return {.5, .8};
  return output_point(r.x + r.w / 2.0, r.y + r.h / 2.0, cw_, ch_, vid_w_, vid_h_, vid_rot_);
}

// Subtitle edit: advances the rings and the lift; true while something still moves.
bool App::step_sub_fx(double t, double dt) {
  bool moving = false;
  if (subfx_.on) {
    moving = true;
    if (subfx_.t_out >= 0 && t - subfx_.t_out >= .45) subfx_.on = false;  // the colour is all back
    update_blur_cloud();
  } else if (snap_.was_on && !snap_.on) {
    update_blur_cloud();  // the last word: the effect off
  }
  if (dt > 0) moving |= sub_lift_.step(dt);
  // the lift moves in steps of 1.5%: every new size makes libass rasterise the line again,
  // so a handful of sizes (cached after the first time) instead of one per frame
  double q = 1.0 + std::round((sub_lift_.x - 1.0) / 0.015) * 0.015;
  char b[16];
  std::snprintf(b, sizeof b, "%.3f", q);
  if (mpv_ok_ && sub_scale_sent_ != b) { sub_scale_sent_ = b; mpv_.set_async("sub-scale", b); }
  return moving;
}

void App::set_snap(double dark, double px, double gray) {
  snap_.on = true;
  snap_.dark = std::clamp(dark, 0.0, 1.0);
  snap_.px = px;
  snap_.gray = std::clamp(gray, 0.0, 1.0);
  update_blur_cloud();
}

void App::snap_subtitles(bool hide) {
  if (hide == snap_.subs_hidden || !mpv_ok_) return;
  if (hide) {
    snap_.subs_were = play_.in.sub_visible;
    snap_.subs_hidden = true;
    if (snap_.subs_were) mpv_.set_flag_async("sub-visibility", false);
  } else {
    snap_.subs_hidden = false;
    if (snap_.subs_were) mpv_.set_flag_async("sub-visibility", true);
  }
}

void App::begin_fullscreen_motion() {
  // Entering fullscreen must not discard a new film's pending/following shape.
  if(!user_sized_ && (!geom_done_ || geom_.on) && video_aspect()>0) {
    pending_ar_=video_aspect();
    geom_done_=true;
  }
  geom_.on=false;
  KillTimer(hwnd_,T_GEOM);
  fullscreen_transition_=true;
  play_.begin_viewport_transition(now(),true);  // the controls step aside on this very frame
  fz_=FullscreenZoom{};
  fz_.entering=!fullscreen_;
  fz_.t0=now();
  if(reduce_motion() || !mpv_ok_ || audio_only_) {
    perform_fullscreen_switch();
    fullscreen_changed_at_=now();
    fullscreen_stage_=9;
  } else {
    snap_subtitles(true);  // subtitles are drawn after the shader: they step out with the controls
    fullscreen_stage_=1;
  }
  ensure_anim();
}

void App::settle_fullscreen_motion() {
  if(!fullscreen_transition_) return;
  if(fz_.entering!=fullscreen_) perform_fullscreen_switch();
  end_snap();
  fullscreen_transition_=false; fullscreen_stage_=0;
  play_.end_viewport_transition(now());
}

void App::end_snap() {
  snap_subtitles(false);
  if (snap_.on) { snap_.on = false; update_blur_cloud(); }
}

bool App::step_fullscreen_transition(double t) {
  if(!fullscreen_transition_) return false;
  if(fullscreen_stage_!=logged_fs_stage_) {
    logged_fs_stage_=fullscreen_stage_;
    log("fullscreen snap stage "+std::to_string(fullscreen_stage_));
  }
  const double frame=pacer_.ok()?pacer_.interval():1./60;
  auto go=[&](int stage) { fullscreen_stage_=stage; fz_.t0=t; };
  auto renderer_ready=[&] {
    RECT child{};
    bool native_ready=!mpv_win_ || (GetClientRect(mpv_win_,&child) && child.right==cw_ && child.bottom==ch_);
    if(geom_.on || !viewport_ready_for_controls(cw_,ch_,osd_w_,osd_h_,native_ready,t-fullscreen_changed_at_,0)) {
      fz_.ready_at=-1; return false;
    }
    if(fz_.ready_at<0) fz_.ready_at=t;
    return t-fz_.ready_at>=2*frame;  // a couple of frames drawn at the new size, still black
  };
  auto done=[&] {
    end_snap();
    fullscreen_transition_=false; fullscreen_stage_=0; logged_fs_stage_=-1;
    play_.end_viewport_transition(t);  // the controls rise in on their spring
    log("fullscreen snap done");
  };
  switch(fullscreen_stage_) {
    case 1: {  // out: pixels grow, colour goes, light goes (accelerating: pulled in)
      double k=clamp01((t-fz_.t0)/kSnapOut);
      double e=k*k;
      set_snap(e, snap_px(k, ch_), std::min(1.0, k*1.6));
      if(k>=1) { set_snap(1.0, snap_px(1, ch_), 1.0); go(2); }
      break;
    }
    case 2: {  // the black picture is presented, our old overlays are gone: cut
      bool overlays=false;
      for(const auto& o:ov_) overlays|=o.shown||o.inflight||o.removing;
      if(overlays || t-fz_.t0<kSnapHold) break;
      perform_fullscreen_switch();
      fullscreen_changed_at_=t; fz_.ready_at=-1;
      set_snap(1.0, snap_px(1, ch_), 1.0);  // the new size gets the same black, biggest blocks
      go(3);
      break;
    }
    case 3:  // the engine draws at the new size (black): then it may come back
      if(renderer_ready()) go(4);
      break;
    case 4: {  // in: light first, then the pixels halve and snap into focus, colour last
      double k=clamp01((t-fz_.t0)/kSnapIn);
      double light=1-ease_out(clamp01(k*1.35));
      double blocks=1-ease_out(clamp01((k-0.08)/0.85));
      double gray=1-smoothstep(0.45,1.0,k);
      set_snap(light, snap_px(blocks, ch_), gray);
      if(k>=0.55) snap_subtitles(false);
      if(k>=1) done();
      break;
    }
    case 9:
      if(renderer_ready()) done();
      break;
  }
  return fullscreen_transition_;
}

void App::perform_fullscreen_switch() {
  geom_.on = false;
  KillTimer(hwnd_, T_GEOM);
  if (!fullscreen_) {
    GetWindowPlacement(hwnd_, &wp_);
    saved_style_ = GetWindowLongPtrW(hwnd_, GWL_STYLE);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &mi);
    // Set state before SetWindowPos sends reentrant sizing/non-client messages.
    fullscreen_ = play_.in.fullscreen = true;
    SetWindowLongPtrW(hwnd_, GWL_STYLE, (saved_style_ & ~(WS_OVERLAPPEDWINDOW | WS_MAXIMIZE | WS_MINIMIZE)) | WS_POPUP);
    SetWindowPos(hwnd_, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                 mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
  } else {
    fullscreen_ = play_.in.fullscreen = false;
    SetWindowLongPtrW(hwnd_, GWL_STYLE, saved_style_);
    SetWindowPlacement(hwnd_, &wp_);
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);
    if (pending_ar_ > 0) { double ar = pending_ar_; pending_ar_ = 0; adapt_window(ar); }  // a file of another shape opened meanwhile
  }
  RECT client{}; GetClientRect(hwnd_, &client);
  SendMessageW(hwnd_, WM_SIZE, IsZoomed(hwnd_) ? SIZE_MAXIMIZED : SIZE_RESTORED, MAKELPARAM(client.right, client.bottom));
  RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_FRAME);
  play_.in.fullscreen = fullscreen_;
  apply_window_corners();
  play_.changed(now());

}

void App::set_title(const std::string& t) { SetWindowTextW(hwnd_, widen(t == "Cue" ? "Cue" : t + " · Cue").c_str()); }

// ── timeline previews ───────────────────────────────────────────────
// Decoded by the helper process (thumbhost.cpp): its own engine at background priority, CPU only.
// Each frame is a keyframe seek and a small picture handed back through a pipe. Frames are
// cached per slice only when requested. No speculative full-timeline scan.
double App::thumb_step() const { return std::max(1.0, play_.in.dur / 300.0); }
int App::thumb_key(double t) const { return (int)std::floor(std::max(0.0, t) / thumb_step()); }

void App::thumb_request(double t) {
  if (audio_only_ || !mpv_ok_ || path_.empty() || preview_.failed) return;
  if (t >= 0) {
    thumb_want_ = t;
    // instant answer: this slice, or the nearest one already decoded while the exact one comes
    if (background_ready() && !thumb_cache_.empty()) {
      int k = thumb_key(t);
      auto it = thumb_cache_.lower_bound(k);
      if (it == thumb_cache_.end() || (it != thumb_cache_.begin() && it->first != k &&
                                       k - std::prev(it)->first < it->first - k)) --it;
      thumb_cache_.touch(it->first);
      if (it->first != thumb_shown_) {
        thumb_shown_ = it->first;
        play_.set_thumb(it->second.px, it->second.w, it->second.h, it->first * thumb_step());
        ensure_anim();
      }
      if (it->first == k) thumb_want_ = -1;
    }
  }
  // Timeline hover waits 3 s; the shared helper may start earlier for colours.
  // Keep the requested time: thumb_next serves it once timeline work is allowed.
  if (t >= 0 ? !background_ready() : !atmosphere_ready()) return;
  if (!thumbs_.running() && !thumbs_.start(hwnd_, WM_APP_THUMB)) { preview_.failed = true; return; }
  if (thumb_path_ != path_) {
    // the helper opens this file once; it answers Ready (or Failed) when it can decode it
    thumbs_.open(path_, play_.thumb_decode_width(), preview_.generation);
    thumb_path_ = path_;
    preview_.loaded = false;
    preview_.busy = true;
    return;
  }
  thumb_next();
}

// When free, serve an explicit hover/phone request or the current scene colour.
// Speculative distant seeks can saturate a HDD or network share; there is no background fill.
void App::thumb_next() {
  if (in_sizemove_ || !atmosphere_ready() || !preview_.loaded || preview_.busy || play_.in.dur <= 0) return;
  double t = -1;
  thumb_for_accent_ = false;
  if (background_ready() && thumb_want_ >= 0 && !thumb_cache_.count(thumb_key(thumb_want_))) t = thumb_want_;
  if (t < 0 && accent_want_ >= 0) {
    // Timeline buckets can span tens of seconds in a long film. Its cached
    // thumbnail is not the colour of the current scene: sample the requested time.
    t = accent_want_;
    accent_want_ = -1;
    thumb_for_accent_ = true;
  }
  if (t < 0) return;
  preview_.busy = true;
  thumb_seek_t_ = t;
  thumbs_.shot(++preview_.request, std::max(0.0, t), thumb_for_accent_ ? 0 : play_.thumb_hover_width());
}

void App::thumb_store(std::vector<uint32_t>&& px, int w, int h) {
  if (w <= 0 || h <= 0 || px.size() < (size_t)w * h) return;
  ThumbImg img;
  img.w = w; img.h = h;
  img.px = std::move(px);
  int k = thumb_key(thumb_seek_t_);
  if (thumb_for_accent_) {
    sample_atmosphere(img.px, img.w, img.h);
    return; // colour probes are transient; do not populate the hover cache every 0.9 seconds
  }
  // Colour probes already returned above at full sampling resolution. Resize only UI previews.
  const auto size = preview_size(img.w, img.h, play_.thumb_hover_width());
  if (size.w != img.w) {
    std::vector<uint32_t> small((size_t)size.w * size.h);
    for (int y = 0; y < size.h; ++y)
      for (int x = 0; x < size.w; ++x)
        small[(size_t)y * size.w + x] = img.px[(size_t)(y * img.h / size.h) * img.w + x * img.w / size.w];
    img.w = size.w; img.h = size.h; img.px = std::move(small);
  }
  if (!thumb_cache_.put(k, std::move(img))) {
    // A rejected oversized picture must not retry forever and churn the disk.
    if (thumb_want_ >= 0 && thumb_key(thumb_want_) == k) thumb_want_ = -1;
    if (remote_peek_want_ >= 0 && thumb_key(remote_peek_want_) == k) remote_peek_want_ = -1;
    return;
  }
  if (remote_peek_want_ >= 0) remote_peek_send();
  // the frame the cursor is waiting for: show it now
  if (background_ready() && thumb_want_ >= 0 && thumb_key(thumb_want_) == k) {
    thumb_want_ = -1;
    thumb_shown_ = k;
    thumb_cache_.touch(k);
    auto& c = thumb_cache_.find(k)->second;
    play_.set_thumb(c.px, c.w, c.h, k * thumb_step());
    ensure_anim();
  }
}

// One answer from the preview helper (it owns the decoding; we only keep the pictures).
void App::on_thumb(ThumbMsg* m) {
  if (m->process != thumbs_.epoch()) return;
  if (!preview_.accepts(m->session) && !(m->type == ThumbMsg::Gone && m->session == 0 && !thumb_path_.empty())) return;
  switch (m->type) {
    case ThumbMsg::Ready: preview_.loaded = true; preview_.busy = false; break;
    case ThumbMsg::Failed: preview_.loaded = false; preview_.busy = false; break;  // this file: no previews
    case ThumbMsg::Frame:
      if (preview_.accepts_frame(m->session, m->id)) {
        preview_.busy = false;
        preview_.misses = 0;
        thumb_store(std::move(m->px), m->w, m->h);
      }
      break;
    case ThumbMsg::NoFrame:
      if (preview_.accepts_frame(m->session, m->id)) { preview_.busy = false; if (++preview_.misses > 8) preview_.failed = true; }
      break;
    case ThumbMsg::Gone:
      preview_.loaded = false;
      preview_.failed = true;
      log("preview helper stopped");
      preview_.busy = false;
      break;
  }
  if (!preview_.failed && preview_.loaded && thumb_path_ == path_) thumb_next();
}

void App::thumb_stop() {
  KillTimer(hwnd_, T_THUMB);
  KillTimer(hwnd_, T_PREFETCH);
  thumb_cache_.clear();
  thumb_shown_ = INT_MIN;
  remote_peek_want_ = -1;
  remote_peek_sent_ = INT_MIN;
  preview_.reset(); // errors and replies from the old file must not disable its replacement
  play_.set_thumb({}, 0, 0, -1);
  thumb_want_ = thumb_seek_t_ = -1;
  accent_want_ = -1;
  thumb_for_accent_ = false;
  // the helper stays alive for the next file: it only lets go of this one
  if (thumbs_.running() && !thumb_path_.empty()) thumbs_.close_file();
  thumb_path_.clear();
  preview_.loaded = preview_.busy = false;
}

// ── own window chrome ───────────────────────────────────────────────
// Interface size: changes the art-pixel multiple; video, zoom and subtitle size are untouched.
// The welcome's choices apply at once, exactly like the same controls in the player.
void App::on_onboarding(ObAction a) {
  double t = now();
  switch (a.a) {
    case ObAct::None: break;
    case ObAct::Ambient:
      store_.ambient = a.v != 0;
      play_.in.ambient = store_.ambient;
      save_state();
      break;
    case ObAct::UiScale: set_ui_scale(a.v); break;
    case ObAct::Finish:
      store_.onboarded = true;
      save_state();
      home_.set_size(cw_, ch_, s_);
      ob_.finish(t, home_.face.cx, home_.face.cy, home_.face.scale);
      log("welcome done");
      break;
    case ObAct::WinMin: ShowWindow(hwnd_, SW_MINIMIZE); break;
    case ObAct::WinPin: toggle_pin(); break;
    case ObAct::WinMax: toggle_maximize(); break;
    case ObAct::WinClose: PostMessageW(hwnd_, WM_CLOSE, 0, 0); break;
  }
}

// The scale for a client area of cw x ch at the window's DPI.
double App::scale_for(int cw, int ch, int choice) const { return ui_scale(dpi_, cw, ch, choice); }

// After a move to another monitor or a display change: the same rule, applied right away.
void App::refresh_ui_scale() {
  if (cw_ <= 0 || ch_ <= 0) return;
  double ns = scale_for(cw_, ch_, store_.ui_scale);
  if (ns == s_) return;
  s_ = ns;
  log("ui scale " + std::to_string(s_));
  home_.set_size(cw_, ch_, s_);
  ob_.set_size(cw_, ch_, s_);
  play_.resize(cw_, ch_, portrait_scale(s_, cw_, ch_, store_.ui_scale));
  if (mode_ == Mode::Play) play_.changed(now());
  ensure_anim();
}

// ── interface language ──────────────────────────────────────────────
// Everything Cue draws is measured from the words at layout time, so a new language is applied
// on the spot: no restart. Windows' own surfaces (title bar menu, file dialog buttons, message
// box buttons) stay in Windows' language.
void App::apply_language(bool relayout) {
  i18n::Lang l = apply_ui_language(store_.language, windows_ui_languages());
  int pref = -1;
  i18n::Lang chosen;
  if (i18n::parse_tag(store_.language, chosen)) pref = (int)chosen;
  play_.in.language = home_.language = pref;
  log(std::string("language ") + i18n::tag(l) + (pref < 0 ? " (Windows)" : " (chosen)"));
  if (!relayout) return;
  if (engine_failed_) home_.engine_problem = engine_text(engine_err_);
  show_update_state();  // its labels are words too
  if (cw_ > 0 && ch_ > 0) {
    home_.relayout();
    ob_.set_size(cw_, ch_, s_);
    play_.resize(cw_, ch_, portrait_scale(s_, cw_, ch_, store_.ui_scale));
  }
  play_.changed(now());
  if (mode_ == Mode::Play && loaded_) refresh_file_info();
  relocalize_licenses();
  if (remote_.running()) remote_boot();
  if (scene_.visible()) scene_.view.set_size(scene_.view.w, scene_.view.h, scene_.view.s);  // new words, next frame
  if (news_host_.visible()) news_.set_size(news_.w, news_.h, news_.ui_s);
  // Explorer's name for Cue's file type follows, in the background like at start
  HANDLE th = CreateThread(nullptr, 0, [](void*) -> DWORD {
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    refresh_open_with();
    if (SUCCEEDED(com)) CoUninitialize();
    return 0;
  }, nullptr, 0, nullptr);
  if (th) CloseHandle(th);
  InvalidateRect(hwnd_, nullptr, FALSE);
  ensure_anim();
}

void App::set_language_pref(int lang) {
  std::string pref = lang >= 0 && lang < i18n::kLangCount ? i18n::tag((i18n::Lang)lang) : i18n::kAuto;
  if (pref == store_.language) return;
  store_.language = pref;
  save_state();  // a choice made by hand survives closing, reopening and updating
  apply_language(true);
}

void App::set_ui_scale(int v) {
  v = std::clamp(v, -1, 3);
  if (v == 1) v = 3;
  if (onboarding_ && v != ob_.ui_scale) ob_.set_scale_choice(v, now());  // ctrl +/- on the welcome
  store_.ui_scale = v;
  play_.in.ui_scale = v;
  save_state();
  double ns = scale_for(cw_, ch_, v);
  if (ns != s_) {
    s_ = ns;
    log("ui scale " + std::to_string(s_));
    home_.set_size(cw_, ch_, s_);
    ob_.set_size(cw_, ch_, s_);
    play_.resize(cw_, ch_, portrait_scale(s_, cw_, ch_, store_.ui_scale));
    if (mode_ == Mode::Play) play_.changed(now());
  } else {
    // same base scale, but a portrait window still follows the chosen size
    play_.resize(cw_, ch_, portrait_scale(s_, cw_, ch_, v));
    play_.changed(now());
  }
  ensure_anim();
}

// Right click > salvar frame: exactly the frame on screen, without any of the UI.
void App::save_frame(bool with_subs) {
  if (!loaded_) return;
  wchar_t* pics = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_Pictures, 0, nullptr, &pics))) return;
  std::wstring root = std::wstring(pics) + L"\\Cue", old = std::wstring(pics) + L"\\.tinyoffice Cue";
  CoTaskMemFree(pics);
  // frames saved back when the app was called Player stay together with the new ones
  if (GetFileAttributesW(root.c_str()) == INVALID_FILE_ATTRIBUTES && GetFileAttributesW(old.c_str()) != INVALID_FILE_ATTRIBUTES)
    MoveFileExW(old.c_str(), root.c_str(), 0);
  std::wstring dir = root + L"\\Frames";
  SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
  std::wstring name = widen(file_name(path_));
  size_t dot = name.find_last_of(L'.');
  if (dot != std::wstring::npos && dot > 0) name.resize(dot);
  for (auto& ch : name) if (wcschr(L"\\/:*?\"<>|", ch)) ch = L'_';
  int tsec = (int)std::max(0.0, play_.in.time);
  wchar_t stamp[32];
  swprintf(stamp, 32, L"_%02d-%02d-%02d", tsec / 3600, tsec / 60 % 60, tsec % 60);
  std::wstring base = dir + L"\\" + name + stamp, file = base + L".png";
  for (int n = 2; GetFileAttributesW(file.c_str()) != INVALID_FILE_ATTRIBUTES && n < 1000; ++n)
    file = base + L" (" + std::to_wstring(n) + L").png";
  // "subtitles" = the video with its subtitles; "video" = the bare picture. Never the OSD/overlays.
  last_frame_ = file;
  mpv_.cmd({"screenshot-to-file", narrow(file), with_subs ? "subtitles" : "video"}, kReplyShot);
}

// ── the window follows the content ──────────────────────────────────
RECT App::window_rect() const {
  RECT r;
  GetWindowRect(hwnd_, &r);
  return r;
}

void App::request_video_geometry() {
  if(!loaded_ || user_sized_ || !mpv_ok_) return;
  if(geometry_query_pending_) { geometry_query_dirty_=true; return; }
  auto request=media_geometry_.request();
  geometry_query_pending_=mpv_.api().get_property_async(mpv_.handle(),request,"video-out-params",MPV_FORMAT_NODE)>=0;
}

void App::video_geometry_reply(const mpv_event* event) {
  geometry_query_pending_=false;
  if(!loaded_ || event->error<0 || !event->data) return;
  auto* p=static_cast<const mpv_event_property*>(event->data);
  if(p->format!=MPV_FORMAT_NODE || !p->data) return;
  const auto& node=*static_cast<const mpv_node*>(p->data);
  if(node.format!=MPV_FORMAT_NODE_MAP || !node.u.list) return;
  int64_t w=0,h=0,dw=0,dh=0,rotation=0;
  for(int i=0;i<node.u.list->num;++i) {
    const char* key=node.u.list->keys[i]; const auto& value=node.u.list->values[i];
    if(value.format!=MPV_FORMAT_INT64) continue;
    if(!std::strcmp(key,"w")) w=value.u.int64;
    else if(!std::strcmp(key,"h")) h=value.u.int64;
    else if(!std::strcmp(key,"dw")) dw=value.u.int64;
    else if(!std::strcmp(key,"dh")) dh=value.u.int64;
    else if(!std::strcmp(key,"rotate")) rotation=value.u.int64;
  }
  VideoShape shape{dw>0?dw:w,dh>0?dh:h,rotation};
  double previous=video_aspect();
  if(!media_geometry_.accept(event->reply_userdata,shape)) return;
  vid_w_=shape.width; vid_h_=shape.height; vid_rot_=shape.rotation;
  if(user_sized_) return; // only respected until the next file is opened
  if(geom_done_ && std::fabs(previous-shape.aspect())<.00001) return;
  geom_done_=false;
  log("media geometry "+std::to_string(vid_w_)+"x"+std::to_string(vid_h_)+" rotate "+std::to_string(vid_rot_));
  maybe_adapt_window();
}

void App::maybe_adapt_window() {
  if (in_sizemove_ || geom_done_ || !loaded_ || vid_w_ <= 0 || vid_h_ <= 0) return;
  // Coalesce output reconfigurations after receiving a complete current-file tuple.
  SetTimer(hwnd_, T_GEOM, 60, nullptr);
}

void App::decide_window() {
  KillTimer(hwnd_, T_GEOM);
  if (in_sizemove_ || geom_done_ || !loaded_ || vid_w_ <= 0 || vid_h_ <= 0) return;
  geom_done_ = true;
  // Heavy video (more than 1440p): the engine draws only real frames. Redrawing every vsync
  // (display-resample, which makes the UI glide at the screen's rate) costs too much there.
  bool big = std::max(vid_w_, vid_h_) > 2600 || std::min(vid_w_, vid_h_) > 1600;
  std::string sync = big ? "audio" : "display-resample";
  if (sync != video_sync_) { mpv_.set_async("video-sync", sync); video_sync_ = sync; }
  if (!logged_hw_) { logged_hw_ = true; hw_log_t_ = now() + 1.5; }
  double w = (double)vid_w_, h = (double)vid_h_;
  if (vid_rot_ % 180 == 90) std::swap(w, h);  // phone videos: stored sideways, shown upright
  adapt_window(w / h);
}

// A comfortable window for this aspect ratio: never the video's own pixel size, never
// bigger than the work area. Every newly opened file gets its own aspect ratio.
void App::adapt_window(double ar) {
  if (ar <= 0) return;
  if (fullscreen_) { pending_ar_ = ar; return; }  // fullscreen just letterboxes; adapt on the way out
  if (IsIconic(hwnd_)) return;
  if (IsZoomed(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
  pending_ar_ = 0;
  RECT wr = window_rect(), cr;
  GetClientRect(hwnd_, &cr);
  int cw = cr.right, ch = cr.bottom;
  int fx = (wr.right - wr.left) - cw, fy = (wr.bottom - wr.top) - ch;  // invisible resize borders
  MONITORINFO mi{sizeof(mi)};
  GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &mi);
  RECT wa = mi.rcWork;
  int aw = wa.right - wa.left, ah = wa.bottom - wa.top;
  auto fitted = video_window_size(ar, aw-fx, ah-fy, dpi_, s_);
  int tw=fitted.w, th=fitted.h;
  auto_portrait_ = ar < 0.95;
  // grow from where the window is (its centre), kept inside the work area
  int cx = (wr.left + wr.right) / 2, cy = (wr.top + wr.bottom) / 2;
  int W = tw + fx, H = th + fy;
  int x = std::clamp(cx - W / 2, (int)wa.left - fx / 2, (int)wa.right - W + fx / 2);
  int y = std::clamp(cy - H / 2, (int)wa.top, (int)wa.bottom - H + fy);
  animate_window_to(RECT{x, y, x + W, y + H});
}

void App::animate_window_to(const RECT& r) {
  geom_.from = window_rect();
  geom_.to = r;
  geom_.t0 = now();
  geom_.on = true;
  if(fullscreen_transition_) {
    // A file opened in fullscreen gets its final window once, under the live
    // compositor stage; no second window-resize animation competes with it.
    geom_.on=false;
    SetWindowPos(hwnd_,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOOWNERZORDER);
    return;
  }
  if (reduce_motion()) geom_.dur = 0;
  else geom_.dur = 0.26;
  step_geom(geom_.t0);
  ensure_anim();
}

// A short geometry transition. Playback never waits for it: the engine keeps rendering into
// the window while it changes shape.
void App::step_geom(double t) {
  if (!geom_.on) return;
  double k = geom_.dur <= 0 ? 1.0 : std::clamp((t - geom_.t0) / geom_.dur, 0.0, 1.0);
  // fullscreen: a spring's gentle start and long landing; shape changes keep their ease-out
  double e = geom_.kind ? spring_ease(k) : 1 - std::pow(1 - k, 3);
  auto L = [&](LONG a, LONG b) { return (int)std::lround(a + (b - a) * e); };
  int x = L(geom_.from.left, geom_.to.left), y = L(geom_.from.top, geom_.to.top);
  int r = L(geom_.from.right, geom_.to.right), b = L(geom_.from.bottom, geom_.to.bottom);
  SetWindowPos(hwnd_, nullptr, x, y, r - x, b - y, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
  if (k >= 1) {
    geom_.on = false;
    geom_.kind = 0;
    refresh_ui_scale();  // normally already right (taken from the target at the start)
  }
}

// ── atmosphere: dynamic accent + ambient light ──────────────────────
bool App::background_ready(double delay) const {
  return !audio_only_ && mode_ == Mode::Play && loaded_ && first_frame_ &&
         background_warmup_.ready(now(), play_.iris_active() || geom_.on || in_sizemove_, delay);
}

bool App::atmosphere_ready() const {
  return background_ready(PlaybackWarmup::kAtmosphereDelay);
}

void App::reset_atmosphere() {
  background_warmup_.reset();
  atmosphere_sampled_ = false;
  ambient_.reset();  // no previous halo or expensive geometry rebuild during the new reveal
  accent_sm_ = ColorSmoother{};
  accent_target_ = {255, 255, 255};
  accent_k_target_ = 0;
  play_.in.accent = accent_target_;
  play_.in.accent_k = 0;
  play_.in.scene = {}; ++play_.in.scene_revision;
  accent_want_ = -1;
  accent_req_t_ = -10;
  accent_media_t_ = -1;
  atmo_t_ = now();
  for (int k = 0; k < FrameColors::kZones; ++k) {
    zone_sm_[k] = ColorSmoother{};
    zone_target_[k] = {};
    zone_light_target_[k] = 0;
  }
}

void App::sample_atmosphere(const std::vector<uint32_t>& px, int w, int h) {
  if (!atmosphere_ready()) return;
  FrameColors fc = analyze_frame(px.data(), w, h);
  if (!fc.valid) return;
  atmosphere_sampled_ = true;
  accent_target_ = fc.accent;
  accent_k_target_ = fc.strength;
  for (int k = 0; k < FrameColors::kZones; ++k) { zone_target_[k] = fc.zone[k]; zone_light_target_[k] = fc.zone_light[k]; }
  ensure_anim();
}

// Slow, continuous blending (no jump at a cut). Playback is never waited on.
bool App::step_atmosphere(double dt) {
  if (!atmosphere_ready() && !ambient_.active()) return false;
  bool moving = accent_sm_.step(accent_target_, accent_k_target_, dt, 1.4);
  play_.in.accent = accent_sm_.rgb();
  play_.in.accent_k = accent_sm_.k;
  Rgb e[FrameColors::kZones];
  double li[FrameColors::kZones];
  if (store_.ambient || ambient_.active()) {
    for (int k = 0; k < FrameColors::kZones; ++k) {
      zone_sm_[k].step(zone_target_[k], zone_light_target_[k], dt, 0.9);  // soft, never a flash at a cut
      e[k] = zone_sm_[k].rgb();
      li[k] = zone_sm_[k].k;
    }
  }
  bool redraw = false;
  if (moving) {
    // the dock redraws only when the colour moved enough to show (its signature quantizes it)
    auto before = play_.dock_signature();
    play_.changed(now());
    redraw = play_.dock_signature() != before;
  }
  bool show = store_.ambient && atmosphere_sampled_ && atmosphere_ready() && !in_sizemove_ && !fullscreen_ && !IsZoomed(hwnd_) &&
              !fullscreen_transition_ &&
              !IsIconic(hwnd_) && !play_.iris_active() && vid_w_ > 0;
  if (show) ambient_.create(inst_);  // allocate halo windows only after playback has settled
  if (store_.ambient || ambient_.active()) ambient_.update(hwnd_, e, li, show, store_.ambient_strength, dt, in_sizemove_);
  return redraw;
}

void App::toggle_maximize() {
  if (fullscreen_) return;
  ShowWindow(hwnd_, IsZoomed(hwnd_) ? SW_RESTORE : SW_MAXIMIZE);
}

void App::begin_window_drag() {
  settle_fullscreen_motion();
  geom_.on = false;
  KillTimer(hwnd_, T_GEOM);
  play_.handoff_window_drag(now());
  if (fullscreen_) return;
  // Let Windows move/restore/Snap the parent directly. Polling the cursor from
  // the animation timer caused synchronous parent/child moves and visible jumps.
  // Post the handoff so it starts after the current mpv event has been drained.
  if (mpv_win_ && IsWindow(mpv_win_)) PostMessageW(mpv_win_, WM_CANCELMODE, 0, 0);
  ReleaseCapture();
  POINT p; GetCursorPos(&p);
  PostMessageW(hwnd_, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(p.x, p.y));
}

// The video window belongs to the engine's thread. Its WM_SETCURSOR is intercepted
// so the app decides the cursor (hidden, open hand over the subtitle, closed hand while
// dragging it), whatever mpv version is in use. Its mouse moves are forwarded right away:
// hover must not wait for a timer.
static std::atomic<bool> g_hide_cursor{false};
static std::atomic<int> g_cursor_kind{0};  // 0 arrow, 2 open hand, 3 closed hand
static HCURSOR g_cur_grab = nullptr, g_cur_grabbing = nullptr;
static std::atomic<bool> g_mouse_pending{false};
static std::atomic<long> g_mouse_xy{0};
static constexpr const wchar_t* kMpvOriginalProc = L"Cue.OriginalMpvProc";
static bool apply_cursor() {
  if (g_hide_cursor.load()) { SetCursor(nullptr); return true; }
  int k = g_cursor_kind.load();
  if (k == 1) { SetCursor(LoadCursorW(nullptr, IDC_HAND)); return true; }
  if (k == 2 && g_cur_grab) { SetCursor(g_cur_grab); return true; }
  if (k == 3 && g_cur_grabbing) { SetCursor(g_cur_grabbing); return true; }
  return false;
}
static void forward_mouse(LPARAM l) {
  g_mouse_xy = (long)l;
  if (!g_mouse_pending.exchange(true) && g_app_hwnd) PostMessageW(g_app_hwnd, WM_APP + 5, 0, 0);
}
static std::atomic<double> g_hit_ui_scale{1};
static int video_resize_hit(HWND owner) {
  if(!owner||IsZoomed(owner)||(GetWindowLongPtrW(owner,GWL_STYLE)&WS_POPUP))return HTCLIENT;
  POINT p;GetCursorPos(&p);RECT r;GetWindowRect(owner,&r);
  int x=p.x-r.left, y=p.y-r.top, width=r.right-r.left;
  return player_resize_hit(x,y,width,r.bottom-r.top,GetDpiForWindow(owner),g_hit_ui_scale.load());
}
static LRESULT CALLBACK mpv_subclass(HWND h, UINT m, WPARAM w, LPARAM l) {
  // Keep the original procedure on this HWND. A queued call from the old output
  // must never read a null/global pointer belonging to its replacement window.
  auto original = reinterpret_cast<WNDPROC>(GetPropW(h, kMpvOriginalProc));
  if(m==WM_SETCURSOR||m==WM_LBUTTONDOWN||m==WM_LBUTTONDBLCLK){
   int edge=video_resize_hit(g_app_hwnd);
   if(edge!=HTCLIENT){
    if(m==WM_SETCURSOR){SetCursor(LoadCursorW(nullptr,edge==HTLEFT||edge==HTRIGHT?IDC_SIZEWE:edge==HTTOP||edge==HTBOTTOM?IDC_SIZENS:edge==HTTOPLEFT||edge==HTBOTTOMRIGHT?IDC_SIZENWSE:IDC_SIZENESW));return TRUE;}
    if(m==WM_LBUTTONDOWN||m==WM_LBUTTONDBLCLK){POINT p;GetCursorPos(&p);PostMessageW(g_app_hwnd,WM_NCLBUTTONDOWN,edge,MAKELPARAM(p.x,p.y));return 0;}
   }
  }

  if (m == WM_SETCURSOR && LOWORD(l) == HTCLIENT && apply_cursor()) return TRUE;
  if (m == WM_MOUSEMOVE) forward_mouse(l);
  // One source of button input. In a CS_DBLCLKS window Windows replaces the
  // second DOWN with DBLCLK; route both as DOWN and suppress mpv's synthesis.
  if (m == WM_LBUTTONDOWN || m == WM_LBUTTONDBLCLK || m == WM_LBUTTONUP || m == WM_RBUTTONUP) {
    if (m == WM_LBUTTONDOWN || m == WM_LBUTTONDBLCLK) { SetFocus(h); SetCapture(h); }
    else if (m == WM_LBUTTONUP && GetCapture() == h) ReleaseCapture();
    UINT msg = m == WM_LBUTTONUP ? WM_APP + 8 : m == WM_RBUTTONUP ? WM_APP + 9 : WM_APP + 7;
    if (g_app_hwnd) PostMessageW(g_app_hwnd, msg, (WPARAM)h, l);
    return 0;
  }
  if (m == WM_RBUTTONDOWN || m == WM_RBUTTONDBLCLK) return 0;
  return original ? CallWindowProcW(original, h, m, w, l) : DefWindowProcW(h, m, w, l);
}

// Pixel hands for the subtitle, drawn on a 16-cell grid like the rest of the UI.
static HCURSOR pixel_cursor(const char* const* rows, int k) {
  int n = 16 * k;
  BITMAPV5HEADER bi{};
  bi.bV5Size = sizeof(bi);
  bi.bV5Width = n;
  bi.bV5Height = -n;
  bi.bV5Planes = 1;
  bi.bV5BitCount = 32;
  bi.bV5Compression = BI_BITFIELDS;
  bi.bV5RedMask = 0x00FF0000; bi.bV5GreenMask = 0x0000FF00; bi.bV5BlueMask = 0x000000FF; bi.bV5AlphaMask = 0xFF000000;
  void* bits = nullptr;
  HDC dc = GetDC(nullptr);
  HBITMAP color = CreateDIBSection(dc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  ReleaseDC(nullptr, dc);
  if (!color || !bits) return nullptr;
  auto* px = static_cast<uint32_t*>(bits);
  for (int y = 0; y < n; ++y)
    for (int x = 0; x < n; ++x) {
      char ch = rows[y / k][x / k];
      px[y * n + x] = ch == '#' ? 0xFF000000u : ch == 'o' ? 0xFFFFFFFFu : 0u;
    }
  HBITMAP mask = CreateBitmap(n, n, 1, 1, nullptr);
  ICONINFO ii{FALSE, (DWORD)(8 * k), (DWORD)(8 * k), mask, color};
  HCURSOR c = (HCURSOR)CreateIconIndirect(&ii);
  DeleteObject(mask);
  DeleteObject(color);
  return c;
}

void App::make_cursors() {
  static const char* open[16] = {
      "......##........", "...##.#o#.......", "..#oo##o#.##....", "..#oo#oo##oo#...",
      "...#oo#oo#oo#.#.", "...#oo#oo#oo##o#", ".##.#ooooooo#oo#", "#oo##oooooooooo#",
      "#ooo#ooooooooo#.", ".#oooooooooooo#.", "..#ooooooooooo#.", "..#oooooooooo#..",
      "...#ooooooooo#..", "....#oooooooo#..", ".....#oooooo#...", ".....#oooooo#..."};
  static const char* closed[16] = {
      "................", "................", "................", "....##.##.##....",
      "...#oo#oo#oo##..", "...#oo#oo#oo#o#.", "..##oooooooooo#.", ".#o#oooooooooo#.",
      ".#oooooooooooo#.", "..#ooooooooooo#.", "..#oooooooooo#..", "...#ooooooooo#..",
      "....#oooooooo#..", ".....#oooooo#...", ".....#oooooo#...", "................"};
  int k = std::max(1, GetSystemMetricsForDpi(SM_CXCURSOR, dpi_) / 16);
  if (g_cur_grab) DestroyCursor(g_cur_grab);
  if (g_cur_grabbing) DestroyCursor(g_cur_grabbing);
  g_cur_grab = pixel_cursor(open, k);
  g_cur_grabbing = pixel_cursor(closed, k);
}

void App::kick() {
  if (!anim_on_) { ensure_anim(); return; }
  frame();  // the cadence admits only the next due frame, including input-driven updates
}

void App::on_play_mouse(int x, int y) {
  if (mode_ != Mode::Play || closing_video_) return;
  if (x == last_cursor_.x && y == last_cursor_.y) return;
  last_cursor_ = {x, y};
  if (!play_.dock_visible()) play_.changed(now()); // catch up the clock before showing the cached dock
  play_.mouse_move(x, y, now());
  run_actions();
  kick();
}

// The engine's video window lives on the engine's thread, and Windows ties its input to ours.
// Before the engine destroys it, it must not be holding the mouse, the focus or our hook,
// otherwise both threads can end up waiting on each other (frozen app, spinning cursor).
void App::release_video_window() {
  closing_video_ = true;
  g_hide_cursor = false;
  if (mpv_win_ && IsWindow(mpv_win_)) {
    if (auto original = GetPropW(mpv_win_, kMpvOriginalProc))
      SetWindowLongPtrW(mpv_win_, GWLP_WNDPROC, (LONG_PTR)original);  // unhook
    PostMessageW(mpv_win_, WM_CANCELMODE, 0, 0);  // drops any mouse capture, on its own thread
  }
  mpv_win_ = nullptr;
  SetFocus(hwnd_);  // focus leaves the video window while its thread is still responsive
}

void App::try_hide_video() {
  if (!hide_video_pending_) return;
  bool engine_window = FindWindowExW(video_, nullptr, nullptr, nullptr) != nullptr;
  if (engine_window && now() - hide_video_since_ < 2.0) return;  // still being torn down
  hide_video_pending_ = false;
  ShowWindow(video_, SW_HIDE);
  InvalidateRect(hwnd_, nullptr, FALSE);
}

// Shuts the engine down on a worker thread while this thread keeps answering the
// messages its window may still send us. Never longer than `max_ms`: past that we stop waiting
// (the process ends right after, and Windows takes back whatever the engine still held).
void App::shutdown_engine(DWORD max_ms) {
  if (!mpv_ok_ || !mpv_.ready()) return;  // still starting on its thread: nothing of ours to wait for
  release_video_window();
  mpv_handle* h = mpv_.handle();
  auto fn = mpv_.api().terminate_destroy;
  mpv_.forget();
  mpv_ok_ = false;
  struct Job { void (*fn)(mpv_handle*); mpv_handle* h; };
  auto* job = new Job{fn, h};
  HANDLE th = CreateThread(nullptr, 0, [](void* p) -> DWORD {
    auto* j = static_cast<Job*>(p);
    j->fn(j->h);
    delete j;
    return 0;
  }, job, 0, nullptr);
  if (!th) { delete job; return; }  // no thread: do not risk waiting on our own thread
  DWORD start = GetTickCount();
  while (MsgWaitForMultipleObjects(1, &th, FALSE, 20, QS_ALLINPUT) != WAIT_OBJECT_0 && GetTickCount() - start < max_ms) {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) continue;  // we leave on our own terms, below
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  CloseHandle(th);
}

// Leaving: what the person sees goes first (window, glow, sound), then what must be kept
// (settings, position), then the rest is let go without waiting for anyone.
void App::quit_now() {
  for (UINT_PTR t : {T_ANIM, T_POLL, T_WAKE, T_SAVE, T_THUMB, T_PERSIST, T_GEOM, T_PREFETCH, T_FINE}) KillTimer(hwnd_, t);
  if (mpv_ok_) release_video_window();  // focus and mouse leave the engine's window first, as before
  ShowWindow(hwnd_, SW_HIDE);  // gone from the screen and the taskbar now
  ambient_.destroy();
  scene_.destroy();
  news_host_.destroy();
  pacer_.stop();
  remote_.stop(false);
  if (mpv_ok_) mpv_.set_flag_async("pause", true);  // silence at once (the engine may take a moment to end)
  save_position();
  save_now(true);
  log("bye");
  if (drop_) { RevokeDragDrop(hwnd_); drop_->Release(); drop_ = nullptr; }
  thumbs_.kill();  // the preview helper keeps nothing: ended, not asked
  // a short grace so the engine can close the audio device and finish a frame being saved;
  // the window is already gone, so nobody waits for it
  shutdown_engine(1200);
  if (fine_timer_) { timeEndPeriod(1); fine_timer_ = false; }
  // everything that matters is on disk: skip the long teardown of a 70 MB engine DLL
  TerminateProcess(GetCurrentProcess(), 0);
  DestroyWindow(hwnd_);  // never reached; the ordinary way out, just in case
}

void App::update_cursor(double t) {
  g_hit_ui_scale = s_;
  if (closing_video_) return;
  if (mpv_win_ && !IsWindow(mpv_win_)) mpv_win_ = nullptr;  // the engine recreates its window per file
  if (!mpv_win_) {
    HWND child = FindWindowExW(video_, nullptr, nullptr, nullptr);
    if (child) {
      auto original = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(child, GWLP_WNDPROC));
      if (original && original != mpv_subclass && SetPropW(child, kMpvOriginalProc, reinterpret_cast<HANDLE>(original))) {
        mpv_win_ = child;
        SetWindowLongPtrW(child, GWLP_WNDPROC, (LONG_PTR)mpv_subclass);
      }
    }
  }
  POINT p = cursor_client();
  RECT rc;
  GetClientRect(hwnd_, &rc);
  bool inside = PtInRect(&rc, p) && GetForegroundWindow() == hwnd_;
  bool hide = mode_ == Mode::Play && inside && !in_sizemove_ && !play_.pinned() && t - play_.last_activity() > tok::dock_idle;
  int kind = mode_ == Mode::Play && inside ? play_.cursor_kind() : 0;
  if (hide != g_hide_cursor.load() || kind != g_cursor_kind.load()) {
    g_hide_cursor = hide;
    g_cursor_kind = kind;
    // re-evaluate the cursor now, without waiting for the mouse to move
    HWND target = mpv_win_ ? mpv_win_ : video_;
    PostMessageW(target, WM_SETCURSOR, (WPARAM)target, MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
  }
}

// ── window procedures ───────────────────────────────────────────────
LRESULT CALLBACK App::wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
  App* self = nullptr;
  if (m == WM_NCCREATE) {
    self = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
    SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
    self->hwnd_ = h;
  } else {
    self = reinterpret_cast<App*>(GetWindowLongPtrW(h, GWLP_USERDATA));
  }
  if (!self) return DefWindowProcW(h, m, w, l);
  return self->handle(m, w, l);
}

// The video host only matters before mpv creates its own window (or for audio-only files).
LRESULT CALLBACK App::videoproc(HWND h, UINT m, WPARAM w, LPARAM l) {
  App* self = reinterpret_cast<App*>(GetWindowLongPtrW(h, GWLP_USERDATA));
  if (self && (m==WM_SETCURSOR||m==WM_LBUTTONDOWN||m==WM_LBUTTONDBLCLK)) {
    int edge=video_resize_hit(self->hwnd_);
    if(edge!=HTCLIENT) {
      if(m==WM_SETCURSOR) {
        SetCursor(LoadCursorW(nullptr,edge==HTLEFT||edge==HTRIGHT?IDC_SIZEWE:edge==HTTOP||edge==HTBOTTOM?IDC_SIZENS:edge==HTTOPLEFT||edge==HTBOTTOMRIGHT?IDC_SIZENWSE:IDC_SIZENESW));
      } else {
        POINT p; GetCursorPos(&p); PostMessageW(self->hwnd_,WM_NCLBUTTONDOWN,edge,MAKELPARAM(p.x,p.y));
      }
      return m==WM_SETCURSOR ? TRUE : 0;
    }
  }
  if (m == WM_SETCURSOR && LOWORD(l) == HTCLIENT && apply_cursor()) return TRUE;
  if (self && self->mode_ == Mode::Play) {
    switch (m) {
      case WM_MOUSEMOVE: self->on_play_mouse(GET_X_LPARAM(l), GET_Y_LPARAM(l)); return 0;
      case WM_LBUTTONDOWN:
      case WM_LBUTTONDBLCLK: SetCapture(h); self->mouse_button(0, true, GET_X_LPARAM(l), GET_Y_LPARAM(l)); return 0;
      case WM_LBUTTONUP: ReleaseCapture(); self->mouse_button(0, false, GET_X_LPARAM(l), GET_Y_LPARAM(l)); return 0;
      case WM_RBUTTONUP: self->mouse_button(1, true, GET_X_LPARAM(l), GET_Y_LPARAM(l)); return 0;
      case WM_MOUSEWHEEL:
        // ctrl + wheel: the interface size, one notch a step (a trackpad's small deltas add up)
        if (GET_KEYSTATE_WPARAM(w) & MK_CONTROL) {
          self->size_wheel_ += GET_WHEEL_DELTA_WPARAM(w);
          if (std::abs(self->size_wheel_) >= WHEEL_DELTA) {
            int dir = self->size_wheel_ > 0 ? 1 : -1;
            self->size_wheel_ = 0;
            int next = next_ui_size(self->store_.ui_scale, dir);
            if (next != self->store_.ui_scale) self->set_ui_scale(next);
            self->say_ui_size();
          }
          return 0;
        }
        self->play_.wheel(GET_WHEEL_DELTA_WPARAM(w) > 0 ? 1 : -1, self->now()); self->run_actions(); self->ensure_anim(); return 0;
    }
  }
  return DefWindowProcW(h, m, w, l);
}

static std::string vk_name(WPARAM vk) {
  switch (vk) {
    case VK_SPACE: return "space";
    case VK_LEFT: return "left";
    case VK_RIGHT: return "right";
    case VK_UP: return "up";
    case VK_DOWN: return "down";
    case VK_RETURN: return "enter";
    case VK_ESCAPE: return "esc";
    case VK_TAB: return "tab";
    case VK_VOLUME_UP: case VK_VOLUME_DOWN: case VK_VOLUME_MUTE: return "";
    case VK_MEDIA_PLAY_PAUSE: return "space";
    case VK_MEDIA_NEXT_TRACK: return "n";
    case VK_OEM_PLUS: case VK_ADD: return "plus";
    case VK_OEM_MINUS: case VK_SUBTRACT: return "minus";
    case '0': case VK_NUMPAD0: return "zero";
  }
  if ((vk >= 'A' && vk <= 'Z')) return std::string(1, (char)(vk - 'A' + 'a'));
  return "";
}

LRESULT App::handle(UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_COPYDATA: {
      if (closing_app_) return FALSE;
      std::vector<std::string> files;
      if (!SingleInstance::decode(reinterpret_cast<const COPYDATASTRUCT*>(l), files)) return FALSE;
      // Copy before the sender exits, then open on our normal event loop.
      // Rapid launches coalesce to the latest request.
      if (!files.empty()) launch_files_ = std::move(files);
      return PostMessageW(hwnd_, WM_APP_OPEN_FILES, 0, 0) ? TRUE : FALSE;
    }
    case WM_APP_OPEN_FILES: {
      if (closing_app_) return 0;
      auto files = std::move(launch_files_);
      launch_files_.clear();
      if (!files.empty()) handle_files(files);
      if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
      SetForegroundWindow(hwnd_);
      return 0;
    }
    case WM_APP_ENGINE: on_engine_ready(w != 0); return 0;
    case WM_APP_LEFT_DOWN:
    case WM_APP_LEFT_UP:
    case WM_APP_RIGHT_UP: {
      HWND source = (HWND)w;
      if (source != mpv_win_ || !IsWindow(source)) return 0; // late input from an unloaded file
      POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
      MapWindowPoints(source, hwnd_, &p, 1);
      mouse_button(m == WM_APP_RIGHT_UP ? 1 : 0, m != WM_APP_LEFT_UP, p.x, p.y);
      return 0;
    }
    case WM_APP_MOUSE: {
      g_mouse_pending = false;
      long xy = g_mouse_xy.load();
      on_play_mouse(GET_X_LPARAM(xy), GET_Y_LPARAM(xy));
      return 0;
    }
    case WM_APP_RECENTS: {
      auto* missing = reinterpret_cast<std::vector<std::wstring>*>(l);
      if ((unsigned)w == recents_gen_) {
        missing_ = std::set<std::wstring>(missing->begin(), missing->end());
        refresh_recents(false);
        ensure_anim();
      }
      delete missing;
      return 0;
    }
    case WM_NCCALCSIZE:
      if (w) {
        // no title bar: the side and bottom frames stay (resize, shadow, Windows 11 corners)
        auto* pr = reinterpret_cast<NCCALCSIZE_PARAMS*>(l);
        RECT orig = pr->rgrc[0];
        DefWindowProcW(hwnd_, m, w, l);
        if (!fullscreen_ && !IsZoomed(hwnd_)) {
          // The content covers the whole window, edge to edge. The native border pixel this
          // used to keep was drawn dark and showed as a thin line along the rounded corners.
          // DWM still rounds the window (and clips the video with it) and keeps its shadow.
          pr->rgrc[0] = orig;
          return 0;
        }
        pr->rgrc[0].top = orig.top;
        if (IsZoomed(hwnd_) && !fullscreen_)
          pr->rgrc[0].top += GetSystemMetricsForDpi(SM_CYFRAME, dpi_) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi_);
        return 0;
      }
      break;
    case WM_NCHITTEST: {
      LRESULT r = DefWindowProcW(hwnd_, m, w, l);
      if (r != HTCLIENT || fullscreen_) return r;
      POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
      ScreenToClient(hwnd_, &p);
      if (!IsZoomed(hwnd_)) {
        int edge = player_resize_hit(p.x, p.y, cw_, ch_, dpi_, s_);
        if (edge != HTCLIENT) return edge;
      }
      if (mode_ == Mode::Home && (onboarding_ ? ob_.is_background(p.x, p.y) : home_.is_background(p.x, p.y)))
        return HTCAPTION;  // drag + Snap, native
      return HTCLIENT;
    }
    case WM_APP_MPV: drain_mpv(); return 0;
    case WM_APP_REMOTE: remote_events(); return 0;
    case WM_APP_UPDATE: {
      auto* um = reinterpret_cast<UpdateMsg*>(l);
      if (um && !closing_app_) on_update(um);
      delete um;
      return 0;
    }
    case WM_APP_VSYNC:
      pacer_.consumed();
      if (anim_on_) frame();
      return 0;
    case WM_DISPLAYCHANGE: refresh_display_rate(); refresh_ui_scale(); break;
    case WM_APP_THUMB: {
      auto* tm = reinterpret_cast<ThumbMsg*>(l);
      if (w == 1 && tm) { on_thumb(tm); delete tm; }
      return 0;
    }
    case WM_SIZE: {
      if (w == SIZE_MINIMIZED) return 0;
      apply_window_corners();
      cw_ = LOWORD(l);
      ch_ = HIWORD(l);
      home_.chrome.maximized = play_.chrome.maximized = IsZoomed(hwnd_) != 0;
      if (video_) SetWindowPos(video_, nullptr, 0, 0, cw_, ch_, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS);
      // the engine's own window gets the new size right away, asynchronously (its thread does the
      // resize; ours never waits on it), instead of whenever it notices the parent changed
      if (mpv_win_ && IsWindow(mpv_win_))
        SetWindowPos(mpv_win_, nullptr, 0, 0, cw_, ch_, SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS | SWP_NOCOPYBITS);
      // the scale follows the window, but never mid-gesture: while the person drags an edge it
      // holds (applied when they let go), and while the window animates to a video's shape it
      // takes the size it is going to, at once
      if (cw_ > 0 && ch_ > 0 && !in_sizemove_) {
        int tw = cw_, th = ch_;
        if (geom_.on) { tw = geom_.to.right - geom_.to.left; th = geom_.to.bottom - geom_.to.top; }
        double ns = scale_for(tw, th, store_.ui_scale);
        if (ns != s_) {
          s_ = ns;
          log("ui scale " + std::to_string(s_));
          // previews already decoded stay (a size off for a while costs nothing); restarting the
          // preview engine here is what used to make fullscreen changes stutter
        }
      }
      home_.set_size(cw_, ch_, s_);
      ob_.set_size(cw_, ch_, s_);
      play_.resize(cw_, ch_, portrait_scale(s_, cw_, ch_, store_.ui_scale));
      if (mode_ == Mode::Play) {
        play_.changed(now());
        apply_style(true);  // only what changed (normally just the lifted subtitle position)
        // the overlays follow the new size in this same message: no frame with stale UI
        play_.prepare(now());
        upload_overlays();
      }
      ensure_anim();
      return 0;
    }
    case WM_DPICHANGED: {
      dpi_ = HIWORD(w);
      refresh_display_rate();
      make_cursors();
      if (fullscreen_) {
        MONITORINFO mi{sizeof(mi)}; GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowPos(hwnd_, nullptr, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right-mi.rcMonitor.left, mi.rcMonitor.bottom-mi.rcMonitor.top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
      }
      RECT* r = reinterpret_cast<RECT*>(l);
      {
        // the client area of the suggested window rect, at the new DPI
        RECT wr, cr; GetWindowRect(hwnd_, &wr); GetClientRect(hwnd_, &cr);
        int fw = (wr.right - wr.left) - cr.right, fh = (wr.bottom - wr.top) - cr.bottom;  // frame
        s_ = ui_scale(dpi_, (r->right - r->left) - fw, (r->bottom - r->top) - fh, store_.ui_scale);
      }
      SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }
    case WM_WINDOWPOSCHANGED:
      ambient_.follow(hwnd_, in_sizemove_);  // move existing pixels; defer expensive shape rebuilds
      scene_.follow(hwnd_, s_);
      news_host_.follow(hwnd_, s_);
      break;
    case WM_GETMINMAXINFO: {
      auto* mm = reinterpret_cast<MINMAXINFO*>(l);
      // the smallest window where the layout still fits at this monitor's base scale (not the
      // current one: that grows with the window and would stop it from getting smaller)
      double bs = ui_base_scale(dpi_, store_.ui_scale);
      mm->ptMinTrackSize.x = 240 * bs;  // narrow enough for a portrait video, the dock recomposes
      mm->ptMinTrackSize.y = 180 * bs;
      return 0;
    }
    case WM_ENTERSIZEMOVE:
      in_sizemove_ = true;
      settle_fullscreen_motion();
      geom_.on = false;  // the person takes over: our animation stops right there
      { RECT r; GetClientRect(hwnd_, &r); sizemove_start_ = {r.right, r.bottom}; }
      break;
    case WM_EXITSIZEMOVE: {
      in_sizemove_ = false;
      refresh_display_rate();  // it may have moved to a monitor with another refresh rate
      refresh_ui_scale();      // the size it was given while the edge was held
      ambient_.follow(hwnd_);
      if (mode_ == Mode::Play) { play_.activity(now()); ensure_anim(); }
      RECT r;
      GetClientRect(hwnd_, &r);
      if (r.right != sizemove_start_.cx || r.bottom != sizemove_start_.cy) {
        user_sized_ = true;              // respected for this media
        auto_portrait_ = false;          // their shape now, not ours
        if (r.bottom <= r.right * 1.05) landscape_size_ = {r.right, r.bottom};
      }
      break;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(hwnd_, &ps);
      if (mode_ == Mode::Home) {
        if (home_canvas_.w != cw_ || home_canvas_.h != ch_) {
          if (onboarding_) ob_.render(home_canvas_, now());
          else home_.render(home_canvas_, now());
        }
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = home_canvas_.w;
        bi.bmiHeader.biHeight = -home_canvas_.h;  // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        SetDIBitsToDevice(dc, 0, 0, home_canvas_.w, home_canvas_.h, 0, 0, 0, home_canvas_.h, home_canvas_.px.data(), &bi,
                          DIB_RGB_COLORS);
      } else {
        FillRect(dc, &ps.rcPaint, (HBRUSH)GetStockObject(BLACK_BRUSH));
      }
      EndPaint(hwnd_, &ps);
      return 0;
    }
    case WM_TIMER:
      if (w == T_ANIM) frame();
      else if (w == T_WAKE) { KillTimer(hwnd_, T_WAKE); ensure_anim(); }
      else if (w == T_SAVE) save_position();
      else if (w == T_PERSIST) save_now();
      else if (w == T_MAINTENANCE) {
        bool idle_home = mode_ == Mode::Home && pending_open_.empty() && mpv_ok_;
        bool settled_media = mode_ == Mode::Play && loaded_ && first_frame_ && !play_.iris_active() && now() - loaded_at_ >= 3;
        if (idle_home || settled_media || engine_failed_) {
          start_maintenance();
          if (maintenance_started_) KillTimer(hwnd_, T_MAINTENANCE);
        } else SetTimer(hwnd_, T_MAINTENANCE, 500, nullptr);
      }
      else if (w == T_GEOM) decide_window();
      else if (w == T_REMOTE) remote_tick();
      else if (w == T_REMOTE_START) { KillTimer(hwnd_, T_REMOTE_START); remote_start(); }
      else if (w == T_NEWS) { KillTimer(hwnd_, T_NEWS); show_news(); }
      else if (w == T_NEXT) {
        // the episode ended with a phone in hand: the next one, after a breath
        KillTimer(hwnd_, T_NEXT);
        auto target = next_advance_.take();
        if (!target.empty() && mode_ == Mode::Play && !next_cancelled_ && !store_.loop && remote_timer_ != -1 && next_auto_)
          open(std::move(target));
      }
      else if (w == T_PREFETCH) { KillTimer(hwnd_, T_PREFETCH); thumb_next(); }
      else if (w == T_FINE) {
        KillTimer(hwnd_, T_FINE);
        if (!anim_on_ && fine_timer_) { timeEndPeriod(1); fine_timer_ = false; }
      }
      else if (w == T_THUMB) {
        // Start colour sampling after 1.5 s; timeline decoding still waits 3 s.
        KillTimer(hwnd_, T_THUMB);
        if (mode_ == Mode::Play && loaded_) {
          if (atmosphere_ready() && play_.in.dur > 0 && !IsIconic(hwnd_)) thumb_request(-1);
          else SetTimer(hwnd_, T_THUMB, 250, nullptr);
        }
      }
      else if (w == T_POLL && mode_ == Mode::Home && !IsIconic(hwnd_)) {
        try_hide_video();
        POINT p;
        GetCursorPos(&p);
        ScreenToClient(hwnd_, &p);
        last_cursor_ = p;
        if (onboarding_ ? ob_.track(p.x, p.y, now()) : home_.track(p.x, p.y, now())) ensure_anim();
        if (ambient_.active()) {  // leaving a film: its light fades out
          double tn = now(), adt = std::clamp(tn - atmo_t_, 0.0, 0.1);
          atmo_t_ = tn;
          step_atmosphere(adt);
        }
      }
      else if (w == T_POLL && mode_ == Mode::Play) {
        if (!next_ep_.empty() && play_.in.dur > 0 &&
            (play_.in.dur - play_.in.time) / std::max(.1, play_.in.speed) <= 90 && !next_thumb_requested_) {
          next_thumb_job_ = request_file_thumbnail(next_ep_);
          next_thumb_requested_ = !!next_thumb_job_;
        }
        if (next_thumb_job_ && next_thumb_job_->done.load()) {
          play_.set_next_picture(next_thumb_job_->pixels,84,48);
          next_thumb_job_.reset(); ensure_anim();
        }
        if (IsIconic(hwnd_)) {
          if (ambient_.active()) {
            double tn = now(), dt = std::clamp(tn - atmo_t_, 0.0, 0.1);
            atmo_t_ = tn;
            step_atmosphere(dt);
          }
          return 0;
        }
        POINT p = cursor_client();
        RECT rc;
        GetClientRect(hwnd_, &rc);
        bool inside = PtInRect(&rc, p) && GetForegroundWindow() == hwnd_;
        if (inside && (p.x != last_cursor_.x || p.y != last_cursor_.y)) {
          on_play_mouse(p.x, p.y);  // fallback: moves normally arrive from the video window right away
        } else if (play_.dock_visible() && !play_.pinned() && now() - play_.last_activity() > tok::dock_idle) {
          ensure_anim();  // lets the dock notice 2 s of stillness and leave
        }
        update_cursor(now());
        if (hw_log_t_ > 0 && now() > hw_log_t_) {
          hw_log_t_ = 0;
          log("video " + std::to_string(vid_w_) + "x" + std::to_string(vid_h_) + ", decode " +
              (hwdec_.empty() || hwdec_ == "no" ? std::string("software") : hwdec_) + ", sync " + video_sync_);
        }
        {
          // atmosphere at ~30 Hz from this timer, never on the 120 Hz animation path
          double tn = now(), adt = std::clamp(tn - atmo_t_, 0.0, 0.1);
          atmo_t_ = tn;
          if (step_atmosphere(adt)) ensure_anim();
        }
        // Sample colours independently of drawing the light. No repeated decode
        // while paused; spatial interpolation continues at the UI poll's ~30 Hz.
        if (atmosphere_ready() && preview_.loaded && !preview_.failed && now() - accent_req_t_ > (store_.ambient ? 0.9 : 4.0) &&
            (accent_media_t_ < 0 || std::fabs(play_.in.time - accent_media_t_) >= 0.25) &&
            !IsIconic(hwnd_)) {
          accent_req_t_ = now();
          accent_media_t_ = play_.in.time;
          accent_want_ = play_.in.time;
          thumb_next();
        }
      }
      return 0;
    case WM_MOUSEMOVE:
      if (mode_ == Mode::Home) {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&tme);
        if (onboarding_) ob_.mouse_move(GET_X_LPARAM(l), GET_Y_LPARAM(l), now());
        else home_.mouse_move(GET_X_LPARAM(l), GET_Y_LPARAM(l), now());
        ensure_anim();
      }
      return 0;
    case WM_NCMOUSEMOVE:
      // over the draggable background Windows reports non-client moves: the home still needs them
      if (mode_ == Mode::Home) {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE | TME_NONCLIENT, hwnd_, 0};
        TrackMouseEvent(&tme);
        POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        ScreenToClient(hwnd_, &p);
        if (onboarding_) ob_.mouse_move(p.x, p.y, now());
        else home_.mouse_move(p.x, p.y, now());
        ensure_anim();
      }
      break;
    case WM_MOUSELEAVE:
    case WM_NCMOUSELEAVE:
      if (mode_ == Mode::Home) {
        POINT p = cursor_client();
        RECT rc;
        GetClientRect(hwnd_, &rc);
        if (!PtInRect(&rc, p)) {  // really left, not client<->caption
          if (onboarding_) ob_.mouse_leave(now()); else home_.mouse_leave(now());
          ensure_anim();
        }
      }
      return 0;
    case WM_LBUTTONDOWN:
      if (mode_ == Mode::Home && onboarding_) {
        on_onboarding(ob_.mouse_down(GET_X_LPARAM(l), GET_Y_LPARAM(l), now()));
        ensure_anim();
        return 0;
      }
      if (mode_ == Mode::Home) {
        HomeAction a = home_.mouse_down(GET_X_LPARAM(l), GET_Y_LPARAM(l), now());
        if (a.a == HomeAct::OpenDialog) { auto f = open_file_dialog(hwnd_, DialogKind::Video); if (!f.empty()) handle_files(f); }
        else if (a.a == HomeAct::ClearRecents) { store_.clear_recents(); save_state(); refresh_recents(); }
        else if (a.a == HomeAct::EggSeen) { store_.eggs_seen = home_.eggs_seen; save_state(); }
        else if (a.a == HomeAct::WinMin) ShowWindow(hwnd_, SW_MINIMIZE);
        else if (a.a == HomeAct::WinPin) toggle_pin();
        else if (a.a == HomeAct::WinMax) toggle_maximize();
        else if (a.a == HomeAct::WinClose) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        else if (a.a == HomeAct::Update) start_update();
        else if (a.a == HomeAct::Language) set_language_pref(a.index);
        else if (a.a == HomeAct::Remote) open_remote_scene();
        else if (a.a == HomeAct::News) show_news(true);
        else if (a.a == HomeAct::NewsClose) close_news_line();
        else if (a.a == HomeAct::SkipUpdate) {
          store_.update_skipped = upd_rel_.version;
          upd_ = Upd::Idle;
          save_state();
          show_update_state();
        }
        if (a.a == HomeAct::OpenRecent) {
      if (a.index >= 0 && a.index < (int)shown_paths_.size()) open(shown_paths_[a.index]);
        }
        ensure_anim();
      }
      return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
      if (m == WM_SYSKEYDOWN && w != VK_RETURN) break;
      bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
      if (m == WM_SYSKEYDOWN && w == VK_RETURN) { if (mode_ == Mode::Play) toggle_fullscreen(); return 0; }
      std::string k = vk_name(w);
      if (k.empty()) break;
      key(k, shift, ctrl);
      return 0;
    }
    case WM_THEMECHANGED:
    case WM_DWMCOMPOSITIONCHANGED:
    case WM_SETTINGCHANGE:
      play_.set_double_click_time(GetDoubleClickTime() / 1000.0);
      reduce_motion() = system_reduce_motion();
      set_high_contrast(system_high_contrast());
      apply_theme_frame();
      ensure_anim();
      break;
    case WM_CLOSE:
      if (closing_app_) return 0;
      closing_app_ = true;
      {
        // remember the window for next time (the normal rect, even if maximized or fullscreen)
        WINDOWPLACEMENT pl{sizeof(pl)};
        if (fullscreen_) pl = wp_; else GetWindowPlacement(hwnd_, &pl);
        RECT r = pl.rcNormalPosition;  // workspace coordinates: shift to screen coordinates
        MONITORINFO pmi{sizeof(pmi)};
        GetMonitorInfoW(MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST), &pmi);
        OffsetRect(&r, pmi.rcWork.left - pmi.rcMonitor.left, pmi.rcWork.top - pmi.rcMonitor.top);
        if (r.right - r.left > 200 && r.bottom - r.top > 120) {
          bool mx = fullscreen_ ? pl.showCmd == SW_SHOWMAXIMIZED : IsZoomed(hwnd_) != 0;
          store_.win = {true, (int)r.left, (int)r.top, (int)(r.right - r.left), (int)(r.bottom - r.top), (int)dpi_, mx};
        }
      }
      quit_now();  // does not return
      return 0;
    case WM_ENDSESSION:
      // Windows is logging off or shutting down (no WM_CLOSE comes): keep the place in the film
      if (w) { save_position(); save_now(true); }
      return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
  }
  return DefWindowProcW(hwnd_, m, w, l);
}
