#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include "remote_server.h"
#include <charconv>

#include <bcrypt.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>

#include "platform.h"
#include "../core/json.h"
#include "cover_image.h"
#include "remote_gate.h"

namespace {
#include "remote_page.inc"

double clock_s() { return GetTickCount64() / 1000.0; }

std::string random_bytes(size_t n) {
  std::string b(n, '\0');
  if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(b.data()), (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
    return {};  // fail closed: predictable credentials must never leave this process
  }
  return b;
}

std::string lower_path(const std::string& p) {
  std::string r;
  for (char c : p) r += c == '/' ? '\\' : (char)std::tolower((unsigned char)c);
  return r;
}

// Bind Host to the actual local endpoint as well as rejecting DNS names.
bool host_ok(SOCKET socket, const std::string& host, int port) {
  if (!remote::valid_authority(host)) return false;
  sockaddr_in local{};
  int n = sizeof local;
  if (getsockname(socket, reinterpret_cast<sockaddr*>(&local), &n)) return false;
  std::string ip = remote::ipv4_text(ntohl(local.sin_addr.s_addr));
  std::string suffix = port == 80 ? "" : ":" + std::to_string(port);
  return host == ip + suffix || ((ntohl(local.sin_addr.s_addr) >> 24) == 127 && host == "localhost" + suffix);
}

bool send_all(SOCKET s, const std::string& data) {
  size_t off = 0;
  while (off < data.size()) {
    int n = send(s, data.data() + off, (int)std::min<size_t>(data.size() - off, 1 << 20), 0);
    if (n <= 0) return false;
    off += (size_t)n;
  }
  return true;
}

// ── thumbnails: what Explorer would show, shrunk to the remote's pixel size ──
constexpr int kThumbW = 84, kThumbH = 48;  // shown doubled on the phone, pixel for pixel

std::string bmp24(const std::vector<uint32_t>& px, int w, int h) {
  int row = (w * 3 + 3) & ~3;
  std::string d(54 + (size_t)row * h, '\0');
  auto put32 = [&](size_t o, uint32_t v) { for (int i = 0; i < 4; ++i) d[o + i] = (char)(v >> (8 * i)); };
  d[0] = 'B'; d[1] = 'M';
  put32(2, (uint32_t)d.size()); put32(10, 54); put32(14, 40); put32(18, (uint32_t)w); put32(22, (uint32_t)h);
  d[26] = 1; d[28] = 24; put32(34, (uint32_t)row * h);
  for (int y = 0; y < h; ++y) {
    char* out = d.data() + 54 + (size_t)(h - 1 - y) * row;  // bottom-up
    for (int x = 0; x < w; ++x) {
      uint32_t v = px[(size_t)y * w + x];
      out[x * 3 + 0] = (char)(v & 0xFF);
      out[x * 3 + 1] = (char)((v >> 8) & 0xFF);
      out[x * 3 + 2] = (char)((v >> 16) & 0xFF);
    }
  }
  return d;
}

// What Explorer shows for a file, as BGRA pixels about W x H or bigger.
static bool shell_pixels(const std::string& path, int W, int H, std::vector<uint32_t>& src, int& w, int& h) {
  bool ok = false;
  IShellItemImageFactory* f = nullptr;
  if (SUCCEEDED(SHCreateItemFromParsingName(widen(path).c_str(), nullptr, IID_PPV_ARGS(&f)))) {
    HBITMAP hb = nullptr;
    if (SUCCEEDED(f->GetImage(SIZE{W, H}, SIIGBF_THUMBNAILONLY | SIIGBF_BIGGERSIZEOK, &hb)) && hb) {
      BITMAP bm{};
      GetObjectW(hb, sizeof bm, &bm);
      w = bm.bmWidth; h = std::abs(bm.bmHeight);
      if (w > 0 && h > 0 && w <= 2048 && h <= 2048) {
        src.assign((size_t)w * h, 0);
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;  // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        HDC dc = GetDC(nullptr);
        ok = GetDIBits(dc, hb, 0, (UINT)h, src.data(), &bi, DIB_RGB_COLORS) == h;
        ReleaseDC(nullptr, dc);
      }
      DeleteObject(hb);
    }
    f->Release();
  }
  return ok;
}

// A file's picture shrunk to W x H. The picture covers a centred tile `tw` wide (W for a film's
// frame; a square for music, whose covers are square). Music reads its own cover first (Windows
// shows none for Opus, Ogg and many other songs), then asks the shell like any file.
static std::string make_image(const std::string& path, int W, int H, int tw) {
  std::string out;
  HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  std::vector<uint32_t> src;
  int w = 0, h = 0;
  bool ok = remote::is_audio_name(path) && cover_image(path, src, w, h, 512);
  if (!ok) ok = shell_pixels(path, W * 4, H * 4, src, w, h);
  if (ok) {
    // cover the tile: crop the middle, then average each target pixel's box.
    // Music keeps its square cover whole, centred in the tile on black.
    const int ox = (W - tw) / 2;
    double ar = (double)tw / H;
    double cw = w, ch = h, cx = 0, cy = 0;
    if (w / (double)h > ar) { cw = h * ar; cx = (w - cw) / 2; } else { ch = w / ar; cy = (h - ch) / 2; }
    std::vector<uint32_t> dst((size_t)W * H);
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < tw; ++x) {
        int x0 = (int)(cx + x * cw / tw), x1 = std::max(x0 + 1, (int)(cx + (x + 1) * cw / tw));
        int y0 = (int)(cy + y * ch / H), y1 = std::max(y0 + 1, (int)(cy + (y + 1) * ch / H));
        uint64_t r = 0, g = 0, b = 0, n = 0;
        for (int yy = y0; yy < std::min(y1, h); ++yy)
          for (int xx = x0; xx < std::min(x1, w); ++xx) {
            uint32_t v = src[(size_t)yy * w + xx];
            b += v & 0xFF; g += (v >> 8) & 0xFF; r += (v >> 16) & 0xFF; ++n;
          }
        if (!n) n = 1;
        dst[(size_t)y * W + ox + x] = (uint32_t)(r / n) << 16 | (uint32_t)(g / n) << 8 | (uint32_t)(b / n);
      }
    // a blank answer (an all-black first frame, a shell with nothing to show) is no
    // picture: the phone draws its own pixel landscape instead
    int lo = 255, hi = 0;
    for (uint32_t v : dst) {
      int l = (int)(((v >> 16) & 0xFF) * 3 + ((v >> 8) & 0xFF) * 6 + (v & 0xFF)) / 10;
      lo = std::min(lo, l);
      hi = std::max(hi, l);
    }
    if (hi - lo > 3) out = bmp24(dst, W, H);
  }
  if (SUCCEEDED(co)) CoUninitialize();
  return out;
}

std::string make_thumb(const std::string& path, bool square) {
  return make_image(path, kThumbW, kThumbH, square ? kThumbH : kThumbW);
}

// The phone's face for a song: its cover, small enough to read as pixels at double size.
std::string make_cover(const std::string& path, int n) { return make_image(path, n, n, n); }
constexpr int kCoverPx = 72;  // shown at 2x: big, honest pixels, like the eyes it replaces

// Gate lives in remote_gate.h so its concurrency contract is testable on its own.

struct ThumbCache {
  std::mutex mu;
  std::map<std::string, std::string> items;  // lower-case path -> BMP ("" = none)
  std::deque<std::string> order;
  bool get(const std::string& k, std::string& out) {
    std::lock_guard<std::mutex> l(mu);
    auto it = items.find(k);
    if (it == items.end()) return false;
    out = it->second;
    return true;
  }
  void clear() { std::lock_guard<std::mutex> l(mu); items.clear(); order.clear(); }
  void put(const std::string& k, std::string v) {
    std::lock_guard<std::mutex> l(mu);
    if (!items.count(k)) order.push_back(k);
    items[k] = std::move(v);
    while (order.size() > 400) { items.erase(order.front()); order.pop_front(); }
  }
};

// ── folder listings ─────────────────────────────────────────────────
using Roots = std::vector<RemoteServer::Root>;
using ProgressMap = std::map<std::string, RemoteServer::Progress>;

const RemoteServer::Root* root_of(const Roots& roots, const std::string& path) {
  const RemoteServer::Root* best = nullptr;
  for (auto& r : roots)
    if (remote::path_within(r.path, path) && (!best || r.path.size() > best->path.size())) best = &r;
  return best;
}

std::vector<std::string> names_in(const std::string& dir, size_t cap) {
  std::vector<std::string> out;
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileExW(widen(dir + "\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
  if (h == INVALID_HANDLE_VALUE) return out;
  size_t scanned = 0;
  do {
    ++scanned;
    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) out.push_back(narrow(fd.cFileName));
  } while (scanned < cap && FindNextFileW(h, &fd));
  FindClose(h);
  return out;
}

int list_json(const std::string& path, const Roots& roots, const ProgressMap& progress, std::string& body) {
  std::vector<remote::Entry> entries;
  if (path.empty()) {
    for (auto& r : roots) entries.push_back({r.name, r.path, true, false, false, 0, 0, false});
    body = remote::listing_json("", "", "", entries);
    return 200;
  }
  const RemoteServer::Root* root = root_of(roots, path);
  if (!root || !remote_path_allowed(root->path, path)) return 403;
  std::string dir = path;
  while (dir.size() > 3 && (dir.back() == '\\' || dir.back() == '/')) dir.pop_back();
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileExW(widen(dir + "\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
  if (h == INVALID_HANDLE_VALUE) return 404;
  std::vector<remote::Entry> dirs, files;
  size_t scanned = 0;
  std::string base = dir.back() == '\\' ? dir : dir + "\\";
  do {
    ++scanned;
    std::string name = narrow(fd.cFileName);
    if (name == "." || name == ".." || (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_REPARSE_POINT))) continue;
    remote::Entry e;
    e.name = name;
    e.path = base + name;
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      e.dir = true;
      if (dirs.size() < 12) e.series = remote::looks_like_series(names_in(e.path, 300));
      dirs.push_back(e);
    } else if (remote::is_playable_name(name)) {
      e.audio = remote::is_audio_name(name);
      auto it = progress.find(lower_path(e.path));
      if (it != progress.end()) { e.pos = it->second.pos; e.dur = it->second.dur; e.seen = it->second.seen; }
      files.push_back(e);
    }
  } while (scanned < 10000 && dirs.size() + files.size() < 1500 && FindNextFileW(h, &fd));
  FindClose(h);
  auto by_name = [](const remote::Entry& a, const remote::Entry& b) { return remote::natural_less(a.name, b.name); };
  std::sort(dirs.begin(), dirs.end(), by_name);
  std::sort(files.begin(), files.end(), by_name);
  entries = std::move(dirs);
  entries.insert(entries.end(), files.begin(), files.end());
  // "Vídeos ▸ minha-serie": the shared folder's name, then the way down
  std::string title = root->name, parent;
  bool at_root = remote::path_within(dir, root->path);
  if (!at_root) {
    std::string rel = dir.substr(std::min(dir.size(), root->path.size()));
    for (char& c : rel) if (c == '/') c = '\\';
    size_t k = 0;
    while (k < rel.size()) {
      size_t e = rel.find('\\', k);
      if (e == std::string::npos) e = rel.size();
      if (e > k) title += " ▸ " + rel.substr(k, e - k);
      k = e + 1;
    }
    parent = dir.substr(0, dir.find_last_of("\\/"));
    if (parent.size() == 2 && parent[1] == ':') parent += '\\';
  }
  body = remote::listing_json(title, dir, parent, entries);
  return 200;
}

struct Client {
  SOCKET s = INVALID_SOCKET;
  std::string in, out;
  bool ws = false, authenticated = false, closing = false;
  std::string credential, instance;  // the credential; never sent, only expected
  uint32_t peer = 0;                // the address the phone is at, for the pairing lockout
  std::string nonce;                // the challenge this connection must answer
  std::string sid;                  // the session its HTTP requests sign with
  remote::RateLimit messages;       // the phone's own messages (WebSocket frames)
  remote::RateLimit requests;       // the phone's own requests (HTTP)
  double accepted = 0;
  std::string device;
  double last_rx = 0, last_ping = 0;
  uint64_t sent_rev = 0, peek_sent = 0;
};
}  // namespace

struct RemoteServer::Impl {
  HWND hwnd = nullptr;
  UINT msg = 0;
  SOCKET lsock = INVALID_SOCKET;
  int port = 0;
  std::thread th;
  std::atomic<bool> quit{false}, running{false};
  std::atomic<int> nclients{0};
  std::shared_ptr<std::atomic<int>> workers = std::make_shared<std::atomic<int>>(0);
  bool tell = false;
  bool wsa = false;
  std::string font;

  std::mutex mu;  // everything below
  std::vector<RemoteServer::QueueFile> queue_files;
  std::string state, boot, page, queue_json="{\"items\":[]}";
  uint64_t state_rev = 0;
  bool page_dirty = true;
  Roots roots;
  ProgressMap progress;
  // The credentials the phones hold. The credential never crosses the wire
  // after pairing: a connection proves it by answering a challenge. The
  // state file holds it encrypted (DPAPI on Windows), the server sees it
  // only to expect it.
  std::unordered_map<std::string, std::string> credentials;  // kid -> credential
  bool pairing = false;
  std::string code;
  double pair_until = 0;
  std::shared_ptr<std::atomic<uint64_t>> generation = std::make_shared<std::atomic<uint64_t>>(0);
  remote::CodeGuard guard;                       // everyone together: guessing from many addresses
  std::map<uint32_t, remote::CodeGuard> guards;  // per address: a wrong guess locks that address only
  // The sessions the open connections hold. Every HTTP request is signed with
  // the session of the connection that opened it; the session dies with it.
  struct Session { std::string key; uint64_t last_counter = 0; std::set<uint64_t> used; };
  std::unordered_map<std::string, Session> sessions;
  std::vector<Event> events;
  bool posted = false;
  std::shared_ptr<ThumbCache> thumbs = std::make_shared<ThumbCache>();
  std::shared_ptr<Gate> gate = std::make_shared<Gate>();
  std::string pc;           // the computer's name, for the phone's "connected" moment
  std::string peek;         // the latest timeline preview asked by a phone (BMP)
  std::string cover_path;   // the song playing, whose cover the phone wears as its face
  uint64_t peek_rev = 0;
  double peek_at = 0;

  std::vector<Client> clients;

  void post(Event e) {
    std::lock_guard<std::mutex> l(mu);
    // Commands cannot starve connection lifecycle events or grow the UI queue without bound.
    if (!remote::event_room(events.size(), e.kind == Event::Command)) return;
    events.push_back(std::move(e));
    if (!posted && hwnd) posted = PostMessageW(hwnd, msg, 0, 0) != 0;
  }
  void queue(Client& c, const std::string& data) {
    if (c.out.size() + data.size() > 512 * 1024) { c.closing = true; c.out.clear(); return; }
    c.out += data;
    flush(c);
  }
  void flush(Client& c) {
    while (!c.out.empty()) {
      int n = send(c.s, c.out.data(), (int)std::min<size_t>(c.out.size(), 1 << 16), 0);
      if (n > 0) { c.out.erase(0, (size_t)n); continue; }
      if (WSAGetLastError() != WSAEWOULDBLOCK) { c.closing = true; c.out.clear(); }
      break;
    }
  }
  std::string page_html() {
    std::lock_guard<std::mutex> l(mu);
    if (page_dirty) {
      page.clear();
      for (const char* p : kRemotePage) page += p;
      size_t k = page.find("/*CUE_BOOT*/");
      if (k != std::string::npos) page.replace(k, 12, boot);
      page_dirty = false;
    }
    return page;
  }
  void handle_http(size_t idx, const remote::Request& r);
  void authenticate(Client& c, const std::string& payload);
  // The credential a phone presents is proven by the challenge, never sent.
  bool credential_ok(const std::string& kid, const std::string& proof, const std::string& nonce) const {
    if (kid.size() != 16 || proof.size() != 64 || nonce.empty()) return false;
    auto it = credentials.find(kid);
    if (it == credentials.end()) return false;
    return remote::same_secret(remote::hex(remote::hmac_sha256(it->second, nonce)), proof);
  }
  // Whether the credential is one of ours (a phone that was paired and is now
  // turned off is forgotten at once).
  bool known(const std::string& credential) const {
    if (!remote::valid_token(credential)) return false;
    return credentials.count(remote::credential_kid(credential)) != 0;
  }
  // A request is signed by the session of the connection that made it.
  bool session_ok(const std::string& sid, uint64_t n, const std::string& proof, const std::string& payload) {
    auto it = sessions.find(sid);
    if (it == sessions.end()) return false;
    auto& s = it->second;
    if (n + 64 <= s.last_counter) return false;  // too old: the page has moved on
    if (n > s.last_counter + 64) return false;   // too far ahead: not reachable
    if (s.used.count(n)) return false;           // already used: a replay
    if (!remote::same_secret(remote::hex(remote::hmac_sha256(s.key, payload)), proof)) return false;
    s.used.insert(n);
    if (n > s.last_counter) s.last_counter = n;
    while (s.used.size() > 64) s.used.erase(s.used.begin());  // the oldest, first
    return true;
  }
  void handle_frames(Client& c);
  void loop();
  void drop(size_t idx);
};

void RemoteServer::Impl::drop(size_t idx) {
  Client& c = clients[idx];
  if (c.s != INVALID_SOCKET) closesocket(c.s);
  // The session dies with the connection: the phone cannot sign anything once
  // it is gone, and it must prove itself again when it reconnects.
  if (!c.sid.empty()) {
    std::lock_guard<std::mutex> l(mu);
    sessions.erase(c.sid);
    c.sid.clear();
  }
  if (c.authenticated) {
    --nclients;
    Event e;
    e.kind = Event::Disconnected;
    e.device = c.device;
    post(e);
  }
  clients.erase(clients.begin() + (long)idx);
}

void RemoteServer::Impl::handle_http(size_t idx, const remote::Request& r) {
  Client& c = clients[idx];
  auto reply = [&](int status, const char* type, const std::string& body, bool cache = false, const std::string& nonce = "") {
    auto host = r.headers.find("host");
    queue(c, remote::http_response(status, type, body, cache, nonce, host == r.headers.end() ? "" : host->second));
    c.closing = true;
  };
  auto hdr = [&](const char* k) { auto it = r.headers.find(k); return it == r.headers.end() ? std::string() : it->second; };
  if (!host_ok(c.s, hdr("host"), port)) return reply(403, "text/plain", "forbidden");
  std::string origin = hdr("origin");
  if ((!origin.empty() && origin != "http://" + hdr("host")) || hdr("sec-fetch-site") == "cross-site")
    return reply(403, "text/plain", "forbidden");
  // The page signs every request with the session of the connection that opened
  // it: Authorization: Cue <sid>:<counter>:<hmac(sid|method|path|counter)>.
  auto signed_ok = [&] {
    std::string a = hdr("authorization");
    if (a.rfind("Cue ", 0) != 0) return false;
    a = a.substr(4);
    const size_t p1 = a.find(':');
    if (p1 == std::string::npos) return false;
    const size_t p2 = a.find(':', p1 + 1);
    if (p2 == std::string::npos) return false;
    const std::string sid = a.substr(0, p1), nstr = a.substr(p1 + 1, p2 - p1 - 1), proof = a.substr(p2 + 1);
    if (sid.size() != 16 || proof.size() != 64 || nstr.empty() || nstr.size() > 18) return false;
    uint64_t n = 0;
    for (char c : nstr) {
      if (c < '0' || c > '9') return false;
      n = n * 10 + (uint64_t)(c - '0');
    }
    std::lock_guard<std::mutex> l(mu);
    return session_ok(sid, n, proof, sid + "\n" + r.method + "\n" + r.path + "\n" + nstr);
  };
  if (r.method != "GET") return reply(405, "text/plain", "");
  // Every phone has a budget for its requests: one that keeps asking for
  // listings and thumbnails must not keep the workers and the disk busy.
  if (!c.requests.take(clock_s(), 40, 120)) return reply(429, "text/plain", "");
  auto q = [&](const char* k) { auto it = r.query.find(k); return it == r.query.end() ? std::string() : it->second; };

  if (r.path == "/" || r.path == "/index.html") {
    std::string nonce = remote::hex(random_bytes(16)), html = page_html();
    if (nonce.size() != 32) return reply(503, "text/plain", "");
    size_t script = html.find("<script>");
    if (script == std::string::npos) return reply(503, "text/plain", "");
    html.replace(script, 8, "<script nonce=\"" + nonce + "\">");
    return reply(200, "text/html; charset=utf-8", html, false, nonce);
  }
  if (r.path == "/font.ttf") {
    if (font.empty()) return reply(404, "text/plain", "");
    return reply(200, "font/ttf", font, true);
  }
  if (r.path == "/ws") {
    std::string key = hdr("sec-websocket-key"), up = hdr("upgrade"), conn = hdr("connection");
    std::transform(up.begin(), up.end(), up.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
    std::transform(conn.begin(), conn.end(), conn.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
    if (origin != "http://" + hdr("host")) return reply(403, "text/plain", "forbidden");
    if (!remote::valid_ws_key(key) || up != "websocket" || hdr("sec-websocket-version") != "13" ||
        conn.find("upgrade") == std::string::npos || !r.query.empty()) return reply(400, "text/plain", "");
    queue(c, remote::ws_handshake(key));
    c.ws = true;
    c.device = remote::device_name(hdr("user-agent"));
    c.last_rx = c.last_ping = clock_s();
    // The challenge: the phone proves it holds the credential by answering it.
    c.nonce = remote::hex(random_bytes(16));
    if (c.nonce.size() == 32) queue(c, remote::ws_encode(remote::WS_TEXT, "{\"t\":\"chal\",\"n\":\"" + c.nonce + "\"}"));
    return;  // first masked text frame authenticates; no protected data before it
  }
  if (r.path == "/peek") {
    if (!signed_ok()) return reply(403, "text/plain", "");
    std::string img;
    {
      std::lock_guard<std::mutex> l(mu);
      img = peek;
    }
    if (img.empty()) return reply(404, "text/plain", "");
    return reply(200, "image/bmp", img);
  }
  if(r.path=="/queue") {
    if (!signed_ok()) return reply(403, "text/plain", "");
    std::string body;
    {std::lock_guard<std::mutex> l(mu); body=queue_json;}
    return reply(200,"application/json; charset=utf-8",body);
  }
  if (r.path == "/list" || r.path == "/thumb" || r.path == "/queue-thumb" || r.path == "/cover") {
    Roots roots_copy;
    std::string queue_path;
    ProgressMap progress_copy;
    if (!signed_ok()) return reply(403, "text/plain", "");
    {
      std::lock_guard<std::mutex> l(mu);
      roots_copy = roots;
      if (r.path == "/cover") {
        queue_path = cover_path;  // only the song playing now, never a path the phone names
        if (queue_path.empty()) return reply(404, "text/plain", "");
      }
      if(r.path=="/queue-thumb") {
        const auto value=q("id");uint32_t id=0;
        auto parsed=std::from_chars(value.data(),value.data()+value.size(),id);
        if(value.empty()||parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size()||!id)return reply(400,"text/plain","");
        for(const auto& file:queue_files)if(file.id==id) {queue_path=file.path;break;}
        if(queue_path.empty())return reply(404,"text/plain","");
      }
      if (r.path == "/list") progress_copy = progress;
    }
    // lists are quick; thumbnails queue up behind a gate (a grid asks for a dozen at once)
    if (*workers >= 6) return reply(503, "text/plain", "");
    // slow work (a sleeping disk, a shell thumbnail) goes to a worker that owns the socket
    SOCKET s = c.s;
    c.s = INVALID_SOCKET;
    c.closing = true;
    u_long blocking = 0;
    ioctlsocket(s, FIONBIO, &blocking);
    DWORD timeout = 8000;
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof timeout);
    std::string path = r.path=="/queue-thumb"||r.path=="/cover"?queue_path:q("p");
    bool list = r.path == "/list", cover = r.path == "/cover";
    // a song already in the queue shows its cover wherever it lives: the phone names only the
    // queue's id, and the queue (names included) is already shared with it. Films still need a
    // shared folder, as a frame can show more than a name.
    bool queue_song = r.path == "/queue-thumb" && remote::is_audio_name(path);
    auto workers_ref = workers;
    auto thumbs_ref = thumbs;
    auto gate_ref = gate;
    auto generation_ref = generation;
    uint64_t epoch = *generation_ref;
    ++*workers_ref;
    try { std::thread([s, path, list, cover, queue_song, roots_copy, progress_copy, workers_ref, thumbs_ref, gate_ref, generation_ref, epoch] {
      // Winsock is paired on the thread that uses it: the worker holds its own
      // reference, so the server stopping does not pull the socket out from
      // under it.
      WSADATA worker_wd{};
      const bool worker_wsa = WSAStartup(MAKEWORD(2, 2), &worker_wd) == 0;
      struct Cleanup {
        SOCKET socket;
        std::shared_ptr<std::atomic<int>> workers;
        bool wsa;
        ~Cleanup() { closesocket(socket); --*workers; if (wsa) WSACleanup(); }
      } cleanup{s, workers_ref, worker_wsa};
      if (!worker_wsa) return;
      try {
      if (*generation_ref != epoch) return;
      std::string resp;
      if (list) {
        std::string body;
        int st = list_json(path, roots_copy, progress_copy, body);
        resp = remote::http_response(st, st == 200 ? "application/json; charset=utf-8" : "text/plain", st == 200 ? body : "");
      } else if (cover) {
        std::string img, k = lower_path(path) + "#cover";
        bool gate_busy = false;
        if (!thumbs_ref->get(k, img)) {
          if (gate_ref->enter(3, generation_ref, epoch)) {
            struct Leave { Gate* gate; ~Leave() { gate->leave(); } } leave{gate_ref.get()};
            if (*generation_ref == epoch && !thumbs_ref->get(k, img)) {
              img = make_cover(path, kCoverPx);
              if (*generation_ref == epoch) thumbs_ref->put(k, img);
            }
          } else if (*generation_ref == epoch) gate_busy = true;
        }
        resp = gate_busy ? remote::http_response(503, "text/plain", "") : (img.empty() ? remote::http_response(404, "text/plain", "") : remote::http_response(200, "image/bmp", img));
      } else if (!queue_song && (!root_of(roots_copy, path) || !remote_path_allowed(root_of(roots_copy, path)->path, path) || !remote::is_playable_name(path))) {
        resp = remote::http_response(403, "text/plain", "");
      } else {
        std::string img, k = lower_path(path);
        bool gate_busy = false;
        if (!thumbs_ref->get(k, img)) {
          if (gate_ref->enter(3, generation_ref, epoch)) {
            struct Leave { Gate* gate; ~Leave() { gate->leave(); } } leave{gate_ref.get()};
            if (*generation_ref == epoch && !thumbs_ref->get(k, img)) {
              img = make_thumb(path, remote::is_audio_name(path));  // a song: its square cover, centred
              if (*generation_ref == epoch) thumbs_ref->put(k, img);
            }
          } else if (*generation_ref == epoch) gate_busy = true;
        }
        resp = gate_busy ? remote::http_response(503, "text/plain", "") : (img.empty() ? remote::http_response(404, "text/plain", "") : remote::http_response(200, "image/bmp", img));
      }
      if (*generation_ref == epoch) send_all(s, resp);
      shutdown(s, SD_SEND);
      } catch (...) { /* resource/shell failures close only this HTTP request */ }
    }).detach(); } catch (...) {
      closesocket(s);
      --*workers_ref;
    }
    return;
  }
  reply(404, "text/plain", "");
}

void RemoteServer::Impl::authenticate(Client& c, const std::string& payload) {
  JVal j;
  int refuse = 4001;
  std::string fresh;
  if (payload.size() <= 256 && json_parse(payload, j) && j.t == JVal::Obj && j["t"].str() == "auth") {
    std::lock_guard<std::mutex> l(mu);
    std::string attempt = j["c"].str(), kid = j["k"].str(), proof = j["r"].str(), instance = j["i"].str();
    if (instance.size() >= 8 && instance.size() <= 64 &&
        std::all_of(instance.begin(), instance.end(), [](unsigned char ch) {
          return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
        })) c.instance = std::move(instance);
    double now = clock_s();
    // The credential never crosses the wire: a connection proves it by
    // answering the challenge. The pairing code is the only way in.
    if (!remote::auth_event_room(events.size())) refuse = 4003;
    else if (!attempt.empty()) {
      if (!pairing || now >= pair_until || code.empty()) refuse = 4002;
      else if (!guard.allowed(now)) refuse = 4003;                       // guessing from many addresses
      else if (!guards[c.peer].allowed(now)) refuse = 4003;               // this address guessed too much
      else if (remote::same_secret(attempt, code)) {                     // no early exit: no timing to read
        fresh = remote::hex(random_bytes(32));
        if (remote::valid_token(fresh)) {
          guard.succeeded();
          guards[c.peer].succeeded();
          credentials[remote::credential_kid(fresh)] = fresh;
          c.credential = fresh;
          pairing = false; code.clear(); // one use, even before the UI processes the event
          refuse = 0;
        }
      } else {
        guard.failed(now);
        // The per-address budgets are kept small: a network with many
        // addresses must not make a list of every one of them.
        if (guards.size() >= 32) guards.clear();
        guards[c.peer].failed(now);
        refuse = (guard.allowed(now) && guards[c.peer].allowed(now)) ? 4001 : 4003;
      }
    } else if (!kid.empty() && !proof.empty()) {
      if (!credential_ok(kid, proof, c.nonce)) refuse = 4001;  // not one of ours, or not the answer
      else {
        auto it = credentials.find(kid);
        c.credential = it->second;
        refuse = 0;
      }
    }
  }
  if (refuse) {
    std::string close{(char)(refuse >> 8), (char)(refuse & 255)};
    queue(c, remote::ws_encode(remote::WS_CLOSE, close));
    c.closing = true;
    return;
  }
  // A reconnect from this page supersedes its old half-open TCP connection.
  // Other tabs and phones keep their own sessions; legacy clients omit the ID.
  if (!c.instance.empty()) for (auto& old : clients) {
    if (&old == &c || !old.authenticated || old.credential != c.credential || old.instance != c.instance) continue;
    old.closing = true; old.in.clear(); old.out.clear();
    if (!old.sid.empty()) {
      std::lock_guard<std::mutex> l(mu);
      sessions.erase(old.sid);
      old.sid.clear();
    }
    shutdown(old.s, SD_BOTH);
  }
  c.authenticated = true;
  ++nclients;
  // The session the HTTP requests this connection makes will sign with. The
  // credential itself goes only to the phone that paired: it is what it must
  // keep to prove itself next time.
  const std::string sid = remote::hex(random_bytes(8));
  const std::string sk = remote::hex(random_bytes(32));
  {
    std::lock_guard<std::mutex> l(mu);
    sessions[sid] = Session{sk, 0, {}};
  }
  c.sid = sid;
  std::string hello = "{\"t\":\"hello\",\"pc\":" + remote::escape(pc) + ",\"sid\":\"" + sid + "\",\"sk\":\"" + sk + "\"";
  if (!fresh.empty()) hello += ",\"key\":" + remote::escape(fresh);
  queue(c, remote::ws_encode(remote::WS_TEXT, hello + "}"));
  {
    std::lock_guard<std::mutex> l(mu);
    if (!state.empty()) queue(c, remote::ws_encode(remote::WS_TEXT, state));
    c.sent_rev = state_rev;
    c.peek_sent = peek_rev;
  }
  Event e;
  e.kind = fresh.empty() ? Event::Connected : Event::Paired;
  e.device = c.device;
  e.key = fresh;
  post(e);
}

void RemoteServer::Impl::handle_frames(Client& c) {
  for (;;) {
    remote::Frame f;
    size_t used = 0;
    int st = remote::ws_decode(c.in, f, used);
    if (st == 0) return;
    if (st < 0 || !f.fin) { c.closing = true; c.in.clear(); return; }
    c.in.erase(0, used);
    c.last_rx = clock_s();
    if (!c.messages.take(c.last_rx)) { c.closing = true; c.in.clear(); return; }
    if (!c.authenticated) {
      if (f.opcode != remote::WS_TEXT) { c.closing = true; return; }
      authenticate(c, f.payload);
      if (c.closing) return;
      continue;
    }
    {
      std::lock_guard<std::mutex> l(mu);
      if (!known(c.credential)) { c.closing = true; return; }
    }
    if (f.opcode == remote::WS_PING) queue(c, remote::ws_encode(remote::WS_PONG, f.payload));
    else if (f.opcode == remote::WS_CLOSE) { queue(c, remote::ws_encode(remote::WS_CLOSE, "")); c.closing = true; return; }
    else if (f.opcode == remote::WS_TEXT) {
      if (f.payload == "{\"t\":\"ping\"}") {
        queue(c, remote::ws_encode(remote::WS_TEXT, "{\"t\":\"pong\"}")); continue;
      }
      Event e;
      if (remote::parse_command(f.payload, e.cmd)) { e.cmd.credential = c.credential; e.kind = Event::Command; e.device = c.device; post(e); }
    }
  }
}

void RemoteServer::Impl::loop() {
  char buf[16384];
  while (!quit) {
    fd_set rd, wr;
    FD_ZERO(&rd);
    FD_ZERO(&wr);
    FD_SET(lsock, &rd);
    for (auto& c : clients) {
      if (c.s == INVALID_SOCKET) continue;
      if (!c.closing) FD_SET(c.s, &rd);
      if (!c.out.empty()) FD_SET(c.s, &wr);
    }
    timeval tv{0, clients.empty() ? 500000 : 100000};  // idle: a new phone wakes it anyway
    int n = select(0, &rd, &wr, nullptr, &tv);
    if (quit) break;
    if (n < 0) { Sleep(50); continue; }
    if (FD_ISSET(lsock, &rd)) {
      sockaddr_in peer{};
      int len = sizeof peer;
      SOCKET s = accept(lsock, reinterpret_cast<sockaddr*>(&peer), &len);
      if (s != INVALID_SOCKET) {
        bool ok = peer.sin_family == AF_INET && remote::private_ipv4(ntohl(peer.sin_addr.s_addr)) && clients.size() < 24;
        if (!ok) closesocket(s);
        else {
          u_long nb = 1;
          ioctlsocket(s, FIONBIO, &nb);
          BOOL nodelay = TRUE;
          setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&nodelay), sizeof nodelay);
          Client c;
          c.s = s;
          c.peer = ntohl(peer.sin_addr.s_addr);
          c.accepted = c.last_rx = clock_s();
          clients.push_back(std::move(c));
        }
      }
    }
    for (size_t i = 0; i < clients.size(); ++i) {
      Client& c = clients[i];
      if (c.s == INVALID_SOCKET) continue;
      if (FD_ISSET(c.s, &wr)) flush(c);
      if (!FD_ISSET(c.s, &rd)) continue;
      int got = recv(c.s, buf, sizeof buf, 0);
      if (got < 0 && WSAGetLastError() == WSAEWOULDBLOCK) continue;
      if (got <= 0) { c.closing = true; c.out.clear(); continue; }
      if (c.closing) continue;
      if (c.in.size() + (size_t)got > remote::kMaxRequest + sizeof buf) {
        c.closing = true; c.in.clear(); continue;
      }
      c.in.append(buf, (size_t)got);
      if (c.ws) { handle_frames(c); continue; }
      remote::Request req;
      size_t hl = 0;
      int st = remote::parse_request(c.in, req, hl);
      if (st < 0) { queue(c, remote::http_response(400, "text/plain", "")); c.closing = true; }
      else if (st > 0) { c.in.erase(0, hl); handle_http(i, req); if (c.ws && !c.in.empty()) handle_frames(c); }
    }
    // the player's state, to every phone that has not seen this version
    double now = clock_s();
    std::string st;
    uint64_t rev = 0, prev = 0;
    double pat = 0;
    {
      std::lock_guard<std::mutex> l(mu);
      rev = state_rev;
      // Do not copy or encode a snapshot at every poll when no receiver needs it.
      if (std::any_of(clients.begin(), clients.end(), [&](const Client& c) {
            return c.authenticated && !c.closing && c.out.empty() && c.sent_rev != rev;
          })) st = state;
      prev = peek_rev;
      pat = peek_at;
    }
    const std::string state_frame = st.empty() ? std::string() : remote::ws_encode(remote::WS_TEXT, st);
    for (auto& c : clients) {
      if (!c.authenticated || c.closing || c.s == INVALID_SOCKET) continue;
      {
        std::lock_guard<std::mutex> l(mu);
        if (!known(c.credential)) { c.closing = true; continue; }
      }
      if (c.sent_rev != rev && !state_frame.empty() && c.out.empty()) { queue(c, state_frame); c.sent_rev = rev; }
      if (c.peek_sent != prev && c.out.empty()) {
        char b[96];
        std::snprintf(b, sizeof b, "{\"t\":\"peek\",\"n\":%llu,\"at\":%.2f}", (unsigned long long)prev, pat);
        queue(c, remote::ws_encode(remote::WS_TEXT, b));
        c.peek_sent = prev;
      }
      if (now - c.last_ping > 20) { queue(c, remote::ws_encode(remote::WS_PING, "")); c.last_ping = now; }
      if (now - c.last_rx > 75) c.closing = true;  // a phone that went quiet (asleep, out of range)
    }
    for (size_t i = clients.size(); i-- > 0;) {
      Client& c = clients[i];
      bool idle_http = !c.authenticated && now - c.accepted > 5;
      if (c.s == INVALID_SOCKET || ((c.closing || idle_http) && c.out.empty()) || (c.closing && now - c.last_rx > 5)) drop(i);
    }
  }
  // leaving: phones hear why (when the person turned the remote off), then every socket closes
  for (auto& c : clients) {
    if (c.s == INVALID_SOCKET) continue;
    if (c.ws && tell) {
      // Best effort on the existing nonblocking socket: a stalled phone must
      // never hold up the UI's stop/join, let alone 24 times in succession.
      send_all(c.s, c.out + remote::ws_encode(remote::WS_TEXT, "{\"t\":\"bye\"}") + remote::ws_encode(remote::WS_CLOSE, ""));
    }
  }
  while (!clients.empty()) drop(clients.size() - 1);
}

std::vector<uint32_t> file_thumbnail_pixels(const std::string& path, bool square) {
  auto bmp=make_thumb(path, square); if(bmp.size()!=54+84*48*3) return {};
  std::vector<uint32_t> px(84*48);
  for(int y=0;y<48;++y) for(int x=0;x<84;++x) {
    auto at=54+(47-y)*84*3+x*3;
    px[y*84+x]=0xff000000u | (uint32_t)(unsigned char)bmp[at+2]<<16 |
      (uint32_t)(unsigned char)bmp[at+1]<<8 | (unsigned char)bmp[at];
  }
  return px;
}

RemoteServer::RemoteServer() : d_(new Impl) {}
RemoteServer::~RemoteServer() { stop(false); }

bool RemoteServer::start(HWND notify, UINT msg, const std::wstring& font_path, std::string* err) {
  if (d_->running) return true;
  d_->hwnd = notify;
  d_->msg = msg;
  {
    wchar_t name[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD n = MAX_COMPUTERNAME_LENGTH + 1;
    if (GetComputerNameW(name, &n)) d_->pc = narrow(std::wstring(name, n));
  }
  WSADATA wd;
  if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) { if (err) *err = "winsock unavailable"; return false; }
  d_->wsa = true;
  if (d_->font.empty()) {
    HANDLE f = CreateFileW(font_path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
      LARGE_INTEGER size{};
      if (GetFileSizeEx(f, &size) && size.QuadPart > 0 && size.QuadPart < (4 << 20)) {
        std::string data((size_t)size.QuadPart, '\0');
        DWORD got = 0;
        if (ReadFile(f, data.data(), (DWORD)data.size(), &got, nullptr) && got == data.size()) d_->font = std::move(data);
      }
      CloseHandle(f);
    }
  }
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) { if (err) *err = "socket failed"; WSACleanup(); d_->wsa = false; return false; }
  BOOL excl = TRUE;
  setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&excl), sizeof excl);
  int port = 0;
  // Bind to the network address itself, not to every adapter: a machine with a
  // public interface must not listen for the world on it. The peer filter
  // (private addresses only) stays as the second layer. Without a network
  // address there is nothing better to bind to, and the peer filter still
  // refuses anyone who is not on a private network.
  uint32_t bind_addr = htonl(INADDR_ANY);
  const std::string lan = lan_address();
  if (!lan.empty()) {
    in_addr parsed{};
    if (InetPtonA(AF_INET, lan.c_str(), &parsed) == 1) bind_addr = parsed.S_un.S_addr;
  }
  for (int p = remote::kPort; p < remote::kPort + remote::kPortTries; ++p) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = bind_addr;
    a.sin_port = htons((u_short)p);
    if (bind(s, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0) { port = p; break; }
  }
  if (!port || listen(s, 16) != 0) {
    if (err) *err = "no free port";
    closesocket(s);
    WSACleanup();
    d_->wsa = false;
    return false;
  }
  u_long nb = 1;
  ioctlsocket(s, FIONBIO, &nb);
  d_->lsock = s;
  d_->port = port;
  d_->quit = false;
  d_->tell = false;
  d_->running = true;
  try { d_->th = std::thread([this] { d_->loop(); }); }
  catch (...) {
    closesocket(s); d_->lsock = INVALID_SOCKET; d_->running = false;
    WSACleanup(); d_->wsa = false;
    if (err) *err = "server thread unavailable";
    return false;
  }
  return true;
}

void RemoteServer::stop(bool tell_phones) {
  if (!d_->running) return;
  d_->tell = tell_phones;
  ++*d_->generation;
  d_->gate->wake_all();
  d_->quit = true;
  if (d_->th.joinable()) d_->th.join();
  closesocket(d_->lsock);
  d_->lsock = INVALID_SOCKET;
  d_->running = false;
  d_->nclients = 0;
  // Workers own their sockets, Winsock reference and data. Generation invalidation
  // suppresses late replies; waiting on Explorer here only stalls the UI.
  if (d_->wsa) { WSACleanup(); d_->wsa = false; }
  std::lock_guard<std::mutex> l(d_->mu);
  d_->pairing = false;
  d_->code.clear();
  d_->peek.clear();
  d_->thumbs->clear();
  d_->events.clear();
  d_->posted = false;
}

bool RemoteServer::running() const { return d_->running; }
int RemoteServer::port() const { return d_->port; }
int RemoteServer::clients() const { return d_->nclients; }

void RemoteServer::set_queue(const std::string& json,std::vector<QueueFile> files) {
  std::lock_guard<std::mutex> l(d_->mu);
  if(json.size()<=256*1024&&files.size()<=512) {d_->queue_json=json;d_->queue_files=std::move(files);}
}
void RemoteServer::set_state(const std::string& json) {
  std::lock_guard<std::mutex> l(d_->mu);
  if (json == d_->state) return;
  d_->state = json;
  ++d_->state_rev;
}

void RemoteServer::set_cover(const std::string& path) {
  std::lock_guard<std::mutex> l(d_->mu);
  d_->cover_path = path;
}

void RemoteServer::set_peek(double at, const std::vector<uint32_t>& px, int w, int h) {
  if (w <= 0 || h <= 0 || px.size() < (size_t)w * h) return;
  // small on purpose: a glance at where the wheel will land, 160 px wide at most
  int ow = std::min(w, 160), oh = std::max(1, (int)std::lround((double)h * ow / w));
  std::vector<uint32_t> out((size_t)ow * oh);
  for (int y = 0; y < oh; ++y)
    for (int x = 0; x < ow; ++x) out[(size_t)y * ow + x] = px[(size_t)(y * h / oh) * w + x * w / ow] & 0xFFFFFF;
  std::string img = bmp24(out, ow, oh);
  std::lock_guard<std::mutex> l(d_->mu);
  d_->peek = std::move(img);
  d_->peek_at = at;
  ++d_->peek_rev;
}

void RemoteServer::set_boot(const std::string& script) {
  std::lock_guard<std::mutex> l(d_->mu);
  if (script == d_->boot) return;
  d_->boot = script;
  d_->page_dirty = true;
}

void RemoteServer::set_roots(std::vector<Root> roots) {
  std::lock_guard<std::mutex> l(d_->mu);
  d_->roots = std::move(roots);
  ++*d_->generation;
  d_->gate->wake_all();
}

void RemoteServer::set_progress(std::vector<std::pair<std::string, Progress>> items) {
  ProgressMap m;
  for (auto& [k, v] : items) m[lower_path(k)] = v;
  std::lock_guard<std::mutex> l(d_->mu);
  d_->progress = std::move(m);
}

void RemoteServer::set_credentials(std::vector<std::string> credentials) {
  std::lock_guard<std::mutex> l(d_->mu);
  d_->credentials.clear();
  // The credential itself, not a hash of it: the phone presents it, the
  // connection proves it against the challenge. The state file holds it
  // encrypted, so a file that is read is not a set of keys.
  for (const auto& c : credentials)
    if (remote::valid_token(c) && d_->credentials.size() < remote::kMaxTokens)
      d_->credentials[remote::credential_kid(c)] = c;
  ++*d_->generation;
  d_->gate->wake_all();
}

void RemoteServer::set_pairing(bool open, const std::string& code) {
  std::lock_guard<std::mutex> l(d_->mu);
  d_->pairing = open && code.size() == remote::kCodeDigits;
  d_->code = d_->pairing ? code : "";
  d_->pair_until = d_->pairing ? clock_s() + remote::kPairLifetime : 0;
}

std::vector<RemoteServer::Event> RemoteServer::take_events() {
  std::lock_guard<std::mutex> l(d_->mu);
  d_->posted = false;
  std::vector<Event> events; events.swap(d_->events); return events;
}

std::string lan_address() {
  ULONG size = 16 * 1024;
  std::vector<unsigned char> buf(size);
  ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
  ULONG r = GetAdaptersAddresses(AF_INET, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
  if (r == ERROR_BUFFER_OVERFLOW) {
    buf.resize(size);
    r = GetAdaptersAddresses(AF_INET, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
  }
  if (r != NO_ERROR) return "";
  std::string best;
  int best_score = -1;
  for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()); a; a = a->Next) {
    if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
    for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
      auto* sa = reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr);
      if (!sa || sa->sin_family != AF_INET) continue;
      uint32_t ip = ntohl(sa->sin_addr.s_addr);
      if (!remote::private_ipv4(ip) || (ip >> 24) == 127 || (ip >> 16) == 0xA9FE) continue;
      // a real home network: has a router; wired and wireless before virtual adapters
      int score = (a->FirstGatewayAddress ? 4 : 0) + ((a->IfType == IF_TYPE_ETHERNET_CSMACD || a->IfType == IF_TYPE_IEEE80211) ? 2 : 0) +
                  (((ip >> 16) == 0xC0A8) ? 1 : 0);
      if (score > best_score) { best_score = score; best = remote::ipv4_text(ip); }
    }
  }
  return best;
}

std::string new_pairing_code() {
  std::string b = random_bytes(4);
  if (b.size() != 4) return {};
  uint32_t v = 0;
  for (unsigned char c : b) v = v << 8 | c;
  std::string code = std::to_string(v % 1000000);
  while (code.size() < (size_t)remote::kCodeDigits) code = "0" + code;
  return code;
}
