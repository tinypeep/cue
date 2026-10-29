#include "../src/ui/play.h"
#include "../src/core/resize_hit.h"
#include "../src/core/video_window.h"
#include "../src/core/ui_scale.h"
#include "../src/core/store.h"
#include <cassert>
#include <cstdio>
#include <fstream>

int main(){
 assert(Store{}.ui_scale==kDefaultUiSize && kDefaultUiSize==2);  // médio is the default
 auto path=std::filesystem::temp_directory_path()/"cue-refinement-settings.json";
 {std::ofstream file(path);file << R"({"schema":3,"prefs":{"ui_scale":0}})";}
 Store migrated;assert(migrated.load(path)&&migrated.ui_scale==2);  // old default -> small -> médio
 {std::ofstream file(path);file << R"({"schema":4,"prefs":{"ui_scale":2}})";}
 Store chosen;assert(chosen.load(path)&&chosen.ui_scale==2);
 {std::ofstream file(path);file << R"({"schema":4,"prefs":{"ui_scale":-1}})";}
 Store old_small;assert(old_small.load(path)&&old_small.ui_scale==2);  // 1.0 betas: small was only the default
 {std::ofstream file(path);file << R"({"schema":5,"prefs":{"ui_scale":-1}})";}
 Store small;assert(small.load(path)&&small.ui_scale==-1);  // chosen after 1.0: kept
 // portrait windows: a narrower, bigger dock that still follows the chosen size
 assert(portrait_scale(1.5,1280,720,2)==1.5);
 double p_small=portrait_scale(1,540,960,-1), p_med=portrait_scale(1.5,540,960,2), p_big=portrait_scale(3,540,960,1);
 assert(p_small>=1.5 && p_med>=p_small && p_big==3);
 assert(kCompactDockW*p_med<=540*0.8+1e-9);
 std::filesystem::remove(path);std::filesystem::remove(path.string()+".bak");
 for(double ui:{1.,1.5,2.,3.})for(int dpi:{96,144,192}){
  WindowChrome c;c.s=ui;c.vis.v=1;
  for(int i=0;i<3;++i){auto b=c.rect(i,1600);
   for(int x=b.x;x<b.r();++x)for(int y=b.y;y<b.b();++y)
    assert(player_resize_hit(x,y,1600,1000,dpi,ui)==1);
  }
  assert(player_resize_hit(0,500,1600,1000,dpi,ui)==10);
  assert(player_resize_hit(1599,999,1600,1000,dpi,ui)==17);
  assert(player_resize_hit(20*dpi/96,2,1600,1000,dpi,ui)!=13); // corner no longer 32 DIP
 }
 for(double ar:{9./16,3./4,1.,4./3,16./9,2.39}){
  auto a=video_window_size(ar,1920,1040,96,1);
  assert(a.w<=1920*.9+1&&a.h<=1040*.84+1);
  assert(std::abs(a.w/(double)a.h-ar)<.004);
  auto b=video_window_size(ar,1280,720,96,1);
  assert(b.w<=1280*.9+1&&b.h<=720*.84+1);
 }
 for(bool free:{false,true}){
  PlayView p;p.in.controls_free=free;p.in.sid=1;p.in.kind=SubKind::Text;p.in.sub_shown=true;p.in.sub_text={"one line","second line"};p.in.sub_lines=2;p.in.style.size=60;
  p.in.style.pos=100;p.resize(1280,720,2);double t=1;p.reset(t);
  auto tick=[&](){t+=1./60;p.tick(t,1./60);};
  for(int i=0;i<60;++i){p.activity(t);tick();p.take_actions();}
  double raised=p.lifted_pos(); assert(raised<99);
  double before=raised;bool began_return=false;int intermediate=0;
  for(int i=0;i<240;++i){tick();double y=p.lifted_pos();
   assert(std::abs(y-before)<2.5); // no one-frame drop from lifted to baseline
   if(y>raised+.001){began_return=true;if(y<99.99)++intermediate;}
   if(began_return)assert(y>=before-.0001);
   before=y;p.take_actions();
  }
  assert(began_return&&intermediate>12&&std::abs(p.lifted_pos()-100)<.01);
  // Pointer return during descent reverses continuously.
  p.activity(t);tick();double y=p.lifted_pos();assert(y>97.5&&y<=100);
  for(int i=0;i<60;++i){p.activity(t);tick();}
  assert(p.lifted_pos()<99);
 }
 puts("Player refinements: smooth subtitle return in both styles, small default migration, exact per-file aspect fits and every chrome button protected from resize passed");
}
