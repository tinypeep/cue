#include "../src/ui/play.h"
#include "../src/core/store.h"
#include "../src/core/window_policy.h"
#include <cassert>
#include <cstdio>
static void settle(PlayView& p,double& t){for(int i=0;i<45;++i){t+=1./60;p.tick(t,1./60);}}
static std::vector<uint32_t> clock(const Layer& l,int scale,int y){std::vector<uint32_t> b;for(int yy=(y-5)*scale;yy<(y+5)*scale;++yy)for(int x=8*scale;x<60*scale;++x)b.push_back(l.c.px[yy*l.c.w+x]);return b;}
int main(){
 assert(Store{}.ambient);
 assert(!restore_saved_window(false, 540, 960));
 assert(restore_saved_window(false, 960, 540));
 assert(!restore_saved_window(true, 540, 960));
 assert(!restore_saved_window(false, 0, 0));
 for(int width:{240,640})for(int scale:{1,2,3}){
  PlayView p;p.in.dur=300;p.in.time=30;double t=1;p.resize(width*scale,540*scale,scale);p.reset(t);p.mouse_move(100,100,t);settle(p,t);p.take_actions();
  p.key("c",false,false,t);settle(p,t);assert(p.panel_open()&&p.take_actions().empty());p.key("esc",false,false,t);settle(p,t);
  auto& l=p.layers[PlayView::L_DOCK];assert(l.visible&&l.c.w<=340*scale);assert(l.y+l.c.h<540*scale-40*scale);
  int y=width==240?51:28;assert(l.c.h==(width==240?62:38)*scale);
  auto before=clock(l,scale,y);
  for(int i=0;i<2;++i){int x=l.x+20*scale,yy=l.y+y*scale;p.mouse_move(x,yy,t);p.mouse_down(x,yy,t);p.mouse_up(x,yy,t);p.mouse_move(width*scale/2,100,t);settle(p,t);assert(p.take_actions().empty());assert(!p.in.paused&&p.in.time==30);assert((clock(l,scale,y)==before)==(i==1));}
  p.key("space",false,false,t);t+=.1;p.tick(t,.1);assert(p.in.paused&&p.layers[PlayView::L_FLASH].visible);auto actions=p.take_actions();assert(actions.size()==1&&actions[0].a==PA::TogglePause);
  p.key("space",false,false,t);t+=.1;p.tick(t,.1);assert(!p.in.paused&&p.layers[PlayView::L_FLASH].visible);
  if(width==240){
    p.take_actions();
    auto click=[&](int x,int y){p.mouse_move(l.x+x*scale,l.y+y*scale,t);p.mouse_down(l.x+x*scale,l.y+y*scale,t);p.mouse_up(l.x+x*scale,l.y+y*scale,t);};
    int dw=l.c.w/scale,centre=dw/2;
    for(int dir:{-1,1}){
      click(centre+(dir<0?-17:18),34);
      auto a=p.take_actions();assert(a.size()==1&&a[0].a==PA::SeekRel&&a[0].v==15*dir);
    }
    click(centre,34);auto a=p.take_actions();assert(a.size()==1&&a[0].a==PA::TogglePause);
    for(auto sample : {std::pair<int,double>{45*scale,125}, {18*scale,50}}){
      int px=l.x+27*scale+sample.first,py=l.y+13*scale;
      p.mouse_move(px,py,t);p.mouse_down(px,py,t);p.mouse_up(px,py,t);
      auto volume=p.take_actions();assert(!volume.empty());
      for(auto action:volume)assert(action.a==PA::Volume&&std::abs(action.v-sample.second)<1);
    }
    click(45,13);for(auto a:p.take_actions())assert(a.a!=PA::TogglePause&&a.a!=PA::SeekRel); // volume row cannot pause or seek
    click(14,34);assert(p.panel_open()&&p.take_actions().empty());p.key("esc",false,false,t);
    click(dw-15,34);a=p.take_actions();assert(a.size()==1&&a[0].a==PA::Fullscreen);
    click(dw-12,13);assert(p.panel_open()&&p.take_actions().empty());
  }
  assert(bmp("cue:skip-back")&&bmp("cue:skip-forward"));
 }
 puts("Controls: portrait reflow, compact width, raised position, C opens CC, clock toggles without pause/seek, central feedback and default light passed");
}
