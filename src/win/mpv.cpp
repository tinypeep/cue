#include "mpv.h"

#include <cstring>
#include <array>

#include "../core/i18n.h"

template <class F>
static bool sym(HMODULE m, const char* name, F& out) {
  out = reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(m, name)));
  return out != nullptr;
}

bool Mpv::load(const std::wstring& dir, std::string* err) {
  const wchar_t* names[] = {L"libmpv-2.dll", L"mpv-2.dll", L"mpv-1.dll", L"mpv.dll"};
  for (auto n : names) {
    std::wstring full = dir + L"\\" + n;
    dll_ = LoadLibraryExW(full.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (dll_) break;
  }
  if (!dll_) { if (err) *err = i18n::key_name(str::engine_missing_dll); return false; }
  bool ok = sym(dll_, "mpv_client_api_version", api_.client_api_version) && sym(dll_, "mpv_create", api_.create) &&
            sym(dll_, "mpv_initialize", api_.initialize) && sym(dll_, "mpv_terminate_destroy", api_.terminate_destroy) &&
            sym(dll_, "mpv_set_option_string", api_.set_option_string) &&
            sym(dll_, "mpv_set_property_string", api_.set_property_string) &&
            sym(dll_, "mpv_set_property", api_.set_property) &&
            sym(dll_, "mpv_set_property_async", api_.set_property_async) &&
            sym(dll_, "mpv_get_property", api_.get_property) &&
            sym(dll_, "mpv_get_property_async", api_.get_property_async) &&
            sym(dll_, "mpv_get_property_string", api_.get_property_string) && sym(dll_, "mpv_command", api_.command) &&
            sym(dll_, "mpv_command_async", api_.command_async) &&
            sym(dll_, "mpv_observe_property", api_.observe_property) && sym(dll_, "mpv_wait_event", api_.wait_event) &&
            sym(dll_, "mpv_set_wakeup_callback", api_.set_wakeup_callback) && sym(dll_, "mpv_free", api_.free) &&
            sym(dll_, "mpv_free_node_contents", api_.free_node_contents) && sym(dll_, "mpv_hook_add", api_.hook_add) &&
            sym(dll_, "mpv_hook_continue", api_.hook_continue) && sym(dll_, "mpv_error_string", api_.error_string);
  if (!ok) { if (err) *err = i18n::key_name(str::engine_too_old); FreeLibrary(dll_); dll_ = nullptr; return false; }
  sym(dll_, "mpv_command_ret", api_.command_ret);  // optional
  // every symbol above exists since client API 1.100 (mpv 0.28); newer options fall back by name
  unsigned long v = api_.client_api_version();
  if (v < ((1ul << 16) | 100)) { if (err) *err = i18n::key_name(str::engine_too_old); return false; }
  return true;
}

void Mpv::wakeup(void* ctx) {
  auto* self = static_cast<Mpv*>(ctx);
  PostMessageW(self->notify_, self->msg_, 0, 0);  // thread-safe; events are drained on the UI thread
}

bool Mpv::create(HWND notify, UINT msg, int64_t wid, std::string* err) {
  if (!dll_) return false;
  h_ = api_.create();
  if (!h_) { if (err) *err = i18n::key_name(str::engine_failed); return false; }
  notify_ = notify;
  msg_ = msg;
  if (wid != 0) option("wid", std::to_string(wid));
  (void)err;
  return true;
}

void Mpv::destroy() {
  if (h_) { api_.terminate_destroy(h_); h_ = nullptr; }
}

void Mpv::option(const char* k, const std::string& v) { if (h_) api_.set_option_string(h_, k, v.c_str()); }

bool Mpv::set(const char* k, const std::string& v) { return h_ && api_.set_property_string(h_, k, v.c_str()) >= 0; }

bool Mpv::set_any(const std::vector<std::string>& names, const std::string& v) {
  for (auto& n : names)
    if (set(n.c_str(), v)) return true;
  return false;
}

void Mpv::set_async(const char* k, const std::string& v) {
  if (!h_) return;
  const char* s = v.c_str();
  api_.set_property_async(h_, 0, k, MPV_FORMAT_STRING, &s);  // mpv copies the value
}
void Mpv::set_double_async(const char* k, double v) { if (h_) api_.set_property_async(h_, 0, k, MPV_FORMAT_DOUBLE, &v); }
void Mpv::set_flag_async(const char* k, bool v) {
  int f = v ? 1 : 0;
  if (h_) api_.set_property_async(h_, 0, k, MPV_FORMAT_FLAG, &f);
}

int Mpv::cmd_vec(const std::vector<std::string>& args, uint64_t reply) {
  if (!h_) return -1;
  std::vector<const char*> a;
  a.reserve(args.size() + 1);
  for (auto& s : args) a.push_back(s.c_str());
  a.push_back(nullptr);
  return api_.command_async(h_, reply, a.data());  // arguments are copied before returning
}
int Mpv::cmd(std::initializer_list<std::string> args, uint64_t reply) {
  if (!h_) return -1;
  // All current commands, including overlay-add, fit without heap allocations.
  std::array<const char*, 16> pointers{};
  if (args.size() >= pointers.size()) return cmd_vec(std::vector<std::string>(args), reply);
  size_t i = 0;
  for (const auto& arg : args) pointers[i++] = arg.c_str();
  return api_.command_async(h_, reply, pointers.data());
}

double Mpv::get_double(const char* k, double def) {
  double v = def;
  if (!h_ || api_.get_property(h_, k, MPV_FORMAT_DOUBLE, &v) < 0) return def;
  return v;
}

std::string Mpv::get_string(const char* k) {
  if (!h_) return "";
  char* s = api_.get_property_string(h_, k);
  if (!s) return "";
  std::string r = s;
  api_.free(s);
  return r;
}

void Mpv::observe(const char* k, mpv_format f, uint64_t id) { if (h_) api_.observe_property(h_, id, k, f); }

static const mpv_node* node_get(const mpv_node* m, const char* key) {
  if (!m || m->format != MPV_FORMAT_NODE_MAP || !m->u.list) return nullptr;
  for (int i = 0; i < m->u.list->num; ++i)
    if (std::strcmp(m->u.list->keys[i], key) == 0) return &m->u.list->values[i];
  return nullptr;
}
static std::string node_str(const mpv_node* n) { return (n && n->format == MPV_FORMAT_STRING && n->u.string) ? n->u.string : ""; }
static bool node_flag(const mpv_node* n) { return n && n->format == MPV_FORMAT_FLAG && n->u.flag; }
static int64_t node_int(const mpv_node* n) {
  if (!n) return 0;
  if (n->format == MPV_FORMAT_INT64) return n->u.int64;
  if (n->format == MPV_FORMAT_DOUBLE) return (int64_t)n->u.double_;
  return 0;
}

std::vector<Track> Mpv::parse_tracks(const mpv_node* n) {
  std::vector<Track> out;
  if (!n || n->format != MPV_FORMAT_NODE_ARRAY || !n->u.list) return out;
  for (int i = 0; i < n->u.list->num; ++i) {
    const mpv_node* t = &n->u.list->values[i];
    Track tr;
    tr.id = node_int(node_get(t, "id"));
    tr.type = node_str(node_get(t, "type"));
    tr.lang = node_str(node_get(t, "lang"));
    tr.title = node_str(node_get(t, "title"));
    tr.codec = node_str(node_get(t, "codec"));
    tr.selected = node_flag(node_get(t, "selected"));
    tr.external = node_flag(node_get(t, "external"));
    tr.forced = node_flag(node_get(t, "forced"));
    tr.is_default = node_flag(node_get(t, "default"));
    tr.albumart = node_flag(node_get(t, "albumart"));
    out.push_back(tr);
  }
  return out;
}
