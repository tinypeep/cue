#include "../src/core/i18n.h"
#include "../src/core/media_info.h"
#include "../src/ui/play.h"
#include <cassert>
#include <cstdio>
#include <limits>

static std::string value(const InfoFields& fields,const std::string& name) {
  for(auto& p:fields)if(p.first==name)return p.second;
  return {};
}
static void settle(PlayView& p,double& t) {for(int i=0;i<45;++i){t+=1./60;p.tick(t,1./60);}}
int main() {
  i18n::set_language(i18n::Lang::PtBR);  // these checks pin the Portuguese reference wording
  MediaDetails info;info.name="vídeo de exemplo.mp4";info.bytes=1250000000;info.duration=125;
  info.container="mov,mp4,m4a,3gp,3g2,mj2";info.width=1920;info.height=1080;info.fps=29.97002997;
  info.video_codec="h264";info.audio_codec="aac";info.channels=2;info.sample_rate=44100;
  auto fields=media_info_fields(info);
  assert(fields.size()==10 && value(fields,"tamanho")=="1,25 GB");
  assert(value(fields,"resolução")=="1920 × 1080" && value(fields,"FPS")=="29,97");
  assert(value(fields,"duração")=="02:05" && value(fields,"frequência")=="44,1 kHz");
  info.audio_only=true;
  auto audio=media_info_fields(info);
  assert(audio.size()==7 && value(audio,"resolução").empty() && value(audio,"vídeo").empty());
  info.bytes=-1;info.sample_rate=std::numeric_limits<double>::quiet_NaN();
  auto missing=media_info_fields(info);
  assert(value(missing,"tamanho")=="indisponível" && value(missing,"frequência")=="indisponível");
  for(bool sound:{false,true})for(double scale:{1.,1.5,2.,2.5,3.,4.})for(int width:{240,320,480,640}) {
    PlayView p;int w=(int)(width*scale),h=(int)(std::max(180,width*9/16)*scale);double t=1;
    p.in.audio_only=sound;p.in.dur=125;p.resize(w,h,scale);p.reset(t);settle(p,t);p.take_actions();
    // First right-click option opens metadata, never pausing or seeking the file.
    p.right_click(w/2,h/2,t);p.key("down",false,false,t);p.key("enter",false,false,t);
    auto acts=p.take_actions();assert(acts.size()==1 && acts[0].a==PA::FileInfo);
    p.in.file_info=sound?audio:fields;++p.in.file_info_revision;p.changed(t);settle(p,t);
    const auto& layer=p.layers[PlayView::L_PANEL];
    assert(layer.visible && layer.x>=0 && layer.y>=0 && layer.x+layer.c.w<=w && layer.y+layer.c.h<=h);
    assert(w-layer.x-layer.c.w <= (int)(9*scale));assert(layer.y<=(int)(27*scale));
    auto pixels=layer.c.px;
    p.in.time=50;p.changed(t);settle(p,t);
    assert(layer.c.px==pixels); // clock updates do not redraw/animate metadata content.
    p.key("esc",false,false,t);settle(p,t);assert(!p.panel_open());
    assert(p.take_actions().empty());
    p.open_panel_for_preview(6,t);settle(p,t);p.take_actions();
    p.mouse_down(1,h-1,t);p.mouse_up(1,h-1,t);settle(p,t);
    assert(!p.panel_open() && p.take_actions().empty());
    p.open_panel_for_preview(6,t);settle(p,t);
    int old_x=layer.x;
    p.resize(w+100,h+100,scale);settle(p,t);
    const auto& moved=p.layers[PlayView::L_PANEL];assert(moved.x>=old_x);assert((w+100)-moved.x-moved.c.w<=(int)(9*scale));assert(moved.x+moved.c.w<=w+100);
  }
  std::puts("Metadata: formats, missing values, audio waveform exclusion, right-click route, top-right bounds, dismissal and stable playback passed");
}
