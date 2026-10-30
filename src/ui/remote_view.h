// The remote's pairing scene: drawn over the player (or the home) while a phone connects.
// Almost no words: the QR code in the middle with the film's light breathing behind it and
// sonar rings leaving it, Cue's eyes peeking over the code, the address and code underneath for
// typing by hand, and the shared folders as chips at the bottom. When a phone arrives the code
// gives way to happy eyes and the phone's name, then the scene leaves on its own.
#pragma once
#include <string>
#include <vector>

#include "../core/qr.h"
#include "anim.h"
#include "face.h"
#include "gfx.h"
#include "scene_view.h"

struct RemoteChip { std::string label; bool on = true; };
enum class RemoteAct { None, Close, ToggleFolder, AddFolder, Sound, TurnOff };
struct RemoteAction { RemoteAct a = RemoteAct::None; int index = -1; };

struct RemoteView : SceneView {
  int w = 0, h = 0;
  double s = 2;
  std::string address, code;     // "192.168.0.12:7878", "4812"
  std::string problem;           // instead of the code: "no network", "could not start"
  std::vector<RemoteChip> chips;
  std::vector<std::string> phones;  // connected right now, by name (shown at the top)
  bool sound = true;
  Rgb glow{160, 80, 143};        // the film's light (Cue's own purple when there is none)
  double veil = 0.95;            // how much of what is behind it the scene covers (less over a paused film)

  void set_size(int w_, int h_, double s_) override;
  void set_link(const std::string& url);  // what the QR code opens
  void open(double now) override;
  void close(double now) override;        // fades out; closed() says when it is gone
  bool closed(double now) const override { return close_t0_ > 0 && now - close_t0_ >= kLeave; }
  bool closing() const override { return close_t0_ > 0; }
  void connected(const std::string& device, double now);  // the code gives way to the phone
  bool done(double now) const { return conn_t0_ > 0 && now - conn_t0_ > 3.2; }
  static constexpr double kEnter = 0.42, kLeave = 0.38;  // the iris opening and closing
  bool tick(double now, double dt) override;
  void render(Canvas& c, double now) override;
  RemoteAction mouse_move(int x, int y, double now);
  RemoteAction mouse_down(int x, int y, double now);
  void mouse_leave(double now) { hover_ = -1; (void)now; }
  RemoteAction key(const std::string& k, bool shift, double now);
  int cursor_kind() const { return hover_ >= 0 ? 1 : 0; }  // 1: a link hand over controls
  // as a scene
  void pointer_move(int x, int y, double now) override { mouse_move(x, y, now); }
  void pointer_leave(double now) override { mouse_leave(now); }
  SceneAct pointer_down(int x, int y, double now) override { auto r = mouse_down(x, y, now); return {(int)r.a, r.index}; }
  SceneAct key_down(const std::string& k, bool shift, double now) override { auto r = key(k, shift, now); return {(int)r.a, r.index}; }
  int cursor() const override { return cursor_kind(); }
  bool fast(double now) const override { return now - open_t0_ < 1.5 || conn_t0_ > 0 || close_t0_ > 0; }
  bool draggable(int x, int y) const override { return hit(x, y) < 0 && !tile_.contains(x, y); }

 private:
  enum : int { kClose = 1000, kAdd = 1001, kSound = 1002, kOff = 1003 };  // chips are 0..n-1
  QrCode qr_;
  Face face_;
  std::string device_;
  double open_t0_ = -10, close_t0_ = -1, conn_t0_ = -1;
  int hover_ = -1, focus_ = -1;
  bool kb_ = false;
  struct Hot { int id; Rect r; };
  std::vector<Hot> hots_;
  Rect tile_;
  int module_ = 6, quiet_ = 3;
  int chip_label_ = 110;  // art px a folder's name may take
  int addr_gap_ = 10;     // art px between the code and the address
  bool landed_ = false;   // the eyes dropped onto the code (the opening)
  int hops_ = 0;          // a phone came in: the eyes' little celebration, hop by hop
  void iris(Canvas& c, double q, bool opening) const;
  void layout();
  int hit(int x, int y) const;
  std::vector<double> hov_;
  double hov(int id) const;
  int hov_index(int id) const;
};
