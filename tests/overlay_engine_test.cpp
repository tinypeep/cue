// Native Windows resource-counter regression (not run by the Linux portable suite).
// Build from the source root with MinGW:
// x86_64-w64-mingw32-g++ -std=c++20 -O2 -municode -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_WIN32_WINNT=0x0A00 tests/overlay_engine_test.cpp src/win/overlay.cpp src/win/vsync.cpp src/ui/*.cpp src/core/*.cpp -static -ldwmapi -lgdi32 -lpsapi -o build/overlay-memory-test.exe
#include "../src/win/overlay.h"
#include <psapi.h>
#include <cassert>
#include <cstdio>

struct BlankScene : SceneView {
  int w=0,h=0;bool closing_=false;
  void set_size(int width,int height,double) override { w=width;h=height; }
  void open(double) override { closing_=false; }
  void close(double) override { closing_=true; }
  bool closing() const override { return closing_; }
  bool closed(double) const override { return closing_; }
  bool tick(double,double) override { return true; }
  void render(Canvas& canvas,double) override { if(canvas.w!=w||canvas.h!=h)canvas.resize(w,h);canvas.clear({3,3,3}); }
  void pointer_move(int,int,double) override {}
  void pointer_leave(double) override {}
  SceneAct pointer_down(int,int,double) override { return {}; }
  SceneAct key_down(const std::string&,bool,double) override { return {}; }
  int cursor() const override { return 0; }
};
struct FindOverlay { Overlay* host;HWND hwnd=nullptr; };
static BOOL CALLBACK find_overlay(HWND hwnd,LPARAM p) {
  auto* search=(FindOverlay*)p;
  if(GetWindowLongPtrW(hwnd,GWLP_USERDATA)==(LONG_PTR)search->host)search->hwnd=hwnd;
  return TRUE;
}
static SIZE_T private_bytes() {
  PROCESS_MEMORY_COUNTERS_EX p{};p.cb=sizeof p;
  assert(GetProcessMemoryInfo(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)&p,sizeof p));return p.PrivateUsage;
}
int wmain() {
  auto process=GetCurrentProcess();DWORD before=GetGuiResources(process,GR_GDIOBJECTS);
  {
    DibSurface dib;
    for(int i=0;i<1000;++i) { assert(dib.resize(100+i%400,100+i%250));assert(GetGuiResources(process,GR_GDIOBJECTS)<=before+2); }
  }
  assert(GetGuiResources(process,GR_GDIOBJECTS)==before);
  HINSTANCE inst=GetModuleHandleW(nullptr);
  HWND owner=CreateWindowExW(0,L"STATIC",L"Cue resource test",WS_OVERLAPPEDWINDOW,0,0,640,480,nullptr,nullptr,inst,nullptr);
  assert(owner);ShowWindow(owner,SW_SHOW);
  SIZE_T memory_before=0,memory_after=0;
  {
    Overlay host;BlankScene scene;assert(host.create(inst));
    FindOverlay search{&host};EnumThreadWindows(GetCurrentThreadId(),find_overlay,(LPARAM)&search);assert(search.hwnd);
    DWORD closed_gdi=0;
    for(int i=0;i<101;++i) {
      // Warm the largest allocation first; later sizes should not grow the GDI count.
      int w=i==0?3840:640+(i%20)*23,h=i==0?2160:360+(i%13)*17;
      SetWindowPos(owner,nullptr,0,0,w,h,SWP_NOMOVE|SWP_NOZORDER);
      host.show(&scene,owner,2,host.now());assert(host.visible());
      for(int j=0;j<4;++j) { SetWindowPos(owner,nullptr,0,0,w+j*13,h+j*7,SWP_NOMOVE|SWP_NOZORDER);host.follow(owner,2); }
      host.close(host.now());SendMessageW(search.hwnd,WM_TIMER,1,0);assert(!host.visible());
      DWORD count=GetGuiResources(process,GR_GDIOBJECTS);
      if(i==0) { closed_gdi=count;memory_before=private_bytes(); }
      else assert(count<=closed_gdi+2);
    }
    memory_after=private_bytes();host.destroy();host.destroy();
  }
  DestroyWindow(owner);
  std::printf("Native GDI owner/overlay cycles passed. Private bytes after warm-up: %llu -> %llu (allocator retention is not itself a leak).\n",(unsigned long long)memory_before,(unsigned long long)memory_after);
  return 0;
}
