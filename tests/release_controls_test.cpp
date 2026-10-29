#include "../src/ui/play.h"
#include "../src/core/store.h"
#include "../src/core/viewport_gate.h"
#include <cassert>
#include <cstdio>

static void settle(PlayView& p,double& t,int count=40){while(count--){t+=1./60;p.tick(t,1./60);}}
int main(){
 assert(Store{}.ui_scale==2);
 assert(!viewport_ready_for_controls(1920,1080,1280,720,true,.1));
 assert(!viewport_ready_for_controls(1920,1080,1920,1080,false,.1));
 assert(!viewport_ready_for_controls(1920,1080,1920,1080,true,.02));
 assert(viewport_ready_for_controls(1920,1080,1920,1080,true,.06));
 assert(!viewport_ready_for_controls(1920,1080,0,0,false,2));
 assert(viewport_ready_for_controls(1920,1080,0,0,true,1.1));
 assert(bmp("k-audio"));
 for(bool minimal:{false,true})for(int width:{240,640})for(double scale:{1.,1.5,2.,3.}){
  PlayView p;p.in.controls_free=minimal;p.in.dur=120;p.in.time=30;
  p.resize(width*scale,500*scale,scale);double t=1;p.reset(t);settle(p,t);
  auto empty=p.layers[PlayView::L_DOCK].c.px;
  p.in.audios={{1,"audio","eng"}};p.changed(t);settle(p,t);
  assert(empty==p.layers[PlayView::L_DOCK].c.px); // one audio stream adds no selector
  p.in.audios.push_back({2,"audio","por"});p.in.aid=1;p.changed(t);settle(p,t);
  auto& d=p.layers[PlayView::L_DOCK];assert(empty!=d.c.px);
  double dw=d.c.w/scale;
  int x=d.x+(width<300?33.5:dw-58.5)*scale,y=d.y+(width<300?34:13)*scale;
  p.take_actions();p.mouse_move(x,y,t);p.mouse_down(x,y,t);p.mouse_up(x,y,t);
  assert(p.panel_open());
  for(int i=0;i<2;++i)p.key("down",false,false,t);
  p.key("enter",false,false,t);
  bool selected=false;for(auto a:p.take_actions()) if(a.a==PA::SelectAudio){assert(a.id==1||a.id==2);selected=true;}
  assert(selected);
  p.key("esc",false,false,t);settle(p,t);
  // Fullscreen transition: fade out at old bounds, keep all layers hidden
  // through repeated native resizes, then reveal within the final viewport.
  p.take_actions();p.begin_viewport_transition(t);settle(p,t,8);assert(p.viewport_hidden());
  p.key("esc",false,false,t);for(auto a:p.take_actions())assert(a.a!=PA::CloseVideo);
  for(auto& layer:p.layers)assert(!layer.visible);
  for(auto dims:{std::pair{1920,1080},std::pair{2560,1440},std::pair{1920,1080}}){
   p.resize(dims.first,dims.second,scale);p.prepare(t);
   for(auto& layer:p.layers)assert(!layer.visible);
   assert(p.blur_cloud().alpha==0);
  }
  p.end_viewport_transition(t);settle(p,t,14);
  auto& full=p.layers[PlayView::L_DOCK];assert(full.visible);
  assert(full.x>=0&&full.y>=0&&full.x+full.c.w<=1920&&full.y+full.c.h<=1080);
 }
 for(bool initial:{false,true}){
  PlayView p;p.in.paused=initial;p.resize(1280,720,2);p.reset(1);double t=1;settle(p,t);p.take_actions();
  p.mouse_down(640,360,t);p.mouse_up(640,360,t+.01);
  auto a=p.take_actions();assert(a.size()==1&&a[0].a==PA::TogglePause&&a[0].v==!initial);
  assert(p.in.paused!=initial); // no timer or system double-click delay
  p.mouse_down(640,360,t+.12);p.mouse_up(640,360,t+.13);
  a=p.take_actions();assert(a.size()==2&&a[0].a==PA::TogglePause&&a[0].v==initial&&a[1].a==PA::Fullscreen);
  assert(p.in.paused==initial);
 }
 puts("Release controls: small default, optional music selector and audio selection, immediate pause targets, double-click restoration and hidden/contained fullscreen overlays passed");
}
