#pragma once
#include <windows.h>
#include "../core/ui_scale.h"
#include <ole2.h>
#include <shellapi.h>

#include <functional>
#include <string>
#include <vector>

std::wstring widen(const std::string& s);
std::string narrow(const std::wstring& s);
std::wstring exe_dir();
std::wstring data_dir();  // %APPDATA%\tinyoffice\Cue (created)

enum class DialogKind { Video, Subtitle };
std::vector<std::string> open_file_dialog(HWND owner, DialogKind kind);
std::vector<std::string> clipboard_files(HWND owner);
void show_in_folder(const std::wstring& file);  // Explorer opens its folder with the file selected (never blocks)
// the remote's shared folders: pick one, name one the way Explorer does ("Vídeos"), and the
// usual places (Videos, Downloads, Desktop) wherever this person keeps them
std::string pick_folder_dialog(HWND owner);
std::string folder_display_name(const std::string& path);
std::vector<std::string> default_media_folders();  // Videos, Downloads, Desktop ("" when unknown)
bool register_open_with(std::string* err);  // HKCU only, never steals defaults
void refresh_open_with();  // refresh stale name/path once; no writes when already current

// The art-px scale actually used: DPI first, then the window decides the in-between cases.
// Choices: -1 small, 0 default, 2 medium (half step), 1 large.
// ui_scale lives in core/ui_scale.h so all DPI/size combinations are testable.
bool system_reduce_motion();
bool system_high_contrast();

// OLE drop target for the home. Reports position while dragging so the eyes can follow.
class DropTarget : public IDropTarget {
 public:
  std::function<void(bool has_files, POINT client)> on_enter;
  std::function<void(POINT client)> on_over;
  std::function<void()> on_leave;
  std::function<void(const std::vector<std::string>&)> on_drop;
  explicit DropTarget(HWND h) : hwnd_(h) {}
  virtual ~DropTarget() = default;

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
  ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref_); }
  ULONG STDMETHODCALLTYPE Release() override;
  HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD keys, POINTL pt, DWORD* effect) override;
  HRESULT STDMETHODCALLTYPE DragOver(DWORD keys, POINTL pt, DWORD* effect) override;
  HRESULT STDMETHODCALLTYPE DragLeave() override;
  HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD keys, POINTL pt, DWORD* effect) override;

 private:
  HWND hwnd_;
  LONG ref_ = 1;
  bool ok_ = false;
  POINT to_client(POINTL pt) const;
};

std::vector<std::string> files_from_hdrop(HDROP drop);
