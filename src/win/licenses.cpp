#include "licenses.h"
#include "platform.h"
#include <dwmapi.h>
#include <richedit.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

namespace {
HWND window = nullptr;
struct Document { std::wstring title; std::filesystem::path path; };
struct View {
  HWND combo = nullptr, text = nullptr, sources = nullptr, close = nullptr;
  HFONT font = nullptr;
  HBRUSH brush = CreateSolidBrush(RGB(14,14,14));
  std::filesystem::path base;
  std::vector<Document> docs;
  int scale(int n) const { return MulDiv(n, dpi, 96); }
  UINT dpi = 96;
  bool window_owns = false;
  ~View() { if(font) DeleteObject(font); if(brush) DeleteObject(brush); }
};
void display(View& v) {
  int index = (int)SendMessageW(v.combo, CB_GETCURSEL, 0, 0);
  if (index < 0 || index >= (int)v.docs.size()) return;
  std::ifstream in(v.docs[index].path, std::ios::binary);
  std::string bytes;
  if (in) {
    in.seekg(0,std::ios::end); auto size=in.tellg();
    if (size>=0 && size<=16*1024*1024) { in.seekg(0);bytes.assign(std::istreambuf_iterator<char>(in),{}); }
  }
  std::wstring value = bytes.empty() ? L"Não foi possível ler este documento. Confira se a pasta do Cue foi extraída inteira." : widen(bytes);
  std::wstring lines;
  lines.reserve(value.size());
  for (size_t i=0; i<value.size(); ++i) {
    if (value[i]==L'\n' && (i==0 || value[i-1]!=L'\r')) lines+=L'\r';
    lines+=value[i];
  }
  SetWindowTextW(v.text, lines.c_str());
  SendMessageW(v.text, EM_SETSEL, 0, 0);
  SendMessageW(v.text, EM_SCROLLCARET, 0, 0);
}
void resize(HWND w, View& v) {
  RECT r; GetClientRect(w,&r);
  int m=v.scale(14), top=v.scale(38), bottom=v.scale(44), bw=v.scale(135), bh=v.scale(28);
  MoveWindow(v.combo,m,m,r.right-2*m,v.scale(300),TRUE);
  MoveWindow(v.text,m,top+m,r.right-2*m,std::max<LONG>(v.scale(80),r.bottom-top-bottom-2*m),TRUE);
  MoveWindow(v.sources,m,r.bottom-bottom,bw,bh,TRUE);
  MoveWindow(v.close,r.right-m-v.scale(80),r.bottom-bottom,v.scale(80),bh,TRUE);
}
LRESULT CALLBACK proc(HWND w,UINT message,WPARAM wp,LPARAM lp) {
  auto* v=reinterpret_cast<View*>(GetWindowLongPtrW(w,GWLP_USERDATA));
  if(message==WM_NCCREATE) {
    v=static_cast<View*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
    SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(v));
  }
  if(!v) return DefWindowProcW(w,message,wp,lp);
  switch(message) {
    case WM_CREATE: {
      using Dpi=UINT(WINAPI*)(HWND);
      if(auto dpi=reinterpret_cast<Dpi>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow")))) v->dpi=dpi(w);
      v->font=CreateFontW(-v->scale(16),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
      v->combo=CreateWindowExW(0,L"COMBOBOX",nullptr,WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,0,0,0,0,w,(HMENU)101,GetModuleHandleW(nullptr),nullptr);
      v->text=CreateWindowExW(0,MSFTEDIT_CLASS,nullptr,WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL,0,0,0,0,w,(HMENU)102,GetModuleHandleW(nullptr),nullptr);
      v->sources=CreateWindowExW(0,L"BUTTON",L"códigos-fonte",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,0,0,0,0,w,(HMENU)103,GetModuleHandleW(nullptr),nullptr);
      v->close=CreateWindowExW(0,L"BUTTON",L"fechar",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,0,0,0,0,w,(HMENU)IDCANCEL,GetModuleHandleW(nullptr),nullptr);
      for(HWND child:{v->combo,v->text,v->sources,v->close})SendMessageW(child,WM_SETFONT,(WPARAM)v->font,TRUE);
      SendMessageW(v->text,EM_EXLIMITTEXT,0,16*1024*1024);
      SendMessageW(v->text,EM_SETBKGNDCOLOR,0,RGB(14,14,14));
      CHARFORMAT2W cf{};cf.cbSize=sizeof cf;cf.dwMask=CFM_COLOR|CFM_FACE|CFM_SIZE;cf.crTextColor=RGB(225,225,225);cf.yHeight=240;
      lstrcpyW(cf.szFaceName,L"Segoe UI");SendMessageW(v->text,EM_SETCHARFORMAT,0,(LPARAM)&cf);
      auto add=[&](std::wstring name,std::filesystem::path path){v->docs.push_back({std::move(name),std::move(path)});};
      add(L"Resumo e créditos",v->base/L"third_party/libmpv/LEIA-ME.txt");
      add(L"Copyright e escopo",v->base/L"COPYRIGHT.md");
      add(L"Licenças e componentes",v->base/L"LICENSES.md");
      add(L"Cue · GPLv3",v->base/L"LICENSE");
      add(L"Autoria e licenças dos assets",v->base/L"ASSETS.md");
      add(L"Fontes e compilação do motor",v->base/L"third_party/libmpv/SOURCES.md");
      for(const auto* name:{L"CueOmni",L"CueClean",L"CueReadable"})add(std::wstring(name)+L" · OFL",v->base/L"licenses"/(std::wstring(name)+L"-OFL.txt"));
      auto notices=v->base/L"third_party/libmpv/notices";
      std::vector<Document> third;
      std::error_code ec;
      std::filesystem::recursive_directory_iterator it(notices,std::filesystem::directory_options::skip_permission_denied,ec),end;
      while(!ec && it!=end) {
        if(it->is_regular_file(ec) && !ec) {
          auto rel=it->path().lexically_relative(notices);
          third.push_back({rel.generic_wstring(),it->path()});
        }
        it.increment(ec);
      }
      std::sort(third.begin(),third.end(),[](const Document& a,const Document& b){return a.title<b.title;});
      v->docs.insert(v->docs.end(),third.begin(),third.end());
      for(const auto& doc:v->docs)SendMessageW(v->combo,CB_ADDSTRING,0,(LPARAM)doc.title.c_str());
      SendMessageW(v->combo,CB_SETCURSEL,0,0);display(*v);resize(w,*v);
      BOOL dark=TRUE;DwmSetWindowAttribute(w,20,&dark,sizeof dark);
      return 0;
    }
    case WM_SIZE: resize(w,*v);return 0;
    case WM_GETMINMAXINFO: {
      auto* m=reinterpret_cast<MINMAXINFO*>(lp);m->ptMinTrackSize={v->scale(500),v->scale(340)};return 0;
    }
    case WM_CTLCOLORLISTBOX: case WM_CTLCOLORSTATIC: case WM_CTLCOLOREDIT: {
      HDC dc=(HDC)wp;SetTextColor(dc,RGB(225,225,225));SetBkColor(dc,RGB(14,14,14));return (LRESULT)v->brush;
    }
    case WM_ERASEBKGND: {RECT r;GetClientRect(w,&r);FillRect((HDC)wp,&r,v->brush);return 1;}
    case WM_COMMAND:
      if(LOWORD(wp)==101 && HIWORD(wp)==CBN_SELCHANGE){display(*v);return 0;}
      if(LOWORD(wp)==103){
        auto folder=v->base/L"fontes";
        if((INT_PTR)ShellExecuteW(w,L"open",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL)<=32)
          MessageBoxW(w,L"A pasta fontes não foi encontrada. Os códigos-fonte estão em github.com/gabccin/cue/releases.",L"Cue",MB_OK|MB_ICONINFORMATION);
        return 0;
      }
      if(LOWORD(wp)==IDCANCEL){DestroyWindow(w);return 0;}
      break;
    case WM_CLOSE: DestroyWindow(w);return 0;
    case WM_NCDESTROY:
      window=nullptr;SetWindowLongPtrW(w,GWLP_USERDATA,0);if(v->window_owns) delete v;return DefWindowProcW(w,message,wp,lp);
  }
  return DefWindowProcW(w,message,wp,lp);
}
}
void open_licenses(HWND owner,const std::wstring& directory) {
  if(window && IsWindow(window)){ShowWindow(window,SW_RESTORE);SetForegroundWindow(window);return;}
  // Load the Windows text control only on demand; startup performs no licence I/O.
  static HMODULE rich=LoadLibraryExW(L"Msftedit.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
  if(!rich){MessageBoxW(owner,L"Não foi possível abrir o leitor de licenças.",L"Cue",MB_OK|MB_ICONWARNING);return;}
  WNDCLASSEXW wc{sizeof wc};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"tinyoffice.Cue.licenses";
  wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(wc.hInstance,MAKEINTRESOURCEW(1));RegisterClassExW(&wc);
  auto owned=std::make_unique<View>();auto* v=owned.get();v->base=directory;
  RECT r{};GetWindowRect(owner,&r);
  window=CreateWindowExW(WS_EX_CONTROLPARENT,wc.lpszClassName,L"Cue · licenças e créditos",WS_OVERLAPPEDWINDOW,
    r.left+std::max<LONG>(0,(r.right-r.left-760)/2),r.top+std::max<LONG>(0,(r.bottom-r.top-560)/2),760,560,owner,nullptr,wc.hInstance,v);
  if(window){v->window_owns=true;owned.release();ShowWindow(window,SW_SHOW);SetForegroundWindow(window);}
}
bool licenses_message(MSG* m) {return window && IsWindow(window) && IsDialogMessageW(window,m);}
