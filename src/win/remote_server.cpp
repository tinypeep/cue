#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include "remote_server.h"

#include <bcrypt.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <atomic>
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

std::string make_thumb(const std::string& path) {
  std::string out;
  HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  IShellItemImageFactory* f = nullptr;
  if (SUCCEEDED(SHCreateItemFromParsingName(widen(path).c_str(), nullptr, IID_PPV_ARGS(&f)))) {
    HBITMAP hb = nullptr;
    if (SUCCEEDED(f->GetImage(SIZE{kThumbW * 4, kThumbH * 4}, SIIGBF_THUMBNAILONLY | SIIGBF_BIGGERSIZEOK, &hb)) && hb) {
      BITMAP bm{};
      GetObjectW(hb, sizeof bm, &bm);
      int w = bm.bmWidth, h = std::abs(bm.bmHeight);
      if (w > 0 && h > 0 && w <= 2048 && h <= 2048) {
        std::vector<uint32_t> src((size_t)w * h);
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;  // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        HDC dc = GetDC(nullptr);
        bool ok = GetDIBits(dc, hb, 0, (UINT)h, src.data(), &bi, DIB_RGB_COLORS) == h;
        ReleaseDC(nullptr, dc);
        if (ok) {
          // cover the 16:9 tile: crop the middle, then average each target pixel's box
          double ar = (double)kThumbW / kThumbH;
          double cw = w, ch = h, cx = 0, cy = 0;
          if (w / (double)h > ar) { cw = h * ar; cx = (w - cw) / 2; } else { ch = w / ar; cy = (h - ch) / 2; }
          std::vector<uint32_t> dst((size_t)kThumbW * kThumbH);
          for (int y = 0; y < kThumbH; ++y)
            for (int x = 0; x < kThumbW; ++x) {
              int x0 = (int)(cx + x * cw / kThumbW), x1 = std::max(x0 + 1, (int)(cx + (x + 1) * cw / kThumbW));
              int y0 = (int)(cy + y * ch / kThumbH), y1 = std::max(y0 + 1, (int)(cy + (y + 1) * ch / kThumbH));
              uint64_t r = 0, g = 0, b = 0, n = 0;
              for (int yy = y0; yy < std::min(y1, h); ++yy)
                for (int xx = x0; xx < std::min(x1, w); ++xx) {
                  uint32_t v = src[(size_t)yy * w + xx];
                  b += v & 0xFF; g += (v >> 8) & 0xFF; r += (v >> 16) & 0xFF; ++n;
                }
              if (!n) n = 1;
              dst[(size_t)y * kThumbW + x] = (uint32_t)(r / n) << 16 | (uint32_t)(g / n) << 8 | (uint32_t)(b / n);
            }
          // a blank answer (an all-black first frame, a shell with nothing to show) is no
          // picture: the phone draws its own pixel landscape instead
          int lo = 255, hi = 0;
          for (uint32_t v : dst) {
            int l = (int)(((v >> 16) & 0xFF) * 3 + ((v >> 8) & 0xFF) * 6 + (v & 0xFF)) / 10;
            lo = std::min(lo, l);
            hi = std::max(hi, l);
          }
          if (hi - lo > 3) out = bmp24(dst, kThumbW, kThumbH);
        }
      }
      DeleteObject(hb);
    }
    f->Release();
  }
  if (SUCCEEDED(co)) CoUninitialize();
  return out;
}

struct Gate {
  std::mutex mu;
  std::condition_variable cv;
  int busy = 0;
  void enter(int limit) { std::unique_lock<std::mutex> l(mu); cv.wait(l, [&] { return busy < limit; }); ++busy; }
  void leave() { { std::lock_guard<std::mutex> l(mu); --busy; } cv.notify_one(); }
};

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
  std::string token;
  remote::RateLimit messages;
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
  std::string state, boot, page;
  uint64_t state_rev = 0;
  bool page_dirty = true;
  Roots roots;
  ProgressMap progress;
  std::set<std::string> tokens;
  bool pairing = false;
  std::string code;
  double pair_until = 0;
  std::shared_ptr<std::atomic<uint64_t>> generation = std::make_shared<std::atomic<uint64_t>>(0);
  remote::CodeGuard guard;
  std::vector<Event> events;
  bool posted = false;
  std::shared_ptr<ThumbCache> thumbs = std::make_shared<ThumbCache>();
  std::shared_ptr<Gate> gate = std::make_shared<Gate>();
  std::string pc;           // the computer's name, for the phone's "connected" moment
  std::string peek;         // the latest timeline preview asked by a phone (BMP)
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
  void handle_frames(Client& c);
  void loop();
  void drop(size_t idx);
};

void RemoteServer::Impl::drop(size_t idx) {
  Client& c = clients[idx];
  if (c.s != INVALID_SOCKET) closesocket(c.s);
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
  if (r.method != "GET") return reply(405, "text/plain", "");
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
    return;  // first masked text frame authenticates; no protected data before it
  }
  auto bearer = [&] {
    std::string auth = hdr("authorization");
    return auth.rfind("Bearer ", 0) == 0 ? auth.substr(7) : std::string();
  };
  if (r.path == "/peek") {
    std::string img;
    {
      std::lock_guard<std::mutex> l(mu);
      if (!tokens.count(bearer())) return reply(403, "text/plain", "");
      img = peek;
    }
    if (img.empty()) return reply(404, "text/plain", "");
    return reply(200, "image/bmp", img);
  }
  if (r.path == "/list" || r.path == "/thumb") {
    Roots roots_copy;
    ProgressMap progress_copy;
    {
      std::lock_guard<std::mutex> l(mu);
      if (!tokens.count(bearer())) return reply(403, "text/plain", "");
      roots_copy = roots;
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
    std::string path = q("p");
    bool list = r.path == "/list";
    auto workers_ref = workers;
    auto thumbs_ref = thumbs;
    auto gate_ref = gate;
    auto generation_ref = generation;
    uint64_t epoch = *generation_ref;
    // Acquire before launching: stop() may release the server's reference before
    // a newly scheduled worker gets its first CPU slice.
    WSADATA wd{};
    if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) { closesocket(s); return; }
    ++*workers_ref;
    try { std::thread([s, path, list, roots_copy, progress_copy, workers_ref, thumbs_ref, gate_ref, generation_ref, epoch] {
      struct Cleanup {
        SOCKET socket;
        std::shared_ptr<std::atomic<int>> workers;
        ~Cleanup() { closesocket(socket); --*workers; WSACleanup(); }
      } cleanup{s, workers_ref};
      try {
      if (*generation_ref != epoch) return;
      std::string resp;
      if (list) {
        std::string body;
        int st = list_json(path, roots_copy, progress_copy, body);
        resp = remote::http_response(st, st == 200 ? "application/json; charset=utf-8" : "text/plain", st == 200 ? body : "");
      } else if (!root_of(roots_copy, path) || !remote_path_allowed(root_of(roots_copy, path)->path, path) || !remote::is_video_name(path)) {
        resp = remote::http_response(403, "text/plain", "");
      } else {
        std::string img, k = lower_path(path);
        if (!thumbs_ref->get(k, img)) {
          gate_ref->enter(3);
          struct Leave { Gate* gate; ~Leave() { gate->leave(); } } leave{gate_ref.get()};
          if (*generation_ref == epoch && !thumbs_ref->get(k, img)) {
            img = make_thumb(path);
            if (*generation_ref == epoch) thumbs_ref->put(k, img);
          }
        }
        resp = img.empty() ? remote::http_response(404, "text/plain", "") : remote::http_response(200, "image/bmp", img);
      }
      if (*generation_ref == epoch) send_all(s, resp);
      shutdown(s, SD_SEND);
      } catch (...) { /* resource/shell failures close only this HTTP request */ }
    }).detach(); } catch (...) {
      closesocket(s);
      --*workers_ref;
      WSACleanup();
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
    std::string token = j["k"].str(), attempt = j["c"].str();
    double now = clock_s();
    if (!remote::auth_event_room(events.size())) refuse = 4003;
    else if (remote::valid_token(token) && tokens.count(token)) { c.token = token; refuse = 0; }
    else if (!attempt.empty()) {
      if (!pairing || now >= pair_until || code.empty()) refuse = 4002;
      else if (!guard.allowed(now)) refuse = 4003;
      else if (attempt == code) {
        fresh = remote::hex(random_bytes(32));
        if (remote::valid_token(fresh)) {
          guard.succeeded();
          tokens.insert(fresh);
          c.token = fresh;
          pairing = false; code.clear(); // one use, even before the UI processes the event
          refuse = 0;
        }
      } else { guard.failed(now); refuse = guard.allowed(now) ? 4001 : 4003; }
    }
  }
  if (refuse) {
    std::string close{(char)(refuse >> 8), (char)(refuse & 255)};
    queue(c, remote::ws_encode(remote::WS_CLOSE, close));
    c.closing = true;
    return;
  }
  c.authenticated = true;
  ++nclients;
  std::string hello = "{\"t\":\"hello\",\"pc\":" + remote::escape(pc);
  if (!fresh.empty()) hello += ",\"token\":" + remote::escape(fresh);
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
  e.token = fresh;
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
      if (!tokens.count(c.token)) { c.closing = true; return; }
    }
    if (f.opcode == remote::WS_PING) queue(c, remote::ws_encode(remote::WS_PONG, f.payload));
    else if (f.opcode == remote::WS_CLOSE) { queue(c, remote::ws_encode(remote::WS_CLOSE, "")); c.closing = true; return; }
    else if (f.opcode == remote::WS_TEXT) {
      Event e;
      if (remote::parse_command(f.payload, e.cmd)) { e.kind = Event::Command; e.device = c.device; post(e); }
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
      st = state;
      rev = state_rev;
      prev = peek_rev;
      pat = peek_at;
    }
    for (auto& c : clients) {
      if (!c.authenticated || c.closing || c.s == INVALID_SOCKET) continue;
      {
        std::lock_guard<std::mutex> l(mu);
        if (!tokens.count(c.token)) { c.closing = true; continue; }
      }
      if (c.sent_rev != rev && !st.empty() && c.out.size() < 256 * 1024) { queue(c, remote::ws_encode(remote::WS_TEXT, st)); c.sent_rev = rev; }
      if (c.peek_sent != prev && c.out.size() < 256 * 1024) {
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

std::vector<uint32_t> file_thumbnail_pixels(const std::string& path) {
  auto bmp=make_thumb(path); if(bmp.size()!=54+84*48*3) return {};
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
  for (int p = remote::kPort; p < remote::kPort + remote::kPortTries; ++p) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
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

void RemoteServer::set_state(const std::string& json) {
  std::lock_guard<std::mutex> l(d_->mu);
  if (json == d_->state) return;
  d_->state = json;
  ++d_->state_rev;
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
}

void RemoteServer::set_progress(std::vector<std::pair<std::string, Progress>> items) {
  ProgressMap m;
  for (auto& [k, v] : items) m[lower_path(k)] = v;
  std::lock_guard<std::mutex> l(d_->mu);
  d_->progress = std::move(m);
}

void RemoteServer::set_tokens(std::vector<std::string> tokens) {
  std::lock_guard<std::mutex> l(d_->mu);
  d_->tokens.clear();
  for (const auto& t : tokens) if (remote::valid_token(t) && d_->tokens.size() < remote::kMaxTokens) d_->tokens.insert(t);
  ++*d_->generation;
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
  return std::move(d_->events);
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
