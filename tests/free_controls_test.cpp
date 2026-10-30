#include "../src/ui/play.h"
#include "../src/ui/atmosphere.h"
#include "../src/core/store.h"
#include <cassert>
#include <filesystem>
#include <cstdio>
static void settle(PlayView& p,double& t){p.activity(t);for(int i=0;i<50;++i){t+=1./60;p.tick(t,1./60);}}
int main(){
 auto file=std::filesystem::temp_directory_path()/"cue-free-controls.json";
 assert(!Store{}.controls_free);
 for(bool mode:{false,true}){Store a;a.controls_free=mode;assert(a.save(file));Store b;assert(b.load(file)&&b.controls_free==mode);}
 std::filesystem::remove(file);std::filesystem::remove(file.string()+".bak");
 for(int width:{240,320,640})for(double scale:{1.,1.5,2.,3.}){
  PlayView p;p.in.dur=300;p.in.time=30;p.resize(width*scale,400*scale,scale);double t=1;p.reset(t);p.mouse_move(90,90,t);settle(p,t);p.take_actions();
  int original=p.layers[PlayView::L_DOCK].c.w;
  p.in.controls_free=true;p.changed(t);settle(p,t);
  auto& layer=p.layers[PlayView::L_DOCK];assert(layer.visible);
  assert(layer.x>=0&&layer.x+layer.c.w<=width*scale);
  assert(layer.c.px[(size_t)(layer.c.h/2)*layer.c.w+2]==0); // no card fill
  if(width>=640)assert(layer.c.w>original);
  for(auto pixel:layer.c.px){unsigned alpha=pixel>>24;if(alpha==255) assert(pixel==0xffffffffu);
    assert(((pixel>>16)&255)==((pixel>>8)&255)&&((pixel>>8)&255)==(pixel&255));}
  auto neutral=layer.c.px;
  p.in.accent={100,180,210};p.in.accent_k=1;p.changed(t);settle(p,t);assert(neutral==layer.c.px);
  // Soft shadow belongs only to the ink; the corners have no panel fill.
  assert(layer.c.px.front()==0&&layer.c.px.back()==0);
  p.mouse_down(layer.x+2,layer.y+layer.c.h/2,t);p.mouse_up(layer.x+2,layer.y+layer.c.h/2,t+.05);settle(p,t);
  int pause=0;for(auto a:p.take_actions())pause+=a.a==PA::TogglePause;assert(pause==1); // empty space passes to video
  p.in.controls_free=false;p.changed(t);settle(p,t);assert(layer.c.w==original);
  p.open_panel_for_preview(3,t);settle(p,t);auto& menu=p.layers[PlayView::L_PANEL];assert(menu.x>=0&&menu.y>=0&&menu.y+menu.c.h<=400*scale);
  p.take_actions();for(int i=0;i<20;++i)p.key("down",false,false,t);
  p.key("up",false,false,t);p.key("up",false,false,t);p.key("right",false,false,t);  // past "about" and "remote control"
  bool selected=false;for(auto a:p.take_actions())if(a.a==PA::ControlsFree&&a.v==1)selected=true;assert(selected);
 }
 for(bool audio:{false,true}){PlayView p;p.in.audio_only=audio;p.in.audios={{1,"audio"},{2,"audio"}};p.resize(480,360,2);double t=1;p.reset(t);p.open_panel_for_preview(3,t);settle(p,t);auto& menu=p.layers[PlayView::L_PANEL];assert(menu.y>=0&&menu.y+menu.c.h<=360);}
 puts("Free controls: saved preference, white ink, transparent corners, responsive width, soft shadow, video clicks and restoration passed");
}
