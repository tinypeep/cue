// Cue · .tinypeep
#include <windows.h>
#include <shellapi.h>
#include <timeapi.h>

#include <string>
#include <vector>

#include "app.h"
#include "platform.h"
#include "single_instance.h"
#include "thumbhost.h"
#include "ui_language.h"

#include <cstdio>
#include <cstring>
#include <fstream>

// If anything goes wrong, leave a trace instead of vanishing silently.
static DWORD g_ui_thread = 0;

// Which module an address belongs to, and the offset inside it ("mpv.dll+0x1a2b3").
static std::string where(void* addr) {
  HMODULE m = nullptr;
  char name[MAX_PATH] = "?";
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         (LPCSTR)addr, &m) && m) {
    GetModuleFileNameA(m, name, MAX_PATH);
    const char* base = std::strrchr(name, '\\');
    char out[MAX_PATH + 32];
    std::snprintf(out, sizeof out, "%s+0x%llx", base ? base + 1 : name,
                  (unsigned long long)((uintptr_t)addr - (uintptr_t)m));
    return out;
  }
  return "unknown";
}

static LONG WINAPI on_crash(EXCEPTION_POINTERS* ep) {
  char line[600];
  const EXCEPTION_RECORD* er = ep->ExceptionRecord;
  std::string access;
  if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2) {
    char b[96];
    std::snprintf(b, sizeof b, " (%s %p)", er->ExceptionInformation[0] == 0 ? "read" : er->ExceptionInformation[0] == 1 ? "write" : "exec",
                  (void*)er->ExceptionInformation[1]);
    access = b;
  }
  std::snprintf(line, sizeof line, "crash: code 0x%08lx at %p in %s%s, %s thread", (unsigned long)er->ExceptionCode,
                er->ExceptionAddress, where(er->ExceptionAddress).c_str(), access.c_str(),
                GetCurrentThreadId() == g_ui_thread ? "ui" : "engine/other");
  std::ofstream f(std::filesystem::path(data_dir()) / "cue.log", std::ios::app);
  f << line << "\n";
  f.close();
  MessageBoxW(nullptr, widen(i18n::format(str::win_crash, {{"path", "%APPDATA%\\tinyoffice\\Cue\\cue.log"}})).c_str(),
              L"Cue", MB_OK | MB_ICONWARNING);
  return EXCEPTION_EXECUTE_HANDLER;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
  g_ui_thread = GetCurrentThreadId();
  SetUnhandledExceptionFilter(on_crash);
  // Per-Monitor v2: every surface is drawn in physical pixels at an integer art scale.
  using SetCtxFn = BOOL(WINAPI*)(HANDLE);
  if (auto fn = reinterpret_cast<SetCtxFn>(reinterpret_cast<void*>(
          GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"))))
    fn((HANDLE)-4 /*DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2*/);
  SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);

  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) args.push_back(narrow(argv[i]));
  LocalFree(argv);
  for (auto& a : args) {
    if (a == "--thumb-host") {
      // the preview helper: silent, no window; if it fails it just ends
      SetUnhandledExceptionFilter([](EXCEPTION_POINTERS*) -> LONG { ExitProcess(3); });
      SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
      return thumb_host_main();
    }
  }
  // the interface language, once per start: the saved choice, else Windows' display languages
  apply_ui_language(saved_language_pref(data_dir() + L"\\state.json"), windows_ui_languages());
  for (auto& a : args) {
    if (a == "--register") {
      std::string err;
      bool ok = register_open_with(&err);
      return ok ? 0 : 1;
    }
  }

  SingleInstance instance;
  auto route = instance.route(args);
  if (route == SingleInstance::Result::Forwarded) return 0;
  if (route == SingleInstance::Result::Error) {
    MessageBoxW(nullptr, widen(tr(str::win_forward_failed)).c_str(), L"Cue", MB_OK | MB_ICONWARNING);
    return 1;
  }

  if (FAILED(OleInitialize(nullptr))) return 1;  // drag & drop + file dialog
  static App app;  // lives for the whole process
  int code = 1;
  if (app.init(inst, args, show)) code = app.run();
  OleUninitialize();
  return code;
}
