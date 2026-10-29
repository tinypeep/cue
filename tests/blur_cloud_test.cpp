#include "../src/ui/play.h"
#include "../src/core/blur_geometry.h"
#include "../src/core/resize_hit.h"
#include <cassert>
#include <cstdio>

int main() {
  for (int dpi : {96,144,192}) {
    int s=dpi/48, w=640*s, h=360*s;
    assert(player_resize_hit(0,h/2,w,h,dpi)==10);
    assert(player_resize_hit(w-1,h/2,w,h,dpi)==11);
    assert(player_resize_hit(w/2,1,w,h,dpi)==12);
    assert(player_resize_hit(w/2,h-1,w,h,dpi)==15);
    assert(player_resize_hit(10*dpi/96,2,w,h,dpi)==13);
    assert(player_resize_hit(w-2,10*dpi/96,w,h,dpi)==14);
    assert(player_resize_hit(2,h-10*dpi/96,w,h,dpi)==16);
    assert(player_resize_hit(w-10*dpi/96,h-2,w,h,dpi)==17);
    assert(player_resize_hit(w/2,h/2,w,h,dpi)==1);
  }
  for (int width : {240,640}) {
    PlayView p; p.in.time=30; p.in.dur=300;
    double t=1; p.resize(width*2,720,2); p.reset(t);
    auto step=[&](int n){while(n--){t+=1./60;p.tick(t,1./60);}};
    step(30); int boxed=p.layers[PlayView::L_DOCK].c.w;
    p.in.controls_free=true;p.changed(t);
    step(10); auto mid=p.blur_cloud();
    assert(mid.alpha>0&&mid.alpha<1);
    if(width>300) assert(mid.controls.w>boxed&&mid.controls.w<1088);
    step(35); auto cloud=p.blur_cloud(); assert(cloud.alpha==1);
    auto& layer=p.layers[PlayView::L_DOCK];
    int white=0, shadow=0;
    for(auto px:layer.c.px) {if((px>>24)==255){assert(px==0xffffffff);++white;} if(px && (px&0xffffff)==0)++shadow;}
    assert(white>50&&shadow>50);
    p.changed(t); auto stable=p.dock_signature(); p.in.scene_revision++;p.in.accent={255,0,0};p.in.accent_k=1;p.changed(t);
    assert(p.dock_signature()==stable); // no scene-driven glyph recolouring
    p.in.controls_free=false;p.changed(t);step(8);
    assert(p.blur_cloud().alpha>0&&p.blur_cloud().alpha<1);
    // Reversing the switch starts from the current state, without a width jump.
    auto before=p.blur_cloud();p.in.controls_free=true;p.changed(t);
    assert(p.blur_cloud().controls.w==before.controls.w);
    step(35);assert(p.blur_cloud().alpha==1);
    p.in.controls_free=false;p.changed(t);step(35);
    assert(p.blur_cloud().alpha==0&&p.blur_cloud().controls.w==boxed);
  }
  BlurCloud cloud{{96,510,1088,76},1};
  for(int rot:{0,90,180,270}) {
    auto a=cloud_uniforms(cloud,1280,720,1920,1080,rot,96);
    assert(a.rx>0&&a.ry>0&&a.sx>0&&a.sy>0);
    if(rot==90||rot==270) {
      // OUTPUT is transposed before this hook: the cloud stays horizontally wide.
      assert(a.rx>a.ry&&a.x==.5);
    }
  }
  auto normal=cloud_uniforms(cloud,1280,720,1920,1080,0,96);
  assert(normal.x==.5&&normal.y>.7&&normal.y<.9);
  auto flipped=cloud_uniforms(cloud,1280,720,1920,1080,180,96);
  assert(std::abs(normal.y+flipped.y-1)<1e-8);
  puts("Blur cloud: pure white ink, shadow, continuous/reversible style transitions, no scene churn, letterbox mapping and all eight resize targets passed");
}
