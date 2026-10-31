#include "../src/core/remote_proto.h"
#include "../src/core/json.h"
#include "../src/ui/remote_view.h"
#include "../src/core/i18n.h"
#include <cassert>
#include <cstdio>
#include <map>
#include <random>
using namespace remote;
int main() {
  assert(kCodeDigits == 6 && valid_token(std::string(64,'a')));
  assert(!valid_token(std::string(32,'a')) && !valid_token(std::string(64,'g')));
  for (auto h : {"192.168.31.226:7878", "127.0.0.1:7878", "localhost:7878"}) assert(valid_authority(h));
  for (auto h : {"evil.test:7878", "192.168.1.300:7878", "192.168.1.1.evil", "1.2.3:7878", "192.168.1.1:0", "192.168.1.1:65536", "010.0.0.1", "1.2.3.4:80:81"}) assert(!valid_authority(h));
  assert(valid_ws_key("dGhlIHNhbXBsZSBub25jZQ=="));
  assert(!valid_ws_key("\r\nInjected: yes") && !valid_ws_key("dGhlIHNhbXBsZSBub25jZR=="));
  Request req; size_t n = 0;
  for (auto text : {"GET / HTTP/1.1\r\nHost: a\r\nHost: b\r\n\r\n", "GET / HTTP/1.1\r\nHost: a\r\nContent-Length: 10\r\n\r\n", "GET / HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\n", "GET /%00 HTTP/1.1\r\nHost: a\r\n\r\n", "GET /?p=a&p=b HTTP/1.1\r\nHost: a\r\n\r\n", "GET / HTTP/1.1\r\nBad Header: yes\r\n\r\n"}) assert(parse_request(text,req,n) == -1);
  auto response = http_response(200, "text/html", "", false, std::string(32,'a'), "127.0.0.1:7878");
  assert(response.find("script-src 'nonce-" + std::string(32,'a') + "'") != std::string::npos);
  assert(response.find("connect-src 'self' ws://127.0.0.1:7878") != std::string::npos);
  assert(http_response(200,"text/plain","").find("script-src 'none'") != std::string::npos);
  JVal j;
  for (auto text : {"{} garbage", "{\"c\":\"toggle\",\"c\":\"open\"}", "{\"x\":1e999}", "{\"x\":nan}", "{\"x\":01}", "{\"x\":1.}", "{\"x\":\"\\q\"}", "{\"x\":\"\\uZZZZ\"}", "{\"x\":\"\\uD800\"}", "{\"x\":\"\\uDC00\"}", "{\"x\":\"a\nb\"}"}) assert(!json_parse(text,j));
  assert(json_parse("{\"x\":\"\\uD83D\\uDE00\",\"y\":-2.5e+2} \n",j) && j["y"].num() == -250);
  Command cmd;
  for (auto text : {"{\"c\":\"exec\"}", "{\"c\":\"timer\",\"v\":1e300}", "{\"c\":\"audio\",\"v\":1e300}", "{\"c\":\"seek\",\"v\":true}", "{\"c\":\"subsize\",\"v\":-999}", "{\"c\":\"open\",\"path\":\"C:\\\\a\\u0000.mkv\"}"}) assert(!parse_command(text,cmd));
  assert(parse_command("{\"c\":\"audio\",\"v\":-1}",cmd));
  assert(parse_command("{\"c\":\"toggle\"}",cmd));
  for (auto path : {"C:\\Videos\\.. \\secret.mp4", "C:\\Videos\\..\\secret.mp4", "C:\\Videos\\x:secret.mp4", "C:\\Videos\\x.\\secret.mp4", "C:\\Videos\\nul.mp4", "C:\\Videos\\com1.mp4", "C:\\Videos\\\\bad.mp4", "\\\\server\\Videos\\a.mp4", "\\\\?\\C:\\Videos\\a.mp4"}) assert(!path_within("C:\\Videos",path));
  assert(path_within("C:\\Videos", "c:/videos/Season 1/episode.mp4"));
  RateLimit rate;
  for (int i=0;i<40;++i) assert(rate.take(0));
  assert(!rate.take(0) && rate.take(.05) && !rate.take(.05));
  assert(rate.take(5));
  Frame f; size_t used = 0;
  assert(ws_decode(std::string("\x89\xfe\x00\x7e",4),f,used) == -1); // overlong control frame
  assert(ws_decode(std::string("\x01\x80",2),f,used) == -1); // fragments unsupported
  assert(ws_decode(std::string("\x83\x80",2),f,used) == -1); // reserved opcode
  std::mt19937 rng(42);
  for (int i=0;i<12000;++i) {
    std::string bytes(rng()%300,' ');
    for (auto& c:bytes) c=(char)rng();
    parse_request(bytes,req,n); ws_decode(bytes,f,used); parse_command(bytes,cmd);
  }
  // The actual pointer routes must expose every shared folder and all controls on both axes.
  for (auto dims : {std::pair{1000,1780}, {640,400}, {960,540}, {1280,720}, {320,568}}) {
    RemoteView view;
    view.chips={{"Vídeos",true},{"Downloads",true},{"Área de Trabalho",false}};
    view.address="192.168.31.107:7878"; view.code="123456";
    view.set_size(dims.first,dims.second,3.5);
    view.set_link("http://192.168.31.107:7878/#c=123456");
    view.open(1);
    std::map<int,int> chips; bool add=false,off=false,sound=false,close=false;
    for (int y=0;y<dims.second;y+=2) for (int x=0;x<dims.first;x+=2) {
      auto action=view.mouse_down(x,y,2);
      if(action.a==RemoteAct::ToggleFolder) ++chips[action.index];
      add|=action.a==RemoteAct::AddFolder; off|=action.a==RemoteAct::TurnOff;
      sound|=action.a==RemoteAct::Sound; close|=action.a==RemoteAct::Close;
    }
    assert(chips.size()==3 && add && off && sound && close);
    Canvas canvas; view.render(canvas,3);
    assert(canvas.w==dims.first && canvas.h==dims.second);
  }
  std::puts("Remote security: strict HTTP/WS/JSON, credential migration, path attacks, command ranges, rate limits, 12000 malformed inputs and portrait/landscape pointer routes passed");
}
