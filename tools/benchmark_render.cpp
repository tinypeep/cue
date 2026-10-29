#include "../src/ui/home.h"
#include "../src/ui/play.h"
#include <chrono>
#include <cstdio>
int main(){
 for(int pass=0;pass<3;++pass){
  HomeView h;h.set_size(1280,720,2);h.set_rows({{"sample.mp4","02:10",.5},{"clip.mkv","00:30",.2}});Canvas c;
  auto start=std::chrono::steady_clock::now();
  for(int i=0;i<1000;++i){h.track(400+i%500,200,1+i/120.0);h.tick(1+i/120.0,1/120.0);h.render(c,1+i/120.0);}
  std::printf("home 1000: %.3f ms\n",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
  PlayView p;p.resize(1280,720,2);p.in.dur=1000;p.reset(1);
  start=std::chrono::steady_clock::now();
  for(int i=0;i<1000;++i){p.in.time=i;p.changed(1+i/120.0);p.activity(1+i/120.0);p.tick(1+i/120.0,1/120.0);}
  std::printf("dock 1000: %.3f ms\n",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
 }
}
