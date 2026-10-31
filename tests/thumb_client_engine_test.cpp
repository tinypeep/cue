// Native Windows integration test; uses this executable as a deliberately stuck
// --thumb-host. No engine DLL needed by the fake child. Compile/link with the app
// objects except src/win/main.cpp, -municode and the same Windows libraries.
#include "../src/win/thumbhost.h"
#include <cassert>
#include <cwchar>
#include <cstdio>
int wmain(int argc, wchar_t** argv) {
  if (argc > 1 && !wcscmp(argv[1], L"--thumb-host")) { Sleep(INFINITE); return 0; }
  HWND hwnd = CreateWindowExW(0,L"STATIC",L"test",0,0,0,0,0,HWND_MESSAGE,nullptr,nullptr,nullptr);
  assert(hwnd);
  DWORD before=0,after=0;
  GetProcessHandleCount(GetCurrentProcess(),&before);
  for(int cycle=0;cycle<50;++cycle) {
    ThumbClient client;
    assert(client.start(hwnd,WM_APP+100));
    ULONGLONG begin=GetTickCount64();
    for(int i=0;i<1000;++i) client.open(std::string(8000,'a'),320,i+1);
    // The old UI WriteFile fills the pipe and never reaches this assertion.
    assert(GetTickCount64()-begin<5000);
    client.kill();
  }
  Sleep(1000); // let the terminated children/owned I/O state finish
  GetProcessHandleCount(GetCurrentProcess(),&after);
  assert(after<=before+4);
  MSG msg;
  assert(!PeekMessageW(&msg,hwnd,WM_APP+100,WM_APP+100,PM_REMOVE));
  DestroyWindow(hwnd);
  printf("stalled helper/restart test: handles %lu -> %lu\n",before,after);
  return 0;
}
