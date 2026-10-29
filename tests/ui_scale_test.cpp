#include "../src/core/ui_scale.h"
#include "../src/core/store.h"
#include "../src/ui/play.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>

int main() {
  int choice = -1;
  for (int expected : {2, 0, 1, 1}) { choice = next_ui_size(choice, 1); assert(choice == expected); }
  for (int expected : {0, 2, -1, -1}) { choice = next_ui_size(choice, -1); assert(choice == expected); }
  assert(ui_scale(96,1280,720,0) == 2);
  assert(ui_scale(96,1280,720,2) == 1.5);
  assert(ui_scale(96,1280,720,1) == 3);
  for (unsigned dpi : {96, 120, 144, 168, 192, 240, 288}) {
    for (int w : {640, 960, 1280, 1920, 3840}) {
      int h = w * 9 / 16;
      double small = ui_scale(dpi,w,h,-1), medium = ui_scale(dpi,w,h,2);
      double large = ui_scale(dpi,w,h,0), larger = ui_scale(dpi,w,h,1);
      assert(small <= medium && medium <= large && large <= larger);
      assert(std::fmod(medium * 2, 1.0) == 0);  // whole or half art px
    }
  }
  // full screen: bigger than the window, but less than in proportion to the picture
  for (int choice : {-1, 2, 0, 1}) {
    double win = ui_scale(96, 1766, 993, choice), full = ui_scale(96, 2560, 1440, choice);
    assert(full >= win && full <= win * 2560 / 1766);
    double win4k = ui_scale(144, 2649, 1490, choice), full4k = ui_scale(144, 3840, 2160, choice);
    assert(full4k >= win4k && full4k <= win4k * 3840 / 2649 + 0.25);
  }
  // in step with the window up to a roomy one: 1280 -> 1600 px grows like the window
  assert(ui_scale(96, 1600, 900, 0) == 2.5 && ui_scale(96, 1280, 720, 0) == 2);
  // width leads: a 2.39:1 window gets the controls of the 16:9 window of the same width
  for (int w : {1280, 1920, 2560}) assert(ui_scale(96, w, (int)(w / 2.39), 2) == ui_scale(96, w, w * 9 / 16, 2));
  // a small window still holds the size down; the reference size is the base
  assert(ui_scale(192, 700, 394, 1) <= 2.5);
  assert(ui_base_scale(96, 2) == 1.5 && ui_base_scale(96, 1) == 3);
  auto path = std::filesystem::temp_directory_path() / "cue-ui-scale-test.json";
  for (int stored : {-1,0,2,1}) {
    Store before; before.ui_scale = stored; assert(before.save(path));
    Store after; assert(after.load(path)); assert(after.ui_scale == stored);
  }
  std::filesystem::remove(path); std::filesystem::remove(path.string()+".bak");
  for (bool audio : {false,true}) for (double scale : {1.,1.5,2.,2.5,3.,3.5,4.5}) for (int chip=0;chip<4;++chip) {
    PlayView p; p.in.audio_only=audio; p.in.dur=120; p.resize(2560,1440,scale); p.reset(1);
    const auto subtitle = p.in.style;
    p.open_panel_for_preview(3,1);
    for(int i=1;i<=45;++i) p.tick(1+i/60.,1/60.);
    p.take_actions();
    const auto& panel=p.layers[PlayView::L_PANEL];
    assert(panel.visible && panel.x>=0 && panel.x+panel.c.w<=2560);
    int x=panel.x+(int)(4*scale+(chip+.5)*(panel.c.w-8*scale)/4);
    int y=panel.y+(int)((audio?80:120)*scale);  // the size selector: after open, close and (with video) the light rows
    p.mouse_move(x,y,2);p.mouse_down(x,y,2);p.mouse_up(x,y,2.01);
    bool found=false;
    for (auto& a:p.take_actions()) if(a.a==PA::UiScale) {assert((int)a.v==ui_size_choice(chip));found=true;}
    assert(found); assert(p.in.style==subtitle);
  }
  std::puts("UI scale: four choices, DPI bounds, stable resize, saved preferences, all menu hitboxes at six scales in audio/video passed");
}
