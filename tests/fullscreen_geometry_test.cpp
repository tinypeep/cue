#include "../src/core/fullscreen_geometry.h"
#include "../src/core/window_policy.h"
#include "../src/core/viewport_gate.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace fullscreen_motion;
static bool near(double a,double b) { return std::abs(a-b)<.00001; }
int main() {
  // A restored source with different bars must show exactly the fullscreen video
  // before zooming out. Test several screen shapes, negative monitor origins and
  // windows deliberately resized away from the media's aspect.
  for(Rect screen : {Rect{0,0,1920,1080},Rect{-3440,120,0,1560},Rect{0,-2160,3840,0}})
    for(double media : {16./9,2.39,4./3,9./16,1.})
      for(double window_ar : {16./9,2.39,4./3,9./16,1.}) {
        Rect window{240,160,1240,160+1000/window_ar};
        Rect mapped=map_viewport(window,screen,media);
        assert(near(width(mapped)/height(mapped),window_ar));
        Rect a=fit(mapped,media),b=fit(screen,media);
        assert(near(a.left,b.left)&&near(a.top,b.top)&&near(a.right,b.right)&&near(a.bottom,b.bottom));
        // A captured fullscreen frame maps back to a normal window with uniform
        // scale on both axes; it cannot squeeze the video at any point.
        for(int i=0;i<=60;++i) {
          double t=i/60.;
          Rect viewport{mapped.left+(window.left-mapped.left)*t,
                        mapped.top+(window.top-mapped.top)*t,
                        mapped.right+(window.right-mapped.right)*t,
                        mapped.bottom+(window.bottom-mapped.bottom)*t};
          Rect frozen=map_viewport(screen,viewport,media);
          assert(near(width(frozen)/width(screen),height(frozen)/height(screen)));
          Rect visible=fit(frozen,media),expected=fit(viewport,media);
          assert(near(visible.left,expected.left)&&near(visible.top,expected.top));
          assert(near(width(visible),width(expected))&&near(height(visible),height(expected)));

        }
      }
  assert(!restore_saved_window(false,1600,670));
  assert(!restore_saved_window(false,1280,960));
  assert(!restore_saved_window(false,720,1280));
  assert(restore_saved_window(false,1280,720));
  assert(restore_saved_window(false,1280,721));
  assert(!restore_saved_window(true,1280,720));
  assert(!viewport_ready_for_controls(1920,1080,1280,720,true,.01,0));
  assert(!viewport_ready_for_controls(1920,1080,1920,1080,false,.01,0));
  assert(viewport_ready_for_controls(1920,1080,1920,1080,true,.01,0)); // compositor fence replaces the arbitrary delay
  assert(zoom>=.30 && zoom<=.35);
  assert(spring_curve(0)==0 && spring_curve(1)==1);
  double peak=0;
  for(int i=0;i<=1000;++i) peak=std::max(peak,spring_curve(i/1000.));
  assert(peak>1.005 && peak<1.025); // visible, restrained spring rather than a flat ease-out
  assert(std::abs(spring_curve(.9999)-spring_curve(1))<.000001);
  // Different refresh rates sample the same analytic motion and land exactly.
  for(int hz : {60,120,144}) {
    double last=0;
    for(int frame=0;frame<=hz;++frame) {
      double k=frame/(double)hz;
      double value=spring_curve(k);
      assert(value>=0 && value<1.025);
      if(k<.4) assert(value>=last);
      last=value;
    }
    assert(last==1);
  }
  puts("Fullscreen: live viewport mapping, analytic spring overshoot/landing at 60/120/144 Hz, renderer gate and 16:9 home passed");
}
