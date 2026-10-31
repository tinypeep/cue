// The remote control's portable half: everything the phone link needs that is not Windows.
// HTTP request parsing, the WebSocket handshake and frames, the JSON both sides speak, which
// files a folder shows, which file is the next episode, and who may pair. The Windows server
// (src/win/remote_server) only moves bytes and calls into this; tests run it on any OS.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace remote {

constexpr int kPort = 7878;          // tried first; the next few if it is taken
constexpr int kPortTries = 12;
constexpr size_t kMaxRequest = 16 * 1024;
constexpr size_t kMaxFrame = 8 * 1024;
constexpr double kPairLifetime = 120;
constexpr int kCodeDigits = 6;
constexpr size_t kMaxTokens = 8;     // phones remembered; the oldest is forgotten past this
constexpr size_t kMaxEvents = 256;
constexpr size_t kAuthEventLimit = 192; // reserve space for disconnects of all 24 clients
inline bool event_room(size_t queued, bool command) { return queued < (command ? 128 : kMaxEvents); }
inline bool auth_event_room(size_t queued) { return queued < kAuthEventLimit; }

// ── HTTP ────────────────────────────────────────────────────────────
struct Request {
  std::string method, path;          // path without the query, percent-decoded
  std::map<std::string, std::string> query, headers;  // header names in lower case
};
// 1 complete (header_len set), 0 needs more bytes, -1 malformed or too large
int parse_request(std::string_view buf, Request& out, size_t& header_len);
bool valid_authority(std::string_view host);
bool valid_ws_key(std::string_view key);
bool valid_token(std::string_view token);
// Small token bucket; each connection has its own bounded budget.
struct RateLimit {
  double credit = 40, last = -1;
  bool take(double now, double rate = 20, double burst = 40);
};
std::string url_decode(std::string_view s);
std::string url_encode(std::string_view s);
std::string http_response(int status, std::string_view type, std::string_view body, bool cache = false,
                          std::string_view nonce = {}, std::string_view authority = {});

// ── WebSocket (RFC 6455, text frames, no extensions) ────────────────
std::string sha1(std::string_view data);  // 20 raw bytes
std::string base64(std::string_view data);
std::string ws_accept(std::string_view key);
std::string ws_handshake(std::string_view key);
struct Frame { int opcode = 0; bool fin = true; std::string payload; };
// 1 one frame decoded (consumed set), 0 needs more, -1 protocol error (unmasked, too big)
int ws_decode(std::string_view buf, Frame& out, size_t& consumed);
std::string ws_encode(int opcode, std::string_view payload);  // server frames are never masked
enum : int { WS_TEXT = 1, WS_CLOSE = 8, WS_PING = 9, WS_PONG = 10 };

// ── who may connect ─────────────────────────────────────────────────
bool private_ipv4(uint32_t host_order);  // 10/8, 172.16/12, 192.168/16, 169.254/16, 127/8
std::string ipv4_text(uint32_t host_order);
std::string device_name(std::string_view user_agent);  // "iPhone", "iPad", "Android", or ""
// Pairing codes are tried by hand: a few mistakes are fine, guessing is not.
class CodeGuard {
 public:
  bool allowed(double now) const;  // false while locked
  void failed(double now);
  void succeeded() { fails_.clear(); }
 private:
  std::vector<double> fails_;
  double locked_until_ = -1;
};
std::string hex(std::string_view bytes);

// ── files ───────────────────────────────────────────────────────────
bool is_video_name(std::string_view name);
bool is_audio_name(std::string_view name);
inline bool is_playable_name(std::string_view n) { return is_video_name(n) || is_audio_name(n); }
// Natural order, case-insensitive: "ep 2" before "ep 10".
bool natural_less(std::string_view a, std::string_view b);
// "S01E03", "s1e3", "1x03", "Ep 3", "E03" -> season/episode (season 0 when unnamed)
bool episode_of(std::string_view name, int& season, int& episode, size_t* marker = nullptr);
// The file after `current` among its siblings: by season/episode when both have one, else by
// natural order. Only playable files count. "" when it is the last one.
bool same_series(std::string_view a, std::string_view b);
std::string next_file(std::string_view current, const std::vector<std::string>& siblings);
std::string next_episode(std::string_view current, const std::vector<std::string>& siblings);
// Two or more playable files numbered as episodes: shown as a series (a stack, not a folder).
bool looks_like_series(const std::vector<std::string>& names);
// `path` is `root` or inside it (case-insensitive, either slash, no ".." tricks).
bool path_within(std::string_view root, std::string_view path);

// ── what the two sides say ──────────────────────────────────────────
struct State {
  bool playing_view = false;     // a file is open (else the home)
  std::string file;              // shown name
  double pos = 0, dur = 0, speed = 1, volume = 100;
  bool paused = true, muted = false, ended = false, audio_only = false;
  bool has_subs = false, subs_on = false;
  double sub_delay = 0;
  int sub_size = 44, sub_size_min = 28, sub_size_max = 72;
  double sub_pos = 93, sub_pos_default = 93, sub_pos_min = 12, sub_pos_max = 97;
  int audio_tracks = 0, audio_index = 0;  // index into the audio tracks (0-based); -1: several
  int audio_mix = 0;                      // how many play together (when audio_index is -1)
  int timer = 0;                 // 0 off, -1 end of the episode, else minutes
  double timer_left = 0;         // seconds, for minutes
  bool next_auto = false;
  std::string next;              // the next episode's name, or ""
  std::string color = "#a0508f"; // the film's light (the phone glows with it)
};
std::string state_json(const State& s);

struct Command {
  std::string c;                 // see the switch in App::remote_command
  double v = 0;
  bool has_v = false;
  std::string path;
};
bool parse_command(std::string_view json, Command& out);

struct Entry {
  std::string name, path;
  bool dir = false, series = false, audio = false;
  double pos = 0, dur = 0;       // from the saved positions, when known
  bool seen = false;             // watched to the end (Cue starts it over next time)
};
std::string listing_json(const std::string& title, const std::string& path, const std::string& parent,
                         const std::vector<Entry>& entries);

std::string escape(std::string_view s);  // a JSON string, quoted

}  // namespace remote
