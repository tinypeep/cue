#include "remote_scene.h"

#include <mmsystem.h>

#include <thread>
#include <vector>

#include "../core/remote_sound.h"

namespace {
const std::vector<char>& sound(remote_sound::Kind k) {
  static const std::vector<char> opening = remote_sound::wav(remote_sound::Kind::Opening);
  static const std::vector<char> waiting = remote_sound::wav(remote_sound::Kind::Waiting);
  static const std::vector<char> connected = remote_sound::wav(remote_sound::Kind::Connected);
  return k == remote_sound::Kind::Opening ? opening : k == remote_sound::Kind::Waiting ? waiting : connected;
}
void play(remote_sound::Kind k, bool loop) {
  PlaySoundA(sound(k).data(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT | (loop ? SND_LOOP : 0));
}
}  // namespace

bool RemoteScene::create(HINSTANCE inst) {
  // the sounds are synthesized once, in the background, so opening the scene never waits for them
  std::thread([] { sound(remote_sound::Kind::Opening); sound(remote_sound::Kind::Waiting); sound(remote_sound::Kind::Connected); }).detach();
  host_.on_action = [this](SceneAct a) { if (on_action) on_action({(RemoteAct)a.a, a.index}); };
  host_.on_closed = [this] { stop_sound(); if (on_closed) on_closed(); };
  host_.on_frame = [this](double t) { frame(t); };
  return host_.create(inst);
}

void RemoteScene::show(HWND owner, double s, double) {
  if (host_.visible()) return;
  double now = host_.now();
  t_open_ = now;
  leaving_sent_ = false;
  looping_ = !view.sound;
  host_.show(&view, owner, s, now);
  if (view.sound) play(remote_sound::Kind::Opening, false);
}

void RemoteScene::frame(double t) {
  if (on_tick) on_tick(t);
  if (view.closing() && !leaving_sent_) { leaving_sent_ = true; if (on_leaving) on_leaving(); }
  // the "on" is one pass of the chord; then the chord loops (the light's own period)
  if (!looping_ && view.sound && !view.closing() && t - t_open_ >= remote_sound::kLoopSeconds - 0.02) {
    play(remote_sound::Kind::Waiting, true);
    looping_ = true;
  }
  if (view.done(t) && !view.closing()) view.close(t);  // a phone came in: the scene leaves on its own
}

void RemoteScene::connected(const std::string& device, double) {
  if (!host_.visible()) return;
  double now = host_.now();
  view.connected(device, now);
  if (view.sound) play(remote_sound::Kind::Connected, false);
  looping_ = true;  // nothing more to loop
}

void RemoteScene::set_sound(bool on) {
  view.sound = on;
  if (!host_.visible() || view.closing()) return;
  if (on) { play(remote_sound::Kind::Waiting, true); looping_ = true; }
  else stop_sound();
}

void RemoteScene::stop_sound() {
  PlaySoundA(nullptr, nullptr, 0);
  looping_ = true;
}
