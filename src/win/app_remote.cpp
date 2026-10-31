// App: the phone remote. The server (remote_server) and the pairing scene (remote_scene) are
// independent pieces; this is where they meet the player. Every command from a phone goes
// through the same actions as the dock, so the PC shows the same feedback ("+15", the pause
// icon, "subtitles on") as if the person had clicked.
#include "app.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

#include "../core/i18n.h"
#include "../core/json.h"
#include "../core/remote_proto.h"
#include "../core/util.h"

namespace {
constexpr Rgb kCueGlow{160, 80, 143};  // the purple of Cue's icon, when the film has no light yet
std::string hex_color(Rgb c) {
  char b[8];
  std::snprintf(b, sizeof b, "#%02x%02x%02x", c.r, c.g, c.b);
  return b;
}
}  // namespace

bool App::remote_start() {
  if (remote_.running()) return true;
  remote_default_folders();
  std::string err;
  if (!remote_.start(hwnd_, WM_APP_REMOTE, exe_dir() + L"\\fonts\\CueOmni.ttf", &err)) {
    log("remote: " + err);
    return false;
  }
  log("remote on port " + std::to_string(remote_.port()));
  remote_.set_tokens(store_.remote_tokens);
  remote_boot();
  remote_roots();
  remote_progress();
  remote_publish();
  SetTimer(hwnd_, T_REMOTE, 500, nullptr);
  return true;
}

void App::remote_stop(bool tell_phones) {
  remote_.stop(tell_phones);
  if (tell_phones) { store_.remote_tokens.clear(); remote_.set_tokens({}); save_state(); }
  remote_devices_.clear();
  if (scene_.visible()) scene_.view.phones.clear();
  if (!remote_timer_) KillTimer(hwnd_, T_REMOTE);
  log("remote off");
}

// The page's words, in Cue's language: every "remote.*" text and the decimal separator.
void App::remote_boot() {
  std::string o = "window.CUE={\"lang\":" + json_quote(i18n::tag(i18n::language())) + ",\"strings\":{";
  bool first = true;
  for (int i = 0; i < str::kCount; ++i) {
    std::string key = i18n::key_name((str::Id)i);
    if (key.rfind("remote.", 0) != 0 && key != "number.decimal") continue;
    o += (first ? "" : ",") + json_quote(key) + ":" + json_quote(tr((str::Id)i));
    first = false;
  }
  o += "}};";
  // never let a text close the page's <script>
  for (size_t k = o.find("</"); k != std::string::npos; k = o.find("</", k + 3)) o.replace(k, 2, "<\\/");
  remote_.set_boot(o);
}

void App::remote_default_folders() {
  if (store_.remote_folders_set) return;
  auto d = default_media_folders();  // Videos, Downloads, Desktop
  store_.remote_folders.clear();
  for (size_t i = 0; i < d.size(); ++i)
    if (!d[i].empty()) store_.remote_folders.push_back({d[i], i < 2});  // the desktop waits to be chosen
  store_.remote_folders_set = true;
  save_state();
}

void App::remote_roots() {
  std::vector<RemoteServer::Root> roots;
  for (auto& f : store_.remote_folders)
    if (f.on && GetFileAttributesW(widen(f.path).c_str()) != INVALID_FILE_ATTRIBUTES)
      roots.push_back({folder_display_name(f.path), f.path});
  remote_.set_roots(std::move(roots));
}

std::vector<RemoteChip> App::remote_chips() const {
  std::vector<RemoteChip> chips;
  for (auto& f : store_.remote_folders) chips.push_back({folder_display_name(f.path), f.on});
  return chips;
}

void App::remote_progress() {
  std::vector<std::pair<std::string, RemoteServer::Progress>> items;
  for (auto& r : store_.recents) {
    RemoteServer::Progress p;
    p.dur = r.dur;
    double rp = store_.resume_point(r.path);
    p.pos = rp > 0 ? rp : 0;
    p.seen = rp <= 0 && r.pos <= 0 && r.dur > 0;  // finished: Cue starts it over next time
    items.push_back({r.path, p});
  }
  remote_.set_progress(std::move(items));
}

bool App::remote_path_ok(const std::string& path) const {
  if (path.empty() || !remote::is_playable_name(path)) return false;
  bool inside = false;
  for (auto& f : store_.remote_folders)
    if (f.on && remote_path_allowed(f.path, path)) inside = true;
  if (!inside) return false;  // never touch an untrusted UNC/device path, even to ask for attributes
  DWORD a = GetFileAttributesW(widen(path).c_str());
  return inside && a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

void App::remote_publish() {
  if (!remote_.running()) return;
  remote::State st;
  const PlayInput& in = play_.in;
  st.playing_view = mode_ == Mode::Play && loaded_;
  st.volume = in.volume;
  st.muted = in.muted;
  if (st.playing_view) {
    st.file = file_name(path_);
    st.pos = in.time;
    st.dur = in.dur;
    st.speed = in.speed;
    st.paused = in.paused || eof_;
    st.ended = eof_;
    st.audio_only = audio_only_;
    st.has_subs = !in.subs.empty();
    st.subs_on = in.sid != 0 && in.sub_visible;
    st.sub_delay = in.sub_delay;
    st.sub_size = in.style.size;
    st.sub_pos = in.style.pos;
    st.sub_pos_default = in.default_style.pos;
    st.sub_pos_min = kSubPosMin;
    st.sub_pos_max = kSubPosMax;
    st.audio_tracks = (int)in.audios.size();
    for (size_t i = 0; i < in.audios.size(); ++i) if (in.audios[i].id == in.aid) st.audio_index = (int)i;
    if (amix_) { st.audio_index = -1; st.audio_mix = (int)in.amix_ids.size(); }  // several at once
    st.next_auto = next_auto_ && !next_cancelled_ && !store_.loop && remote_timer_ != -1;
    st.next = next_ep_.empty() ? "" : file_name(next_ep_);
  }
  st.timer = remote_timer_;
  st.timer_left = remote_timer_ > 0 ? std::max(0.0, remote_timer_end_ - now()) : 0;
  st.color = hex_color(in.accent_k > 0.2 && st.playing_view && !audio_only_ ? in.accent : kCueGlow);
  remote_.set_state(remote::state_json(st));
}

void App::remote_tick() {
  double t = now();
  if (remote_timer_ > 0 && t >= remote_timer_end_) {
    // the sleep timer: the film pauses, quietly
    remote_timer_ = 0;
    if (mode_ == Mode::Play && loaded_ && !play_.in.paused && !eof_) {
      play_.remote_toggle_pause(t);
      run_actions();
      ensure_anim();
    }
    say(tr(str::remote_goodnight));
  }
  if (remote_.running()) {
    if (remote_.clients() > 0) {  // with no phone there, nothing to tell (a phone that comes in is told at once)
      if (++remote_ticks_ % 10 == 0) remote_progress();  // saved positions, every few seconds
      remote_publish();
    }
  } else if (!remote_timer_) {
    KillTimer(hwnd_, T_REMOTE);
  }
}

void App::remote_events() {
  for (auto& e : remote_.take_events()) {
    if (e.kind == RemoteServer::Event::Command) { remote_command(e.cmd); continue; }
    if (e.kind == RemoteServer::Event::Paired) {
      store_.remote_tokens.push_back(e.token);
      while (store_.remote_tokens.size() > remote::kMaxTokens) store_.remote_tokens.erase(store_.remote_tokens.begin());
      remote_.set_tokens(store_.remote_tokens);
      save_state();
    }
    if (e.kind == RemoteServer::Event::Disconnected) {
      std::string device = e.device.empty() ? std::string(tr(str::remote_phone)) : e.device;
      auto it = std::find(remote_devices_.begin(), remote_devices_.end(), device);
      if (it != remote_devices_.end()) remote_devices_.erase(it);
      if (scene_.visible()) scene_.view.phones = remote_devices_;
      continue;
    }
    if (e.kind == RemoteServer::Event::Paired || e.kind == RemoteServer::Event::Connected) {
      std::string device = e.device.empty() ? std::string(tr(str::remote_phone)) : e.device;
      remote_devices_.push_back(device);
      if (scene_.visible()) scene_.view.phones = remote_devices_;
      double t = now();
      if (scene_.visible()) scene_.connected(device, t);
      else if (e.kind == RemoteServer::Event::Paired || t - remote_toast_at_ > 300) {
        // phones reconnect whenever their screen wakes: the PC says so once in a while, not each time
        say(i18n::format(str::remote_connected, {{"device", device}}));
      }
      remote_toast_at_ = t;
      remote_progress();
      remote_publish();
    }
  }
}

void App::remote_command(const remote::Command& c) {
  double t = now();
  PlayInput& in = play_.in;
  const std::string& k = c.c;
  if (k == "open") {
    if (remote_path_ok(c.path)) open(c.path);
    return;
  }
  if (k == "timer") {
    int m = (int)std::lround(c.v);
    if (m != -1 && (m < 0 || m > 600)) m = 0;
    remote_timer_ = m;
    remote_timer_end_ = m > 0 ? t + m * 60.0 : 0;
    if (m) SetTimer(hwnd_, T_REMOTE, 500, nullptr);
    remote_publish();
    return;
  }
  bool playing = mode_ == Mode::Play && loaded_;
  if (k == "peek") {  // the wheel is turning: a glance at where it will land, for the phone only
    if (playing && !audio_only_ && std::isfinite(c.v)) remote_peek(c.v);
    return;
  }
  if (!playing) { remote_publish(); return; }
  if (k == "toggle") {
    scene_paused_ = false;  // the phone decides now
    play_.remote_toggle_pause(t);
  } else if (k == "skip") {
    double v = std::clamp(c.v, -600.0, 600.0);
    play_.remote_act(PA::SeekRel, v);
    play_.flash((v > 0 ? "+" : "") + std::to_string((int)std::lround(v)), t);
  } else if (k == "seek" && c.has_v) {
    double v = std::clamp(c.v, 0.0, std::max(0.0, in.dur - 0.5));
    play_.remote_act(PA::SeekAbsExact, v);
    play_.flash(fmt_time(v), t);
  } else if (k == "vol" && c.has_v) {
    double v = std::clamp(std::round(c.v), 0.0, kVolMax);
    play_.remote_act(PA::Volume, v);
    play_.flash(std::to_string((int)v) + "%", t);
  } else if (k == "what") {
    // "what did they say?": ten seconds back, with subtitles until we are past that line again
    play_.remote_act(PA::SeekRel, -10);
    play_.flash("-10", t);
    bool showing = in.sid != 0 && in.sub_visible;
    if (!in.subs.empty() && !showing && what_until_ < 0) {
      what_until_ = in.time;
      what_sid_ = in.sid;
      what_vis_ = in.sub_visible;
      if (in.sid == 0) mpv_.set_async("sid", std::to_string(in.subs[0].id));
      mpv_.set_flag_async("sub-visibility", true);
    }
    if (in.paused || eof_) play_.remote_toggle_pause(t);
  } else if (k == "sub") {
    end_what(false);
    play_.remote_act(PA::SubVisible, c.v > 0 ? 1 : 0);
  } else if (k == "subdelay" && c.has_v) {
    double v = std::clamp(std::round(c.v * 10) / 10, -30.0, 30.0);
    if (std::fabs(v - in.sub_delay) > 0.001) play_.remote_act(PA::SubDelay, v - in.sub_delay);
  } else if (k == "subsize" && c.has_v) {
    in.style.size = std::clamp((int)std::lround(c.v), 28, 72);
    play_.remote_act(PA::Style);
    play_.changed(t);
  } else if (k == "subpos" && c.has_v) {
    in.style.pos = std::clamp(c.v, kSubPosMin, kSubPosMax);
    play_.remote_act(PA::Style);
    play_.changed(t);
  } else if (k == "audio" && c.has_v) {
    int i = (int)c.v;
    if (i == -1 && in.audios.size() >= 2) play_.remote_act(PA::AudioMix, 1, 0);  // all of them
    else if (i >= 0 && i < (int)in.audios.size()) play_.remote_act(PA::SelectAudio, 0, in.audios[i].id);
  } else if (k == "next") {
    if (!next_ep_.empty()) open(next_ep_);
    return;
  } else {
    return;
  }
  run_actions();
  ensure_anim();
  remote_publish();
}

void App::end_what(bool restore) {
  if (what_until_ < 0) return;
  what_until_ = -1;
  if (!restore || !mpv_ok_) return;
  // give the subtitles back exactly as they were (none, or hidden), without saving it as a choice
  what_restored_at_ = now();
  mpv_.set_flag_async("sub-visibility", what_vis_);
  if (what_sid_ == 0) mpv_.set_async("sid", "no");
}

// Which file plays after this one: the next episode, by its number, else by name.
void App::find_next_episode() {
  next_ep_.clear();
  next_auto_ = false; play_.in.has_next = false;
  if (path_.empty() || path_.find("://") != std::string::npos) return;
  std::error_code ec;
  std::filesystem::path p(widen(path_));
  std::filesystem::path dir = p.parent_path();
  std::vector<std::string> names;
  for (std::filesystem::directory_iterator it(dir, std::filesystem::directory_options::skip_permission_denied, ec), end;
       !ec && it != end && names.size() < 4000; it.increment(ec)) {
    if (it->is_regular_file(ec)) names.push_back(narrow(it->path().filename().wstring()));
  }
  std::string next = remote::next_episode(narrow(p.filename().wstring()), names);
  next_auto_ = !audio_only_ && !next.empty() && remote::same_series(narrow(p.filename().wstring()), next);
  if (next.empty()) next = remote::next_file(narrow(p.filename().wstring()), names);
  if (!next.empty()) next_ep_ = narrow((dir / widen(next)).wstring());
  play_.set_next_details(file_name(next_ep_));
  next_thumb_job_.reset(); next_thumb_requested_ = false;
  play_.in.has_next = !next_ep_.empty();

}

// Capture the destination before idle notifications can clear discovery state.
bool App::queue_next_episode() {
  bool allowed = !next_cancelled_ && !store_.loop && remote_timer_ != -1 &&
      next_auto_ && mode_ == Mode::Play;
  if (next_advance_.pending()) return allowed;
  if (!next_advance_.arm(next_ep_, allowed)) return false;
  SetTimer(hwnd_, T_NEXT, next_auto_ ? 1 : 2500, nullptr);
  return true;
}

// The phone's timeline preview: the same frames as the PC's hover, from the same helper.
void App::remote_peek(double t) {
  if (play_.in.dur <= 0) return;
  remote_peek_want_ = std::clamp(t, 0.0, play_.in.dur);
  int shown = thumb_shown_;
  thumb_request(remote_peek_want_);
  thumb_shown_ = shown;  // the PC's own hover keeps its own picture
  remote_peek_send();
}

void App::remote_peek_send() {
  if (remote_peek_want_ < 0 || thumb_cache_.empty()) return;
  int k = thumb_key(remote_peek_want_);
  auto it = thumb_cache_.lower_bound(k);
  if (it == thumb_cache_.end() || (it != thumb_cache_.begin() && it->first != k && k - std::prev(it)->first < it->first - k)) --it;
  thumb_cache_.touch(it->first);
  if (it->first != remote_peek_sent_) {
    remote_peek_sent_ = it->first;
    remote_.set_peek(it->first * thumb_step(), it->second.px, it->second.w, it->second.h);
  }
  if (it->first == k) remote_peek_want_ = -1;
}

void App::open_remote_scene() {
  if (scene_.visible()) return;
  bool on = remote_start();
  if (store_.remote_on != on) { store_.remote_on = on; save_state(); }
  remote_default_folders();
  remote_roots();
  RemoteView& v = scene_.view;
  v.chips = remote_chips();
  v.phones = remote_devices_;
  v.sound = store_.remote_sound;
  v.glow = play_.in.accent_k > 0.2 && mode_ == Mode::Play && loaded_ && !audio_only_ ? play_.in.accent : kCueGlow;
  v.problem.clear();
  v.address.clear();
  v.code.clear();
  remote_pair_deadline_ = 0;
  std::string ip = on ? lan_address() : "";
  if (!on) {
    v.problem = tr(str::remote_failed);
    v.set_link("");
  } else if (ip.empty()) {
    v.problem = tr(str::remote_no_network);
    v.set_link("");
  } else {
    remote_code_ = new_pairing_code();
    remote_pair_deadline_ = now() + remote::kPairLifetime;
    if (remote_code_.empty()) { v.problem = tr(str::remote_failed); v.set_link(""); }
    remote_.set_pairing(!remote_code_.empty(), remote_code_);
    v.address = ip + ":" + std::to_string(remote_.port());
    v.code = remote_code_;
    if (!remote_code_.empty()) v.set_link("http://" + v.address + "/#c=" + remote_code_);
  }
  // the film waits behind the scene, in black and white, and shows through a little more
  bool film = mode_ == Mode::Play && loaded_ && !audio_only_;
  v.veil = film ? 0.8 : 0.95;
  scene_paused_ = false;
  if (mode_ == Mode::Play && loaded_ && !play_.in.paused && !eof_) { request_pause(true); scene_paused_ = true; }
  gray_target_ = film ? 1 : 0;
  gray_last_ = -1;
  scene_.show(hwnd_, s_, now());
  log("remote pairing open");
}

// The colour drains out as the scene opens and comes back as it leaves (the engine's own
// saturation control: the paused picture is redrawn, nothing is decoded again).
void App::scene_gray(double t) {
  double dt = gray_last_ < 0 ? 0 : std::clamp(t - gray_last_, 0.0, 0.1);
  gray_last_ = t;
  double step = reduce_motion() ? 1.0 : dt / 0.35;
  gray_ = gray_ < gray_target_ ? std::min(gray_target_, gray_ + step) : std::max(gray_target_, gray_ - step);
  double k = gray_ < 0.5 ? 2 * gray_ * gray_ : 1 - 2 * (1 - gray_) * (1 - gray_);
  int sat = -(int)std::lround(100 * k);
  if (sat != gray_sent_ && mpv_ok_) { mpv_.set_double_async("saturation", sat); gray_sent_ = sat; }
}

void App::remote_scene_action(RemoteAction a) {
  double t = now();
  switch (a.a) {
    case RemoteAct::Close: scene_.close(t); break;
    case RemoteAct::ToggleFolder:
      if (a.index >= 0 && a.index < (int)store_.remote_folders.size()) {
        store_.remote_folders[a.index].on = !store_.remote_folders[a.index].on;
        save_state();
        remote_roots();
        scene_.view.chips = remote_chips();
        scene_.view.set_size(scene_.view.w, scene_.view.h, scene_.view.s);
      }
      break;
    case RemoteAct::AddFolder: {
      std::string dir = pick_folder_dialog(hwnd_);
      if (!dir.empty()) {
        bool known = false;
        for (auto& f : store_.remote_folders)
          if (remote::path_within(f.path, dir) && remote::path_within(dir, f.path)) { f.on = true; known = true; }
        if (!known && store_.remote_folders.size() < 8) store_.remote_folders.push_back({dir, true});
        save_state();
        remote_roots();
        scene_.view.chips = remote_chips();
        scene_.view.set_size(scene_.view.w, scene_.view.h, scene_.view.s);
      }
      break;
    }
    case RemoteAct::Sound:
      store_.remote_sound = !store_.remote_sound;
      save_state();
      scene_.set_sound(store_.remote_sound);
      break;
    case RemoteAct::TurnOff:
      remote_stop(true);
      store_.remote_on = false;
      save_state();
      scene_.close(t);
      break;
    case RemoteAct::None: break;
  }
}
