// libmpv loaded at runtime (LoadLibrary). Declares only the stable C API
// subset Cue uses, so no mpv headers or import libs are needed to build.
#pragma once
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "../core/model.h"

extern "C" {
typedef struct mpv_handle mpv_handle;
typedef enum mpv_format {
  MPV_FORMAT_NONE = 0, MPV_FORMAT_STRING = 1, MPV_FORMAT_OSD_STRING = 2, MPV_FORMAT_FLAG = 3,
  MPV_FORMAT_INT64 = 4, MPV_FORMAT_DOUBLE = 5, MPV_FORMAT_NODE = 6, MPV_FORMAT_NODE_ARRAY = 7,
  MPV_FORMAT_NODE_MAP = 8, MPV_FORMAT_BYTE_ARRAY = 9
} mpv_format;
typedef enum mpv_event_id {
  MPV_EVENT_NONE = 0, MPV_EVENT_SHUTDOWN = 1, MPV_EVENT_LOG_MESSAGE = 2, MPV_EVENT_GET_PROPERTY_REPLY = 3,
  MPV_EVENT_SET_PROPERTY_REPLY = 4, MPV_EVENT_COMMAND_REPLY = 5, MPV_EVENT_START_FILE = 6, MPV_EVENT_END_FILE = 7,
  MPV_EVENT_FILE_LOADED = 8, MPV_EVENT_CLIENT_MESSAGE = 16, MPV_EVENT_VIDEO_RECONFIG = 17,
  MPV_EVENT_AUDIO_RECONFIG = 18, MPV_EVENT_SEEK = 20, MPV_EVENT_PLAYBACK_RESTART = 21,
  MPV_EVENT_PROPERTY_CHANGE = 22, MPV_EVENT_QUEUE_OVERFLOW = 24, MPV_EVENT_HOOK = 25
} mpv_event_id;
typedef struct mpv_node_list mpv_node_list;
typedef struct mpv_node {
  union { char* string; int flag; int64_t int64; double double_; mpv_node_list* list; void* ba; } u;
  mpv_format format;
} mpv_node;
struct mpv_node_list { int num; mpv_node* values; char** keys; };
typedef struct mpv_event { mpv_event_id event_id; int error; uint64_t reply_userdata; void* data; } mpv_event;
typedef struct mpv_event_property { const char* name; mpv_format format; void* data; } mpv_event_property;
typedef struct mpv_event_client_message { int num_args; const char** args; } mpv_event_client_message;
typedef struct mpv_byte_array { void* data; size_t size; } mpv_byte_array;
typedef struct mpv_event_command { mpv_node result; } mpv_event_command;
typedef struct mpv_event_hook { const char* name; uint64_t id; } mpv_event_hook;
typedef struct mpv_event_end_file { int reason; int error; int64_t playlist_entry_id; int64_t playlist_insert_id; int playlist_insert_num_entries; } mpv_event_end_file;
enum { MPV_END_FILE_REASON_EOF = 0, MPV_END_FILE_REASON_STOP = 2, MPV_END_FILE_REASON_QUIT = 3, MPV_END_FILE_REASON_ERROR = 4, MPV_END_FILE_REASON_REDIRECT = 5 };
}

struct MpvApi {
  unsigned long (*client_api_version)();
  mpv_handle* (*create)();
  int (*initialize)(mpv_handle*);
  void (*terminate_destroy)(mpv_handle*);
  int (*set_option_string)(mpv_handle*, const char*, const char*);
  int (*set_property_string)(mpv_handle*, const char*, const char*);
  int (*set_property)(mpv_handle*, const char*, mpv_format, void*);
  int (*set_property_async)(mpv_handle*, uint64_t, const char*, mpv_format, void*);
  int (*get_property)(mpv_handle*, const char*, mpv_format, void*);
  int (*get_property_async)(mpv_handle*, uint64_t, const char*, mpv_format);
  char* (*get_property_string)(mpv_handle*, const char*);
  int (*command)(mpv_handle*, const char**);
  int (*command_string)(mpv_handle*, const char*);
  int (*command_async)(mpv_handle*, uint64_t, const char**);
  int (*observe_property)(mpv_handle*, uint64_t, const char*, mpv_format);
  mpv_event* (*wait_event)(mpv_handle*, double);
  void (*set_wakeup_callback)(mpv_handle*, void (*)(void*), void*);
  void (*free)(void*);
  void (*free_node_contents)(mpv_node*);
  int (*hook_add)(mpv_handle*, uint64_t, const char*, int);
  int (*hook_continue)(mpv_handle*, uint64_t);
  const char* (*error_string)(int);
  int (*command_ret)(mpv_handle*, const char**, mpv_node*);  // optional (client API 1.103+)
};

class Mpv {
 public:
  // Looks for libmpv-2.dll (or mpv-2.dll / mpv-1.dll) next to Cue.exe.
  bool load(const std::wstring& dir, std::string* err);
  bool create(HWND notify, UINT msg, int64_t wid, std::string* err);
  void destroy();
  bool ready() const { return h_ != nullptr; }
  bool loaded() const { return dll_ != nullptr; }
  void forget() { h_ = nullptr; }  // the caller destroys the handle elsewhere

  void option(const char* k, const std::string& v);
  bool set(const char* k, const std::string& v);  // sync, returns success
  void set_async(const char* k, const std::string& v);
  void set_double_async(const char* k, double v);
  void set_flag_async(const char* k, bool v);
  int cmd(std::initializer_list<std::string> args, uint64_t reply = 0);  // < 0: not queued (no reply will come)
  int cmd_vec(const std::vector<std::string>& args, uint64_t reply = 0);
  bool cmd_batch(const std::vector<std::vector<std::string>>& commands);
  double get_double(const char* k, double def = 0);
  std::string get_string(const char* k);
  void observe(const char* k, mpv_format f, uint64_t id);
  mpv_event* wait() { return api_.wait_event(h_, 0); }
  void hook_add(const char* name, uint64_t id) { api_.hook_add(h_, id, name, 0); }
  void hook_continue(uint64_t id) { api_.hook_continue(h_, id); }
  const MpvApi& api() const { return api_; }
  mpv_handle* handle() const { return h_; }

  static std::vector<Track> parse_tracks(const mpv_node* n);

 private:
  HMODULE dll_ = nullptr;
  MpvApi api_{};
  mpv_handle* h_ = nullptr;
  HWND notify_ = nullptr;
  UINT msg_ = 0;
  static void wakeup(void* ctx);
};
