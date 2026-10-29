#pragma once
#include <windows.h>

#include <array>
#include <atomic>
#include <climits>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../core/store.h"
#include "../core/playback_warmup.h"
#include "../core/preview_session.h"
#include "../core/overlay_buffers.h"
#include "../core/media_geometry.h"
#include "../ui/home.h"
#include "../ui/onboarding.h"
#include "../ui/play.h"
#include "../ui/atmosphere.h"
#include "ambient.h"
#include "mpv.h"
#include "thumbhost.h"
#include "platform.h"
#include "vsync.h"
#include "updater.h"

class App {
 public:
  bool init(HINSTANCE inst, const std::vector<std::string>& args, int show);
  int run();

 private:
  enum class Mode { Home, Play };
  enum : UINT { WM_APP_MPV = WM_APP + 1, WM_APP_ENGINE = WM_APP + 2, WM_APP_RECENTS = WM_APP + 3, WM_APP_THUMB = WM_APP + 4, WM_APP_MOUSE = WM_APP + 5, WM_APP_OPEN_FILES = WM_APP + 6, WM_APP_LEFT_DOWN = WM_APP + 7, WM_APP_LEFT_UP = WM_APP + 8, WM_APP_RIGHT_UP = WM_APP + 9, WM_APP_VSYNC = WM_APP + 10, WM_APP_UPDATE = WM_APP + 11 };
  enum : UINT_PTR { T_ANIM = 1, T_POLL = 2, T_WAKE = 3, T_SAVE = 4, T_THUMB = 5, T_PERSIST = 6, T_GEOM = 7, T_PREFETCH = 8, T_FINE = 9, T_MAINTENANCE = 10 };

  HINSTANCE inst_ = nullptr;
  HWND hwnd_ = nullptr, video_ = nullptr;
  Mpv mpv_;
  bool mpv_ok_ = false;
  std::atomic<bool> mpv_wake_pending_{false};
  void wake_mpv();
  std::string engine_err_;
  Store store_;
  std::filesystem::path state_path_, data_dir_;
  HomeView home_;
  OnboardingView ob_;      // first run: shown in the home's place until finished
  bool onboarding_ = false;
  void on_onboarding(ObAction a);
  PlayView play_;
  Canvas home_canvas_;      // what is on screen (WM_PAINT copies from it)
  Canvas home_back_;        // the next frame is drawn here, then compared and swapped
  void present_home(double t);
  Mode mode_ = Mode::Home;
  double s_ = 2;
  UINT dpi_ = 96;
  int cw_ = 0, ch_ = 0;
  bool fullscreen_ = false;
  bool fullscreen_transition_ = false;
  int fullscreen_stage_ = 0, osd_w_ = 0, osd_h_ = 0;
  double fullscreen_changed_at_ = 0;
  void perform_fullscreen_switch();
  bool step_fullscreen_transition(double t);
  uint64_t pause_request_ = 0;
  bool pause_pending_ = false, pause_target_ = false;
  void request_pause(bool paused);
  DWORD corner_preference_ = DWORD(-1);
  WINDOWPLACEMENT wp_{sizeof(WINDOWPLACEMENT)};
  LONG_PTR saved_style_ = 0;
  DropTarget* drop_ = nullptr;

  // playback session
  std::string path_;
  bool loaded_ = false;
  double loaded_at_ = 0;
  double resume_toast_ = -1;
  std::vector<std::string> pending_subs_;
  std::string pending_open_;
  std::vector<std::string> launch_files_;
  int ext_subs_ = 0;
  double last_time_ = 0;
  bool seek_in_flight_ = false;
  double seek_pending_ = -1;
  bool cursor_autohide_ = true;
  POINT last_cursor_{-1, -1};
  bool anim_on_ = false;
  double last_tick_ = 0, home_paint_at_ = -1;
  FrameCadence animation_cadence_;
  VsyncPacer pacer_;              // display-locked ticks (SetTimer only as a fallback)
  double play_interval_ = 1.0 / 60;  // overlay uploads: a divisor of the refresh rate, at most ~90 Hz
  void refresh_display_rate();
  void arm_animation_timer();
  LARGE_INTEGER qpf_{}, qp0_{};

  // One upload in flight per layer. mpv copies the pixels when it executes overlay-add, which
  // can be a while after we queue it: the buffer it points at must stay untouched until its
  // reply arrives. Never reused, resized or freed before that.
  // Two buffers: the one the engine shows (it may keep reading it) and the one being sent.
  using Ov = OverlayBuffers;
  bool overlays_pending() const;
  void update_blur_cloud();
  std::string cloud_options_;
  FrameCadence cloud_cadence_;
  bool cloud_pending_ = false;
  std::array<Ov, PlayView::L_COUNT> ov_;

  static LRESULT CALLBACK wndproc(HWND, UINT, WPARAM, LPARAM);
  static LRESULT CALLBACK videoproc(HWND, UINT, WPARAM, LPARAM);
  LRESULT handle(UINT, WPARAM, LPARAM);

  double now() const;
  // The engine loads on its own thread after the shell's first paint (to avoid
  // loader-lock stalls during window creation). The UI thread touches mpv_ only after
  // WM_APP_ENGINE arrives (mpv_ok_), so nothing is shared while the worker writes.
  struct EngineCfg {
    int64_t wid = 0;
    std::string slang, alang;
    double volume = 100;
    bool muted = false, log = false;
  } engine_cfg_;
  HANDLE engine_thread_ = nullptr, engine_go_ = nullptr;
  bool engine_log_ = false, engine_failed_ = false;
  bool maintenance_started_ = false;
  double open_started_ = 0;
  static DWORD WINAPI engine_main(void* self);
  bool start_engine();  // worker: load, create, set options, initialize
  void on_engine_ready(bool ok);
  void install_user_fonts();
  void start_maintenance();
  void write_input_conf(const std::wstring& path);
  void drain_mpv();
  void on_property(uint64_t id, const mpv_event_property* p);
  void on_client_message(const mpv_event_client_message* m);
  void on_file_loaded();
  void refresh_file_info();
  void configure_audio_mode();
  bool audio_only_ = false, waveform_active_ = false, waveform_fallback_ = false;
  void on_end_file(const mpv_event_end_file* e);

  void open(const std::string& path, std::vector<std::string> subs = {});
  void handle_files(const std::vector<std::string>& files);
  void go_home();
  void refresh_recents(bool check = true);
  void resolve_props();
  // timeline previews
  ThumbClient thumbs_;  // previews + film colour, decoded in a background helper process
  std::string thumb_path_;
  PreviewSession preview_;
  double thumb_want_ = -1, thumb_seek_t_ = -1;
  int thumb_shown_ = INT_MIN;
  struct ThumbImg { std::vector<uint32_t> px; int w = 0, h = 0; };
  std::map<int, ThumbImg> thumb_cache_;  // slice of the timeline -> frame
  std::vector<double> thumb_order_;      // background fill, coarse to fine
  size_t thumb_order_pos_ = 0;
  double thumb_step() const;
  int thumb_key(double t) const;
  bool first_frame_ = false, reveal_pending_ = false, swallow_next_ = false;
  void reveal_play();
  void try_start_iris();
  void thumb_request(double t);  // t < 0: just keep filling in the background
  void thumb_next();
  bool prefetch_on_ = false;
  double last_prefetch_t_ = -10;
  void thumb_store(std::vector<uint32_t>&& px, int w, int h);
  void on_thumb(ThumbMsg* m);
  void thumb_stop();
  unsigned recents_gen_ = 0;
  std::set<std::wstring> missing_;
  std::vector<std::string> shown_paths_;
  std::string observed_path_;
  bool has_background_box_ = false;
  void save_state();   // debounced: sliders and drags do not write on every step
  void save_now(bool synchronous = false);
  HANDLE save_thread_ = nullptr;
  bool tracks_applied_ = false;
  void apply_media_tracks();
  void save_frame(bool with_subs);
  std::wstring last_frame_;  // the frame saved last, for the toast's folder button
  bool eof_ = false;         // paused on the last frame (play starts over)
  void set_ui_scale(int v);
  // updates from GitHub Releases: checked every 15 days in the background, or when asked
  enum class Upd { Idle, Checking, Available, Downloading, Launching };
  Upd upd_ = Upd::Idle;
  ReleaseInfo upd_rel_;           // empty url: only the version is known (from a previous run)
  bool upd_manual_ = false, upd_install_after_check_ = false;
  int upd_pct_ = -1;
  void check_updates(bool manual);
  void start_update();
  void on_update(UpdateMsg* m);
  void show_update_state();
  void say(const std::string& text);  // a short message wherever the person is (home or player)
  // the film's atmosphere: sampled from the preview engine (never the playback one)
  Ambient ambient_;
  ColorSmoother accent_sm_, zone_sm_[FrameColors::kZones];
  Rgb accent_target_{255, 255, 255}, zone_target_[FrameColors::kZones]{};
  double accent_k_target_ = 0, zone_light_target_[FrameColors::kZones]{};
  double accent_want_ = -1, accent_req_t_ = -10;
  double accent_media_t_ = -1;
  bool thumb_for_accent_ = false;
  PlaybackWarmup background_warmup_;
  bool atmosphere_sampled_ = false;
  bool background_ready(double delay = PlaybackWarmup::kDelay) const;
  bool atmosphere_ready() const;
  void reset_atmosphere();
  void sample_atmosphere(const std::vector<uint32_t>& px, int w, int h);
  bool step_atmosphere(double dt);
  double atmo_t_ = 0;
  // the window follows the content: portrait video -> portrait window, landscape -> landscape
  int64_t vid_w_ = 0, vid_h_ = 0, vid_rot_ = 0;
  MediaGeometry media_geometry_;
  bool geometry_query_pending_=false;
  bool geometry_query_dirty_=false;
  void request_video_geometry();
  void video_geometry_reply(const mpv_event* event);
  bool geom_done_ = true;          // this file's adaptation already happened (or is not wanted)
  bool user_sized_ = false;        // the person resized during this media: respect it
  bool auto_portrait_ = false;     // the current portrait shape came from us
  bool in_sizemove_ = false;
  SIZE sizemove_start_{0, 0};
  double pending_ar_ = 0;          // decided while fullscreen: applied on the way out
  SIZE landscape_size_{0, 0};      // last landscape window size, to come back to
  // kind: 0 a shape change, 1 growing into fullscreen, 2 coming back out of it
  struct GeomAnim { bool on = false; RECT from{}, to{}; double t0 = 0, dur = 0.26; int kind = 0; } geom_;
  void begin_fullscreen_motion();
  // fullscreen zoom, drawn by the compositor (see begin_fullscreen_motion)
  struct FullscreenZoom {
    bool entering = true, animate = true;
    double t0 = 0, ready_at = -1, video_ar = 16.0/9.0;
    RECT monitor{}, from{}, to{};  // screen coordinates
  } fz_;
  // fullscreen snap: uniforms of the video shader (see step_fullscreen_transition)
  struct Snap {
    bool on = false, was_on = false, subs_hidden = false, subs_were = true;
    double dark = 0, px = 1, gray = 0;
  } snap_;
  // moving the subtitle: the film pauses and goes grey under it (0..1), and plays on after
  // the grey spreads from the subtitle in a ring of pixel blocks; on release the colour comes back
  // the same way from where it was left. The subtitle itself is lifted a little (sub-scale).
  struct SubFx { bool on = false; double t_in = -1, t_out = -1, ax = .5, ay = .5, bx = .5, by = .5; } subfx_;
  Spring sub_lift_ = [] { Spring k = Spring::rd(0.30, 0.58, 0.0004); k.snap(1.0); return k; }();  // sub-scale
  std::string sub_scale_sent_ = "1.000";
  bool sub_edit_resume_ = false;
  bool step_sub_fx(double t, double dt);
  double scale_for(int cw, int ch, int choice) const;  // DPI + window + choice
  void refresh_ui_scale();
  std::pair<double, double> sub_center_out() const;
  void set_snap(double dark, double px, double gray);
  void snap_subtitles(bool hide);
  void end_snap();
  int logged_fs_stage_ = -1;
  double video_aspect() const;
  void settle_fullscreen_motion();
  void maybe_adapt_window();
  void decide_window();
  std::string video_sync_ = "display-resample", hwdec_;
  bool logged_hw_ = false;
  double hw_log_t_ = 0;
  void adapt_window(double ar);
  void animate_window_to(const RECT& r);
  void step_geom(double t);
  RECT window_rect() const;
  void save_position(bool finished = false);
  void apply_style(bool live = false);
  std::map<std::string, std::string> prop_name_, applied_;
  void run_actions();
  void upload_overlays();
  void remove_overlays();
  void ensure_anim();
  void frame();
  void kick();
  bool fine_timer_ = false;                      // input arrived: draw now, not on the next timer tick
  void on_play_mouse(int x, int y);
  int cursor_kind_ = 0;
  void make_cursors();
  void schedule_wake();
  void toggle_fullscreen();
  void toggle_maximize();
  void begin_window_drag();
  void update_cursor(double t);
  void release_video_window();
  void try_hide_video();
  void shutdown_engine(DWORD max_ms);
  void quit_now();
  bool closing_video_ = false, hide_video_pending_ = false, closing_app_ = false;
  double hide_video_since_ = 0;
  HWND mpv_win_ = nullptr;
  double last_move_t_ = 0;
  void set_title(const std::string& t);
  void set_edge_highlight(bool on);
  void apply_theme_frame();
  void apply_window_corners(bool force = false);
  void key(const std::string& k, bool shift, bool ctrl);
  void mouse_button(int which, bool down, int x, int y);  // coordinates at the event, in Play mode
  POINT cursor_client() const;
  void log(const std::string& msg);
};
