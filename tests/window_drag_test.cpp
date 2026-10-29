#include "../src/ui/play.h"
#include <cassert>
#include <cstdio>

int main() {
  for (bool audio : {false, true}) {
    PlayView view;
    view.in.audio_only = audio;
    view.in.dur = 240;
    view.resize(1280, 720, 2);
    view.reset(0);
    for (int i = 0; i < 60; ++i) view.tick(i / 60.0, 1.0 / 60);
    view.take_actions();
    view.mouse_down(400, 10, 1);
    view.mouse_move(460, 30, 1.1);
    bool began = false;
    for (auto a : view.take_actions()) {
      began |= a.a == PA::WinDragBegin;
      assert(a.a != PA::TogglePause);
    }
    assert(began && view.win_dragging());
    view.handoff_window_drag(1.1);
    assert(!view.win_dragging() && !view.dragging());
    view.mouse_up(460, 30, 1.2); // a late video-child release after the native handoff
    for (auto a : view.take_actions()) assert(a.a != PA::TogglePause && a.a != PA::Fullscreen);
    for (int i = 0; i < 360; ++i) view.tick(2 + i / 60.0, 1.0 / 60);
    assert(!view.win_dragging());
    assert(view.dock_visible() == audio); // normal idle policy returns after the drag
    view.mouse_down(640, 360, 9);
    view.mouse_up(640, 360, 9.1);
    view.tick(9.5, 0.05); // single click commits after the double-click window
    int toggles = 0;
    for (auto a : view.take_actions()) toggles += a.a == PA::TogglePause;
    assert(toggles == 1); // next genuine click is still handled normally
  }
  std::puts("window drag checks passed: native handoff clears custom gesture, late release does not pause, next click works, idle resumes");
}
