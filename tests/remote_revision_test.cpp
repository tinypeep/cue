#include "../src/ui/remote_view.h"
#include "../src/ui/play.h"
#include <cassert>
#include <cstdio>
int main() {
  RemoteView v; v.address="192.168.31.107:7878";v.code="123456";
  v.chips={{"Videos",true},{"Downloads",true},{"Desktop",false}};
  v.set_link("http://192.168.31.107:7878/#c=123456");
  v.set_size(1000,1780,2); assert(v.s>=4);
  v.open(100); assert(v.key("esc",false,100.01).a==RemoteAct::Close);
  v.close(100.01);v.tick(100.5,.1);assert(v.closed(100.5));
  Canvas c;v.open(101);v.render(c,103);assert(c.w==1000 && c.h==1780);
  v.set_size(1600,900,2);assert(v.s==2);
  PlayView p;p.in.dur=120;p.in.time=111;p.resize(960,540,2);p.reset(0);
  p.set_next_details("Twin Peaks S01E02.mkv");
  p.set_next_picture(std::vector<uint32_t>(84*48,0xff884422),84,48);
  p.next_notice("Up next in 9s",1);p.tick(1.2,.1);
  p.prepare(1.2);
  bool next=false,cancel=false;
  for(int y=0;y<80;y+=2) for(int x=0;x<960;x+=2) {
    p.mouse_down(x,y,1.3);
    for(auto a:p.take_actions()) { next|=a.a==PA::NextEpisode;cancel|=a.a==PA::CancelNext; }
  }
  assert(next && cancel);
  p.next_notice("",1.5);p.prepare(1.6);
  puts("Remote portrait scale, Esc lifecycle and next-episode/cancel hit targets passed");
}
