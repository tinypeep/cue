// Playback UI: everything drawn over the video. The video is the interface;
// this layer appears only when the mouse moves and leaves after 2 s.
#pragma once
#include <array>
#include <string>
#include <vector>

#include "../core/model.h"
#include "../core/media_info.h"
#include "anim.h"
#include "chrome.h"
#include "gfx.h"
#include "scene_contrast.h"
#include "blur_cloud.h"

struct PlayInput {
  int w = 0, h = 0; double s = 2;
  bool fullscreen = false;
  bool paused = false;
  bool audio_only = false;
  double time = 0, dur = 0, volume = 100;
  bool muted = false;
  std::vector<Track> subs, audios;
  int64_t sid = 0, aid = 0;  // 0 = none
  bool sub_visible = true;
  double sub_delay = 0;
  SubStyle style, default_style;
  SubKind kind = SubKind::None;  // kind of the selected subtitle track
  int sub_lines = 1, sub_chars = 28;
  bool sub_shown = false;  // a subtitle line is on screen right now
  // the lines on screen right now (from the engine); laid out like libass to know where they are
  std::vector<std::string> sub_text;
  double speed = 1.0;
  int ui_scale = 2;         // -1 small, 2 medium (default), 0 large, 1 larger
  bool controls_free = false;
  SceneGrid scene;
  uint64_t scene_revision = 0;
  bool ambient = true;
  double ambient_strength = 0.15;  // 0.05..1, starts soft
  Rgb accent{255, 255, 255};  // atmosphere of the film (already smoothed); white when unknown
  double accent_k = 0;        // 0 = neutral, 1 = full accent
  InfoFields file_info;
  // updates: "" when there is nothing to say; otherwise what the row should read
  std::string update_label;    // "procurar atualizações", "atualizar para 1.2", "baixando 42%"
  bool update_ready = false;   // a newer version is known: the More menu shows it too
  bool loop = false;           // repeat the file forever (music videos, loops)
  bool ended = false;          // stopped on the last frame (the controls stay in view)
  uint64_t file_info_revision = 0;
};

enum class PA {
  TogglePause, SeekRel, SeekAbs, SeekAbsExact, Volume, ToggleMute, Fullscreen, SelectSub, SelectAudio,
  AddSubFile, OpenFile, CloseVideo, Style, StyleLive, StyleDefault, SubDelay, SubVisible, Placement, SubLift,
  WinMin, WinMax, WinClose, WinDragBegin, WinDragEnd, Preview, Speed, SaveFrame, UiScale, Ambient, AmbientStrength,
  OpenFrameFolder, OpenLinkedIn, OpenGitHub, OpenLicenses, FileInfo, ControlsFree, Update, Loop, SubEdit
};
struct PlayAction { PA a; double v = 0; int64_t id = 0; };

struct Layer {
  Canvas c;
  int x = 0, y = 0;
  bool visible = false;
  bool dirty = true;  // contents or position changed since the last upload
  // what was last composed, so unchanged frames are never re-uploaded
  int lx = -1, ly = -1, la = -1;
  const void* lsrc = nullptr;
  uint64_t lgen = 0;
};

class PlayView {
 public:
  enum { L_DOCK, L_PANEL, L_TOAST, L_TIP, L_G0, L_G1, L_G2, L_HINT, L_WIN, L_FLASH, L_BACK, L_IRIS, L_COUNT };
  PlayInput in;
  DockPlace place;
  std::array<Layer, L_COUNT> layers;
  WindowChrome chrome;

  void resize(int w, int h, double s);
  void changed(double now);   // App updated `in`
  void reset(double now);     // new file
  void begin_viewport_transition(double now, bool instant = false);
  void end_viewport_transition(double now);
  bool viewport_hidden() const { return viewport_transition_ && viewport_vis_.v <= 0; }
  void glide_next_resize() { glide_ = true; }  // the next resize animates the dock instead of jumping
  bool tick(double now, double dt);
  std::vector<PlayAction> take_actions() { auto a = std::move(acts_); acts_.clear(); return a; }

  void mouse_move(int x, int y, double now);
  void mouse_down(int x, int y, double now);
  void mouse_up(int x, int y, double now);
  void right_click(int x, int y, double now);
  void wheel(int dir, double now);
  void key(const std::string& k, bool shift, bool ctrl, double now);
  void toast(const std::string& text, const char* icon, double now, bool icon_after = false);
  // "frame salvo ✓ | pasta": stays a little longer, and its folder button opens where the frame went
  void toast_folder(const std::string& text, double now);
  void activity(double now);
  double last_activity() const { return last_activity_; }
  uint64_t dock_signature() const { return dock_revision_; }
  int cursor_kind() const;  // 0 arrow, 1 link, 2 open hand (subtitle), 3 closed hand (dragging it)
  void flash(const std::string& text, double now);  // tiny centred feedback ("+15", "1.25×")
  void flash_pause(bool paused, double now);
  // timeline preview: a small frame near the hovered time (BGRA, opaque)
  void set_thumb(const std::vector<uint32_t>& px, int w, int h, double t);
  int thumb_width() const { return 80 * s_; }
  // previews are decoded at half size and shown doubled, pixel for pixel (lighter, and in the
  // same pixel language as the rest of the UI)
  // Hover images use one quarter of their display width (versus half for colours).
  int thumb_hover_width() const { return std::max(16, (int)std::lround(20 * s_)); }
  int thumb_decode_width() const { return std::max(32, (int)(40 * s_ / 2) * 2); }

  // opening a video: black until the first frame is up, then it opens in a pixel ellipse
  void begin_cover();
  void start_iris(double now);
  bool iris_waiting() const { return iris_ == 1; }
  bool iris_active() const { return iris_ != 0; }
  void prepare(double now) { compose(now); }  // compose the layers right away (before the first tick)

  bool pinned() const;
  Rect subtitle_rect() const { return sub_rect(); }  // where the subtitle sits on screen (client px)
  void toggle_loop(double now);
  void begin_sub_edit(double now);  // a subtitle drag starts: pause + grey, controls step aside
  bool dragging() const;  // a button is held on a control
  bool win_dragging() const { return win_drag_; }  // UI must stay (scrubbing, panel, dragging...)
  void handoff_window_drag(double now); // clear gesture before Windows takes over its move loop
  void set_double_click_time(double seconds) { double_click_time_ = std::clamp(seconds, 0.1, 5.0); }
  bool panel_open() const { return panel_ != Panel::None; }
  bool dock_visible() const { return vis_.v > 0; }
  BlurCloud blur_cloud() const { return {dock_rect(), style_mix_ * ease_out(vis_.v) * ease_out(viewport_vis_.v) * (1 - ease_out(edit_.x))}; }
  // something of the UI is on screen or about to change (otherwise playback updates draw nothing)
  bool needs_frame() const {
    if (vis_.v > 0 || vis_.target > 0 || pvis_.v > 0 || edit_.x > 0 || edit_.target > 0 || iris_ != 0 || sub_settling_ || dragging() || pending_click_ != P_NONE) return true;
    for (auto& L : layers) if (L.dirty) return true;
    return !toast_text_.empty() || last_now_ - flash_t0_ < 0.9;
  }
  double lifted_pos() const;  // sub-pos to apply now (rises above the dock)
  // for previews
  void open_panel_for_preview(int which, double now);
  void set_hover_for_preview(int x, int y, double now) { mouse_move(x, y, now); }
  void begin_sub_drag_for_preview(double now);

 private:
  enum Btn { B_NONE = 0, B_VOL, B_VOLBAR, B_REW, B_PLAY, B_FWD, B_CC, B_AUDIO, B_FS, B_MORE, B_TIMELINE, B_DOCKBG, B_SPEED, B_TIME, B_LOOP, B_COUNT };
  enum class Panel { None, CC, Style, Audio, More, Speed, Context, About, FileInfo };
  enum class RK { Header, Option, Action, Submenu, Slider, Fonts, Stepper, Segmented, Hint, Separator, Links, Toggle,
                  SpeedBar, Chips, Brand, Credit, Profile, Info, CloseHeader };
  enum RowId {
    R_NONE, R_SUB_TOGGLE, R_SUB_TRACK, R_ADD_SUB, R_STYLE, R_DELAY, R_BACK, R_FONTS, R_SIZE, R_TEXT, R_BG, R_OUTLINE, R_LINEGAP, R_TRACK, R_POS,
    R_ASS, R_LINKS, R_AUDIO_TRACK, R_OPEN, R_CLOSE, R_SPEED, R_SPEED_OPT, R_BACK_MORE, R_UISCALE, R_AMBIENT,
    R_FRAME, R_FRAME_CLEAN, R_FS_ROW, R_AUDIO_ROW, R_AMBIENT_STR, R_SPEED_VALUE, R_SPEED_BAR, R_SPEED_CHIPS,
    R_ABOUT, R_LINKEDIN, R_GITHUB, R_LICENSES, R_FILE_INFO, R_DISMISS, R_CONTROLS_FREE, R_UPDATE
  };
  struct Row {
    RK k;
    RowId id = R_NONE;
    std::string label, right;
    bool checked = false;
    bool enabled = true;
    int64_t track = 0;
    double value = 0, minv = 0, maxv = 1;
    int seg = 0;
    double opt = 0;  // value carried by an option (speed)
    std::vector<std::string> segs;
    const char* icon = nullptr;
    // layout (panel-local physical px)
    Rect r;
    std::vector<Rect> parts;  // slider bar, stepper -/+, segments, links
  };

  int w_ = 0, h_ = 0; double s_ = 2;
  std::vector<PlayAction> acts_;
  void act(PA a, double v = 0, int64_t id = 0) { acts_.push_back({a, v, id}); }

  // dock
  int dw_ = 0, dh_ = 0;  // physical
  Spring px_{17, 0.86, 0.3}, py_{17, 0.86, 0.3};
  Fade vis_{0, 0, tok::dock_in, tok::dock_out};
  double last_activity_ = -10;
  int hover_ = B_NONE, focus_ = B_NONE;
  bool kb_ = false;
  bool dock_dirty_ = true;
  Canvas dock_base_;
  uint64_t dock_gen_ = 0, panel_gen_ = 0, toast_gen_ = 0;
  std::array<int64_t, 17> dock_state_{};
  int layout_speed_w_ = -1;  // width of the speed label the dock was laid out for
  double style_mix_ = 0;
  PolishedMotion style_motion_;
  bool style_animating_ = false;
  bool layout_free_ = false;
  bool dock_state_valid_ = false, layout_hours_ = false;
  size_t layout_audio_count_ = 0;
  uint64_t dock_revision_ = 0;
  std::string panel_sig_;
  Spring morph_{26, 1.0, 0.01};  // 0 = play, 1 = pause
  // moving the subtitle: the video pauses and turns grey, the controls step aside (0..1)
  Spring edit_ = Spring::rd(0.26, 1.0, 0.004);
  bool edit_resume_dock_ = false;
  Spring rise_ = Spring::rd(0.42, 0.78, 0.02);  // dock entrance, art px (soft overshoot)
  double last_vis_target_ = 0;
  std::array<double, B_COUNT> hov_{};  // hover fade per dock button
  // magnetic hover: each icon leans (physical px) toward the cursor inside its hitbox
  std::array<Spring, B_COUNT> magx_{}, magy_{};
  bool remaining_ = false;
  bool compact_ = false;
  bool glide_ = false;
  bool viewport_transition_ = false;
  Fade viewport_vis_{1,1,.22,.11};  // in: with the rise spring; out: quick, before the zoom gets going
  double tl_near_ = 0;             // 0..1: the cursor approaches the timeline           // narrow window (portrait video): the dock recomposes
  int still_x_ = -1, still_y_ = -1;  // where the pointer rested when the dock left (jitter guard)
  bool swallow_up_ = false;        // the press closed a panel: its release does nothing
  int ctx_x_ = 0, ctx_y_ = 0;      // context menu anchor
  Rect closing_rect_;              // a closing panel fades where it was
  std::string flash_text_;
  int flash_icon_ = -1;            // -1 text, 0 play, 1 pause
  double flash_t0_ = -10;
  double last_now_ = 0;
  uint64_t flash_gen_ = 0;
  Canvas flash_base_;
  std::vector<double> phov_;      // hover fade per panel row
  double hover_since_ = 0, ack_until_ = 0, content_t0_ = -10;
  bool ack_now_ = false;
  std::string chrome_sig_;
  enum Press { P_NONE, P_VIDEO, P_SUB, P_STRIP };
  Press press_ = P_NONE;
  int press_x_ = 0, press_y_ = 0;
  bool win_drag_ = false;
  bool close_on_release_ = false;
  static constexpr int kStrip = 26;  // top strip (art px) that drags the window
  bool sub_draggable() const;
  Canvas thumb_, thumb_prev_;
  double thumb_xfade_ = 1;  // 0..1: the new frame dissolves over the previous one
  double thumb_t_ = -1, last_preview_req_ = -100;
  uint64_t thumb_gen_ = 0;
  struct Hot { int id; Rect r; };
  std::vector<Hot> hots_;
  Rect tl_;  // timeline track, dock-local
  std::string shown_time_;
  int shown_tl_px_ = -1;
  bool drag_pending_ = false, dragging_dock_ = false;
  int grab_x_ = 0, grab_y_ = 0, down_x_ = 0, down_y_ = 0;
  bool scrubbing_ = false, vol_drag_ = false;
  double scrub_t_ = 0;
  int mx_ = -1, my_ = -1;
  double last_click_t_ = -10;
  int last_click_x_ = 0, last_click_y_ = 0;
  double double_click_time_ = 0.35;
  Press pending_click_ = P_NONE;
  bool second_click_ = false;
  bool click_was_paused_ = false;
  void commit_click();

  // panel
  Panel panel_ = Panel::None;
  int panel_btn_ = B_NONE;
  std::vector<Row> rows_;
  int pw_ = 0, ph_ = 0;
  int phover_ = -1, ppart_ = -1, pfocus_ = -1;
  int slider_row_ = -1;
  Fade pvis_{0, 0, 0.12, 0.10};
  Spring prise_ = Spring::rd(0.36, 0.8, 0.03);  // panel entrance, art px
  bool panel_dirty_ = true;
  Canvas panel_base_;

  // subtitle drag
  bool sub_drag_ = false, sub_hover_ = false;
  double sub_start_pos_ = 0;
  int sub_start_y_ = 0;
  Spring sub_spring_{20, 0.9, 0.02};
  bool sub_settling_ = false;

  // toast
  std::string toast_text_;
  const char* toast_icon_ = nullptr;
  bool toast_icon_after_ = false;
  double toast_t0_ = -10;
  bool toast_dirty_ = false;
  Canvas toast_base_;
  double toast_hold_ = 1.1;      // seconds at full opacity
  bool toast_btn_ = false;       // has the folder button
  bool toast_hot_ = false;       // the cursor is on that button
  double toast_alpha_ = 0;
  Rect toast_rect_, toast_btn_rect_;  // window coords / toast-local
  bool toast_hit(int x, int y, bool* on_btn) const;

  // back to the home: a small arrow in the top-left corner, with the rest of the UI
  Fade back_vis_{0, 0, 0.14, 0.30};
  double back_hov_ = 0;
  bool back_hot_ = false;
  std::string back_sig_;
  Rect back_rect() const;
  bool back_hit(int x, int y) const;

  // tooltip on the timeline
  bool tip_on_ = false;
  std::string tip_text_;
  double tip_t_ = 0;
  Spring tipx_{30, 0.95, 0.3};   // the preview glides after the cursor
  Fade tipvis_{0, 0, 0.12, 0.09};
  std::string tip_ts_;           // last time shown, kept while it fades out
  bool tip_want() const;
  void tip_size(int& W, int& H) const;

  // "legenda" switch in the CC panel
  Spring sw_ = Spring::rd(0.3, 0.66, 0.01);

  // layout + helpers
  void layout_dock();
  Rect dock_rect() const;  // physical, current animated position
  void anchor_pos(DockAnchor a, double& x, double& y) const;
  void place_target(double& x, double& y) const;
  int hit_dock(int x, int y) const;  // window coords
  int dock_btn_at(int lx, int ly) const;
  void render_dock();
  void render_panel();
  void compose(double now);
  double time_at(int x) const;
  void seek_scrub(int x, bool final);
  void set_volume_from(int x);
  void activate(int btn, double now);
  std::vector<int> focus_order() const;

  void open_panel(Panel p, int btn, double now);
  void close_panel();
  void build_rows();
  bool switch_on() const { return panel_ == Panel::More ? in.ambient : (in.sid != 0 && in.sub_visible); }
  void layout_rows();
  int slider_label_w() const;
  bool icon_grid() const;
  Rect panel_rect() const;
  int hit_panel(int x, int y, int* part) const;
  void panel_click(int row, int part, int x, double now);
  void panel_adjust(int row, int dir, double now);
  void slider_set(int row, int x, bool live);
  bool selectable(const Row& r) const;
  void set_speed(double v);  // rounded to 0.05, 0.25..3; tells the engine and redraws
  void style_changed(bool live);

  Rect sub_rect() const;              // where the subtitle is drawn right now
  Rect sub_rect_at(double pos) const;
  Rect sub_band() const;
  bool sub_hit(int x, int y) const;  // over the subtitle's actual lines (where the hand appears)
  bool sub_line_rects(double pos, std::vector<Rect>& out) const;  // false when the layout is not known
  mutable int style_y_ = -1, style_x_ = 0;
  void update_sub_layers();
  double snap_sub(double pos) const;
  double last_lift_ = -1;
  PolishedMotion subtitle_lift_;
  double wanted_subtitle_lift() const;

  int iris_ = 0;  // 0 none, 1 covered (waiting for the picture), 2 opening
  double iris_t0_ = 0, iris_drawn_ = -1;
  int iris_frame_ = -1;
  void render_iris(double k);
};
