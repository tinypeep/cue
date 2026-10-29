#include "../src/core/frame_cadence.h"
#include "../src/ui/anim.h"
#include "../src/ui/play.h"
#include "../src/core/blur_geometry.h"
#include <cassert>
#include <cstdio>

int main(){
 for(double duration:{.32,.42}){
  PolishedMotion m;m.snap(0);m.to(1,duration);
  double prev=0;int frames=0;
  for(int i=0;i<60;++i){m.step(1./60);assert(m.x>=prev-1e-9&&m.x<=1);if(!m.settled())++frames;prev=m.x;}
  assert(m.x==1&&m.v==0&&frames>=18);
  m.to(0,duration);for(int i=0;i<8;++i)m.step(1./60);
  double pos=m.x,vel=m.v;m.to(1,duration);assert(m.x==pos&&m.v==vel); // C1 retarget
  for(int i=0;i<60;++i)m.step(1./60);assert(m.x==1&&m.v==0);
 }
 // Windows millisecond timer rounding: 17/17/16 ms, without cumulative drift.
 for(double beginning:{0.,.004,.014}) {
 FrameCadence ui,overlay,cloud;double time=beginning;int uploads=0,uniforms=0;
 for(int i=0;i<600;++i){
  assert(ui.ready(time));ui.mark(time);
  // An expensive first upload must not shift all later uploads into alternate ticks.
  double painted=time+(i==0?.006:.0003+(i%5)*.0001);
  if(overlay.ready(painted,.0015)){overlay.mark(painted);++uploads;}
  if(cloud.ready(painted,.0015)){cloud.mark(painted);++uniforms;}
  unsigned delay=ui.wait_ms(painted);assert(delay>=1&&delay<=17);
  time=painted+std::max(10u,delay)/1000.; // USER_TIMER_MINIMUM
 }
 assert(uploads>=597&&uniforms>=597);
 assert(time-beginning>9.96&&time-beginning<10.03);
 // A long stall advances once, without replaying an accumulated queue of frames.
 ui.mark(time+2);assert(!ui.ready(time+2));assert(ui.wait_ms(time+2)<=17);
 }
 BlurCloud c{{96,558,1088,76},1};auto u=cloud_uniforms(c,1280,720,1280,720,0,96);
 assert(std::abs(u.sx*1280-6)<1e-8&&std::abs(u.sy*720-6)<1e-8);
 assert(u.ry*720<66); // shorter cloud, formerly 98 px
 for(bool minimal:{false,true}){
  PlayView p;p.in.controls_free=minimal;p.in.sid=1;p.in.kind=SubKind::Text;p.in.style.pos=100;
  p.in.style.size=60;p.in.sub_lines=2;p.in.sub_text={"a line","a second line"};p.resize(1280,720,2);
  double t=1;p.reset(t);
  for(int i=0;i<60;++i){p.activity(t);t+=1./60;p.tick(t,1./60);}
  double raised=p.lifted_pos(),start=-1,finish=-1,previous=raised;
  for(int i=0;i<200;++i){t+=1./60;p.tick(t,1./60);double y=p.lifted_pos();
   assert(y>=previous-1e-6);previous=y;
   if(start<0&&y>raised+.0001)start=t;
   if(start>0&&finish<0&&std::abs(y-100)<.00001)finish=t;
  }
  assert(start>0&&finish-start>.36&&finish-start<.44);
 }
 {
  PlayView p;p.resize(1280,720,2);p.in.dur=120;double t=1;p.reset(t);
  for(int i=0;i<30;++i){t+=1./60;p.tick(t,1./60);}
  p.in.controls_free=true;p.changed(t);int fractional=0;
  for(int i=0;i<16;++i){t+=1./60;p.tick(t,1./60);auto& d=p.layers[PlayView::L_DOCK];
    if(d.c.w%2){++fractional;assert(d.c.px[(d.c.h/2)*d.c.w+d.c.w-1]>>24);}
  }
  assert(fractional>0); // whole physical pixels, including the panel's right edge
 }
 puts("Motion/cadence: bounded 320/420 ms motion, continuous reversal, 60 Hz deadlines with timer jitter, no alternate upload skips, lighter cloud geometry and subtitle return in both modes passed");
}
