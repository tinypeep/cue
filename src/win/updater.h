// Network side of updates: GitHub's API over WinHTTP, the installer downloaded to a temp
// folder and checked against the SHA-256 GitHub publishes for it, then handed to the installer.
// Everything runs on short-lived worker threads and reports back with PostMessage(msg, 0, UpdateMsg*).
#pragma once
#include <windows.h>

#include <atomic>
#include <string>

#include "../core/release.h"

struct UpdateMsg {
  enum Kind { Checked, Progress, Downloaded, Failed } kind = Failed;
  bool manual = false;
  bool found = false;        // Checked: a usable release was read (newer or not)
  ReleaseInfo rel;
  int percent = 0;           // Progress
  std::wstring file;         // Downloaded: the verified installer
  std::string error;         // for the log
};

void update_check_async(HWND hwnd, UINT msg, bool manual);
void update_download_async(HWND hwnd, UINT msg, const ReleaseInfo& rel);
// Starts the installer in update mode: it waits for this process to exit, installs over
// `install_dir` and opens Cue again. False when it could not be started.
bool update_launch_installer(const std::wstring& installer, const std::wstring& install_dir);
