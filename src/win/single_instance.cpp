#include "single_instance.h"

#include <filesystem>

#include "../core/launch_request.h"
#include "platform.h"

namespace {
constexpr ULONG_PTR kOpenRequest = 0x43554501;  // Cue protocol v1
}

SingleInstance::~SingleInstance() {
  if (owned_) ReleaseMutex(mutex_);
  if (mutex_) CloseHandle(mutex_);
}

SingleInstance::Result SingleInstance::route(const std::vector<std::string>& args) {
  // Local scopes this to the interactive Windows session. Ownership survives
  // startup and is automatically released by Windows after a crash.
  mutex_ = CreateMutexW(nullptr, FALSE, L"Local\\tinyoffice.Cue.instance.v1");
  if (!mutex_) return Result::Error;
  DWORD wait = WaitForSingleObject(mutex_, 0);
  if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED) {
    owned_ = true;
    return Result::Primary;
  }
  if (wait != WAIT_TIMEOUT) return Result::Error;

  std::vector<std::string> files;
  for (const auto& arg : args) {
    if (arg.empty() || arg[0] == '-') continue;
    std::wstring path = widen(arg);
    // The existing process may have a different working directory. Preserve
    // URLs and resolve relative local filenames before crossing the boundary.
    if (path.find(L"://") == std::wstring::npos) {
      std::error_code ec;
      auto absolute = std::filesystem::absolute(std::filesystem::path(path), ec);
      if (ec) return Result::Error;
      path = absolute.wstring();
    }
    files.push_back(narrow(path));
  }
  std::string payload = launch_request::encode(files);
  if (payload.empty()) return Result::Error;
  COPYDATASTRUCT data{kOpenRequest, static_cast<DWORD>(payload.size()), payload.data()};

  // Two Explorer launches may race before the first window exists. Wait for
  // that window, or take ownership if its process exits during startup.
  const ULONGLONG deadline = GetTickCount64() + 10000;
  do {
    if (HWND target = FindWindowW(L"tinyoffice.Cue", nullptr)) {
      DWORD pid = 0;
      GetWindowThreadProcessId(target, &pid);
      if (pid) AllowSetForegroundWindow(pid);
      DWORD_PTR accepted = 0;
      if (SendMessageTimeoutW(target, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data),
                              SMTO_ABORTIFHUNG | SMTO_BLOCK, 3000, &accepted)) {
        if (accepted == TRUE) return Result::Forwarded;
      } else if (IsWindow(target)) {
        // A timeout can leave the original delivery in flight: do not resend.
        return Result::Error;
      }
    }
    wait = WaitForSingleObject(mutex_, 50);
    if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED) {
      owned_ = true;
      return Result::Primary;
    }
    if (wait != WAIT_TIMEOUT) return Result::Error;
  } while (GetTickCount64() < deadline);
  // Do not open a duplicate window when the original process is unresponsive.
  return Result::Error;
}

bool SingleInstance::decode(const COPYDATASTRUCT* data, std::vector<std::string>& files) {
  files.clear();
  return data && data->dwData == kOpenRequest && launch_request::decode(data->lpData, data->cbData, files);
}
