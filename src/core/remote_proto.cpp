#include "remote_proto.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "json.h"

namespace remote {

namespace {
std::string lower(std::string_view s) {
  std::string r(s);
  for (auto& c : r) c = (char)std::tolower((unsigned char)c);
  return r;
}
std::string trim(std::string_view s) {
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
  return std::string(s.substr(a, b - a));
}
int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
std::string ext_of(std::string_view name) {
  size_t d = name.rfind('.');
  if (d == std::string_view::npos || d + 1 >= name.size()) return "";
  return lower(name.substr(d + 1));
}
std::string num(double v, int places) {
  if (!std::isfinite(v)) v = 0;
  char b[48];
  std::snprintf(b, sizeof b, "%.*f", places, v);
  return b;
}
}  // namespace

// ── HTTP ────────────────────────────────────────────────────────────
std::string url_decode(std::string_view s) {
  std::string r;
  r.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size() && hexval(s[i + 1]) >= 0 && hexval(s[i + 2]) >= 0) {
      r += (char)(hexval(s[i + 1]) * 16 + hexval(s[i + 2]));
      i += 2;
    } else if (s[i] == '+') {
      r += ' ';
    } else {
      r += s[i];
    }
  }
  return r;
}

std::string url_encode(std::string_view s) {
  static const char* h = "0123456789ABCDEF";
  std::string r;
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') r += (char)c;
    else { r += '%'; r += h[c >> 4]; r += h[c & 15]; }
  }
  return r;
}

bool valid_authority(std::string_view host) {
  if (host.empty() || host.size() > 32) return false;
  size_t colon = host.find(':');
  auto ip = host.substr(0, colon);
  if (colon != std::string_view::npos) {
    auto port = host.substr(colon + 1);
    if (port.empty() || port.size() > 5) return false;
    unsigned n = 0;
    for (char c : port) { if (c < '0' || c > '9') return false; n = n * 10 + c - '0'; }
    if (!n || n > 65535) return false;
  }
  if (ip == "localhost") return true;
  size_t at = 0;
  for (int part = 0; part < 4; ++part) {
    size_t end = ip.find('.', at);
    if ((part < 3) != (end != std::string_view::npos)) return false;
    if (end == std::string_view::npos) end = ip.size();
    auto v = ip.substr(at, end - at);
    if (v.empty() || v.size() > 3 || (v.size() > 1 && v[0] == '0')) return false;
    int n = 0;
    for (char c : v) { if (c < '0' || c > '9') return false; n = n * 10 + c - '0'; }
    if (n > 255) return false;
    at = end + 1;
  }
  return true;
}

bool valid_ws_key(std::string_view k) {
  if (k.size() != 24 || k.substr(22) != "==") return false;
  for (size_t i = 0; i < 22; ++i)
    if (!std::isalnum((unsigned char)k[i]) && k[i] != '+' && k[i] != '/') return false;
  return k[21] == 'A' || k[21] == 'Q' || k[21] == 'g' || k[21] == 'w';
}

bool valid_token(std::string_view t) {
  if (t.size() != 64) return false;
  for (char c : t) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  return true;
}

std::string hmac_sha256(std::string_view key, std::string_view message) {
  // HMAC with a block size of 64: a key longer than the block is hashed first.
  std::string block(64, '\0');
  if (key.size() > 64) {
    std::string h = sha256(key);
    std::copy(h.begin(), h.end(), block.begin());
  } else {
    std::copy(key.begin(), key.end(), block.begin());
  }
  std::string inner = block, outer = block;
  for (auto& c : inner) c = (char)(c ^ 0x36);
  for (auto& c : outer) c = (char)(c ^ 0x5c);
  inner += message;
  std::string h = sha256(inner);
  outer += h;
  return sha256(outer);
}

std::string token_hash(std::string_view token) { return hex(sha256(token)); }

std::string credential_kid(std::string_view credential) { return hex(sha256(credential)).substr(0, 16); }

bool valid_token_hash(std::string_view h) { return valid_token(h); }

bool same_secret(std::string_view a, std::string_view b) {
  // The same length on both sides, and every byte looked at: the time the
  // comparison takes must not say how much of the secret was right.
  if (a.size() != b.size()) return false;
  unsigned diff = 0;
  for (size_t i = 0; i < a.size(); ++i) diff |= (unsigned char)a[i] ^ (unsigned char)b[i];
  return diff == 0;
}

bool RateLimit::take(double now, double rate, double burst) {
  if (last < 0) credit = burst;
  else credit = std::min(burst, credit + std::max(0.0, now - last) * rate);
  last = now;
  if (credit < 1) return false;
  credit -= 1;
  return true;
}

int parse_request(std::string_view buf, Request& out, size_t& header_len) {
  size_t end = buf.find("\r\n\r\n");
  if (end == std::string_view::npos) return buf.size() > kMaxRequest ? -1 : 0;
  if (end + 4 > kMaxRequest) return -1;
  header_len = end + 4;
  out = Request{};
  size_t line_end = buf.find("\r\n");
  std::string_view first = buf.substr(0, line_end);
  size_t a = first.find(' '), b = first.rfind(' ');
  if (a == std::string_view::npos || b == a || first.substr(b + 1) != "HTTP/1.1") return -1;
  out.method = std::string(first.substr(0, a));
  std::string_view target = first.substr(a + 1, b - a - 1);
  if (target.empty() || target[0] != '/' || target.find('#') != std::string_view::npos) return -1;
  size_t q = target.find('?');
  out.path = url_decode(target.substr(0, q));
  if (out.path.find_first_of(std::string("\0\r\n", 3)) != std::string::npos) return -1;
  if (q != std::string_view::npos) {
    std::string_view qs = target.substr(q + 1);
    while (!qs.empty()) {
      size_t amp = qs.find('&');
      std::string_view kv = qs.substr(0, amp);
      size_t eq = kv.find('=');
      if (!kv.empty()) {
        auto key = url_decode(kv.substr(0, eq));
        if (out.query.count(key)) return -1;
        out.query[key] = eq == std::string_view::npos ? "" : url_decode(kv.substr(eq + 1));
      }
      if (amp == std::string_view::npos) break;
      qs = qs.substr(amp + 1);
    }
  }
  size_t pos = line_end + 2;
  while (pos < end) {
    size_t le = buf.find("\r\n", pos);
    if (le == std::string_view::npos || le > end) return -1;
    std::string_view line = buf.substr(pos, le - pos);
    size_t colon = line.find(':');
    if (colon == std::string_view::npos || !colon) return -1;
    auto name = lower(line.substr(0, colon));
    for (unsigned char c : name) if (!(std::isalnum(c) || c == '-')) return -1;
    auto value = trim(line.substr(colon + 1));
    for (unsigned char c : value) if ((c < 32 && c != 9) || c == 127) return -1;
    if (out.headers.count(name)) return -1;
    out.headers[name] = value;
    pos = le + 2;
  }
  if (!out.headers.count("host") || out.headers.count("transfer-encoding")) return -1;
  auto cl = out.headers.find("content-length");
  if (cl != out.headers.end() && cl->second != "0") return -1;
  return 1;
}

std::string http_response(int status, std::string_view type, std::string_view body, bool cache, std::string_view nonce, std::string_view authority) {
  const char* reason = status == 200 ? "OK" : status == 400 ? "Bad Request" : status == 403 ? "Forbidden" :
                       status == 404 ? "Not Found" : status == 405 ? "Method Not Allowed" : "Error";
  std::string r = "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\n";
  if (!type.empty()) r += "Content-Type: " + std::string(type) + "\r\n";
  r += "Content-Length: " + std::to_string(body.size()) + "\r\n";
  r += cache ? "Cache-Control: max-age=86400\r\n" : "Cache-Control: no-store\r\n";
  r += "Content-Security-Policy: default-src 'none'; script-src ";
  bool safe_nonce = nonce.size() == 32 && std::all_of(nonce.begin(), nonce.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
  r += safe_nonce ? "'nonce-" + std::string(nonce) + "'" : "'none'";
  r += "; style-src 'self' 'unsafe-inline'; img-src 'self' blob:; font-src 'self'; connect-src 'self'";
  if (valid_authority(authority)) r += " ws://" + std::string(authority) + " wss://" + std::string(authority);
  r += "; frame-ancestors 'none'; base-uri 'none'; form-action 'none'\r\n";
  r += "X-Frame-Options: DENY\r\nCross-Origin-Resource-Policy: same-origin\r\nPermissions-Policy: camera=(), microphone=(), geolocation=()\r\n";
  r += "X-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nConnection: close\r\n\r\n";
  r.append(body.data(), body.size());
  return r;
}

// ── WebSocket ───────────────────────────────────────────────────────
std::string sha1(std::string_view data) {
  uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  std::string m(data);
  uint64_t bits = (uint64_t)data.size() * 8;
  m += (char)0x80;
  while (m.size() % 64 != 56) m += (char)0;
  for (int i = 7; i >= 0; --i) m += (char)((bits >> (i * 8)) & 0xFF);
  auto rol = [](uint32_t v, int n) { return (v << n) | (v >> (32 - n)); };
  for (size_t off = 0; off < m.size(); off += 64) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i)
      w[i] = (uint32_t)(unsigned char)m[off + i * 4] << 24 | (uint32_t)(unsigned char)m[off + i * 4 + 1] << 16 |
             (uint32_t)(unsigned char)m[off + i * 4 + 2] << 8 | (uint32_t)(unsigned char)m[off + i * 4 + 3];
    for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; ++i) {
      uint32_t f, k;
      if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
      else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
      else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
      else { f = b ^ c ^ d; k = 0xCA62C1D6; }
      uint32_t t = rol(a, 5) + f + e + k + w[i];
      e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
  }
  std::string out;
  for (uint32_t v : h) for (int i = 3; i >= 0; --i) out += (char)((v >> (i * 8)) & 0xFF);
  return out;
}

std::string sha256(std::string_view data) {
  static const uint32_t k[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
      0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
      0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
      0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
      0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  std::string m(data);
  uint64_t bits = (uint64_t)data.size() * 8;
  m += (char)0x80;
  while (m.size() % 64 != 56) m += (char)0;
  for (int i = 7; i >= 0; --i) m += (char)((bits >> (i * 8)) & 0xFF);
  auto rotr = [](uint32_t v, int n) { return (v >> n) | (v << (32 - n)); };
  for (size_t off = 0; off < m.size(); off += 64) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
      w[i] = (uint32_t)(unsigned char)m[off + i * 4] << 24 | (uint32_t)(unsigned char)m[off + i * 4 + 1] << 16 |
             (uint32_t)(unsigned char)m[off + i * 4 + 2] << 8 | (uint32_t)(unsigned char)m[off + i * 4 + 3];
    for (int i = 16; i < 64; ++i) {
      uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
      uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      uint32_t ch = (e & f) ^ (~e & g);
      uint32_t t1 = hh + S1 + ch + k[i] + w[i];
      uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      uint32_t t2 = S0 + maj;
      hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
  }
  std::string out;
  for (uint32_t v : h) for (int i = 3; i >= 0; --i) out += (char)((v >> (i * 8)) & 0xFF);
  return out;
}

std::string base64(std::string_view data) {
  static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string r;
  size_t i = 0;
  for (; i + 2 < data.size(); i += 3) {
    uint32_t v = (uint32_t)(unsigned char)data[i] << 16 | (uint32_t)(unsigned char)data[i + 1] << 8 | (unsigned char)data[i + 2];
    r += t[v >> 18]; r += t[(v >> 12) & 63]; r += t[(v >> 6) & 63]; r += t[v & 63];
  }
  if (i + 1 == data.size()) {
    uint32_t v = (uint32_t)(unsigned char)data[i] << 16;
    r += t[v >> 18]; r += t[(v >> 12) & 63]; r += "==";
  } else if (i + 2 == data.size()) {
    uint32_t v = (uint32_t)(unsigned char)data[i] << 16 | (uint32_t)(unsigned char)data[i + 1] << 8;
    r += t[v >> 18]; r += t[(v >> 12) & 63]; r += t[(v >> 6) & 63]; r += '=';
  }
  return r;
}

std::string ws_accept(std::string_view key) {
  return base64(sha1(std::string(key) + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));
}

std::string ws_handshake(std::string_view key) {
  return "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " +
         ws_accept(key) + "\r\n\r\n";
}

int ws_decode(std::string_view buf, Frame& out, size_t& consumed) {
  if (buf.size() < 2) return 0;
  unsigned char b0 = (unsigned char)buf[0], b1 = (unsigned char)buf[1];
  if (b0 & 0x70) return -1;     // no extensions were agreed
  if (!(b1 & 0x80)) return -1;  // clients must mask
  uint64_t len = b1 & 0x7F;
  size_t pos = 2;
  if (len == 126) {
    if (buf.size() < 4) return 0;
    len = (uint64_t)(unsigned char)buf[2] << 8 | (unsigned char)buf[3];
    pos = 4;
  } else if (len == 127) {
    if (buf.size() < 10) return 0;
    len = 0;
    for (int i = 0; i < 8; ++i) len = len << 8 | (unsigned char)buf[2 + i];
    pos = 10;
  }
  int op = b0 & 0x0F;
  if (op != WS_TEXT && op != WS_CLOSE && op != WS_PING && op != WS_PONG) return -1;
  if (!(b0 & 0x80) || ((op & 8) && (len > 125 || (b1 & 0x7F) >= 126))) return -1;
  if ((op == WS_CLOSE && len == 1) || len > kMaxFrame) return -1;
  if (buf.size() < pos + 4 + len) return 0;
  const unsigned char* key = (const unsigned char*)buf.data() + pos;
  pos += 4;
  out.opcode = b0 & 0x0F;
  out.fin = (b0 & 0x80) != 0;
  out.payload.assign(buf.data() + pos, (size_t)len);
  for (size_t i = 0; i < out.payload.size(); ++i) out.payload[i] ^= (char)key[i & 3];
  consumed = pos + (size_t)len;
  return 1;
}

std::string ws_encode(int opcode, std::string_view payload) {
  std::string r;
  r += (char)(0x80 | (opcode & 0x0F));
  size_t n = payload.size();
  if (n < 126) r += (char)n;
  else if (n < 65536) { r += (char)126; r += (char)(n >> 8); r += (char)(n & 0xFF); }
  else { r += (char)127; for (int i = 7; i >= 0; --i) r += (char)((uint64_t)n >> (i * 8) & 0xFF); }
  r.append(payload.data(), payload.size());
  return r;
}

// ── who may connect ─────────────────────────────────────────────────
bool private_ipv4(uint32_t ip) {
  uint32_t a = ip >> 24, b = (ip >> 16) & 0xFF;
  return a == 10 || a == 127 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) || (a == 169 && b == 254);
}

std::string ipv4_text(uint32_t ip) {
  return std::to_string(ip >> 24) + "." + std::to_string((ip >> 16) & 0xFF) + "." + std::to_string((ip >> 8) & 0xFF) +
         "." + std::to_string(ip & 0xFF);
}

std::string device_name(std::string_view ua) {
  if (ua.find("iPhone") != std::string_view::npos) return "iPhone";
  if (ua.find("iPad") != std::string_view::npos) return "iPad";
  // iPadOS asks for desktop pages and says Macintosh; a touch Mac does not exist
  if (ua.find("Android") != std::string_view::npos) return "Android";
  return "";
}

bool CodeGuard::allowed(double now) const { return now >= locked_until_; }

void CodeGuard::failed(double now) {
  fails_.push_back(now);
  fails_.erase(std::remove_if(fails_.begin(), fails_.end(), [&](double t) { return now - t > 60; }), fails_.end());
  if (fails_.size() >= (size_t)max_fails_) { locked_until_ = now + 60; fails_.clear(); }
}

std::string hex(std::string_view bytes) {
  static const char* h = "0123456789abcdef";
  std::string r;
  for (unsigned char c : bytes) { r += h[c >> 4]; r += h[c & 15]; }
  return r;
}

// ── files ───────────────────────────────────────────────────────────
bool is_video_name(std::string_view name) {
  static const char* v[] = {"mkv", "mp4", "m4v", "mov", "avi", "webm", "wmv", "flv", "ts", "m2ts", "mts", "mpg",
                            "mpeg", "vob", "ogv", "3gp", "f4v", "divx", "rmvb", "rm", "asf", "mxf", "y4m", "dv", "dif"};
  std::string e = ext_of(name);
  for (auto x : v) if (e == x) return true;
  return false;
}

bool is_audio_name(std::string_view name) {
  static const char* v[] = {"mp3", "m4a", "aac", "flac", "wav", "ogg", "oga", "opus", "wma", "aiff", "aif", "ape",
                            "mka", "alac", "wv"};
  std::string e = ext_of(name);
  for (auto x : v) if (e == x) return true;
  return false;
}

bool natural_less(std::string_view a, std::string_view b) {
  size_t i = 0, j = 0;
  while (i < a.size() && j < b.size()) {
    unsigned char ca = (unsigned char)a[i], cb = (unsigned char)b[j];
    if (std::isdigit(ca) && std::isdigit(cb)) {
      size_t i2 = i, j2 = j;
      while (i2 < a.size() && a[i2] == '0') ++i2;
      while (j2 < b.size() && b[j2] == '0') ++j2;
      size_t ie = i2, je = j2;
      while (ie < a.size() && std::isdigit((unsigned char)a[ie])) ++ie;
      while (je < b.size() && std::isdigit((unsigned char)b[je])) ++je;
      if (ie - i2 != je - j2) return ie - i2 < je - j2;
      int c = a.substr(i2, ie - i2).compare(b.substr(j2, je - j2));
      if (c) return c < 0;
      i = ie; j = je;
      continue;
    }
    int la = std::tolower(ca), lb = std::tolower(cb);
    if (la != lb) return la < lb;
    ++i; ++j;
  }
  if (a.size() - i != b.size() - j) return a.size() - i < b.size() - j;
  return a < b;
}

bool episode_of(std::string_view name, int& season, int& episode, size_t* marker) {
  std::string n = lower(name);
  size_t dot = n.rfind('.');
  if (dot != std::string::npos) n.resize(dot);
  auto digits = [&](size_t& k, int max_len) {
    int v = 0, len = 0;
    while (k < n.size() && std::isdigit((unsigned char)n[k]) && len < max_len) { v = v * 10 + (n[k] - '0'); ++k; ++len; }
    return len ? v : -1;
  };
  auto boundary = [&](size_t k) { return k == 0 || !std::isalpha((unsigned char)n[k - 1]); };
  // S01E03
  for (size_t k = 0; k + 1 < n.size(); ++k) {
    if (n[k] != 's' || !boundary(k) || !std::isdigit((unsigned char)n[k + 1])) continue;
    size_t p = k + 1;
    int s = digits(p, 3);
    while (p < n.size() && (n[p] == ' ' || n[p] == '.' || n[p] == '_' || n[p] == '-')) ++p;
    if (p < n.size() && n[p] == 'e') {
      ++p;
      int e = digits(p, 4);
      if (e >= 0) { season = s; episode = e; if (marker) *marker = k; return true; }
    }
  }
  // 1x03
  for (size_t k = 1; k + 1 < n.size(); ++k) {
    if (n[k] != 'x' || !std::isdigit((unsigned char)n[k - 1]) || !std::isdigit((unsigned char)n[k + 1])) continue;
    size_t a = k;
    while (a > 0 && std::isdigit((unsigned char)n[a - 1])) --a;
    if (k - a > 2 || (a > 0 && std::isalnum((unsigned char)n[a - 1]))) continue;
    size_t p = a;
    int s = digits(p, 2);
    p = k + 1;
    int e = digits(p, 3);
    if (p < n.size() && std::isdigit((unsigned char)n[p])) continue;  // a resolution like 1920x1080
    season = s; episode = e; if (marker) *marker = a;
    return true;
  }
  // "ep 3", "episode 03", "e03", "cap 3", "capítulo 3"
  static const char* words[] = {"episode", "episodio", "episódio", "capitulo", "capítulo", "cap", "ep", "e"};
  for (auto w : words) {
    std::string ws = w;
    for (size_t k = n.find(ws); k != std::string::npos; k = n.find(ws, k + 1)) {
      if (!boundary(k)) continue;
      size_t p = k + ws.size();
      while (p < n.size() && (n[p] == ' ' || n[p] == '.' || n[p] == '_' || n[p] == '-')) ++p;
      int e = digits(p, 4);
      if (e >= 0 && (p >= n.size() || !std::isalpha((unsigned char)n[p]))) { season = 0; episode = e; if (marker) *marker = k; return true; }
    }
  }
  return false;
}

namespace {
std::string series_key(std::string_view name) {
  int season=0, episode=0; size_t marker=0;
  if (!episode_of(name,season,episode,&marker)) return {};
  std::string prefix=lower(name.substr(0,marker));
  // Release groups are metadata, not series identity.
  while (!prefix.empty() && prefix.front()=='[') {
    auto end=prefix.find(']'); if(end==std::string::npos) break;
    prefix.erase(0,end+1); prefix=trim(prefix);
  }
  std::string key; bool space=false;
  for (unsigned char c:prefix) {
    if (std::isalnum(c) || c>=128) { if(space && !key.empty()) key+=' '; key+=(char)c; space=false; }
    else space=true;
  }
  return key;
}
}
std::string series_id(std::string_view name) { return series_key(name); }

std::string series_title(std::string_view name) {
  int season = 0, episode = 0; size_t marker = 0;
  if (!episode_of(name, season, episode, &marker)) return {};
  std::string t(name.substr(0, marker));
  while (!t.empty() && t.front() == '[') { auto end = t.find(']'); if (end == std::string::npos) break; t.erase(0, end + 1); }
  std::string out; bool space = false;
  for (unsigned char c : t) {
    bool sep = c == '.' || c == '_' || c == ' ';
    if (sep) { space = true; continue; }
    if (space && !out.empty()) out += ' ';
    out += (char)c; space = false;
  }
  while (!out.empty() && (out.back() == '-' || out.back() == ' ' || out.back() == '(' || out.back() == '[')) out.pop_back();
  return out;
}

std::string episode_label(std::string_view name) {
  int season = 0, episode = 0;
  if (!episode_of(name, season, episode)) return {};
  char b[24];
  if (season > 0) std::snprintf(b, sizeof b, "S%02dE%02d", season, episode);
  else std::snprintf(b, sizeof b, "E%02d", episode);
  return b;
}

void card_words(std::string_view name, std::string& title, std::string& sub) {
  title.clear(); sub.clear();
  std::string stem(name);
  if (auto slash = stem.find_last_of("/\\"); slash != std::string::npos) stem.erase(0, slash + 1);
  if (auto dot = stem.rfind('.'); dot != std::string::npos && dot > 0 && stem.size() - dot <= 5) stem.resize(dot);
  if (std::string ep = episode_label(stem); !ep.empty()) {
    title = series_title(stem);
    sub = ep;
    if (!title.empty()) return;
    sub.clear();
  }
  // words: a name with spaces keeps its dots ("Mr. Robot"); one without uses them as spaces
  const bool spaced = stem.find(' ') != std::string::npos;
  std::vector<std::string> words;
  std::string cur;
  for (char c : stem) {
    bool sep = c == ' ' || c == '_' || (!spaced && c == '.');
    if (sep) { if (!cur.empty()) words.push_back(cur), cur.clear(); continue; }
    cur += c;
  }
  if (!cur.empty()) words.push_back(cur);
  while (!words.empty() && words.front().front() == '[') words.erase(words.begin());  // a release group
  auto lower = [](std::string w) { for (auto& c : w) c = (char)std::tolower((unsigned char)c); return w; };
  auto strip = [](std::string w) {
    while (!w.empty() && (w.front() == '(' || w.front() == '[')) w.erase(0, 1);
    while (!w.empty() && (w.back() == ')' || w.back() == ']')) w.pop_back();
    return w;
  };
  static const char* tags[] = {"2160p", "1080p", "720p", "576p", "480p", "4k", "uhd", "hdr", "hdr10", "dv", "web", "web-dl", "webdl",
                               "webrip", "bluray", "blu-ray", "brrip", "bdrip", "dvdrip", "hdtv", "hdrip", "x264", "x265", "h264", "h265",
                               "hevc", "avc", "aac", "ac3", "dts", "atmos", "10bit", "8bit", "proper", "repack", "remux", "extended",
                               "unrated", "imax", "nf", "amzn", "dsnp", "hmax", "yts", "rarbg"};
  size_t cut = words.size();
  for (size_t i = 1; i < words.size(); ++i) {
    std::string w = lower(strip(words[i]));
    bool tag = false;
    for (auto t : tags) if (w == t) tag = true;
    if (!tag && words[i].front() == '[') tag = true;
    if (tag) { cut = i; break; }
  }
  auto is_year = [](const std::string& w) {
    return w.size() == 4 && std::all_of(w.begin(), w.end(), [](char c) { return c >= '0' && c <= '9'; }) && (w[0] == '1' || w[0] == '2') &&
           std::atoi(w.c_str()) >= 1900 && std::atoi(w.c_str()) <= 2100;
  };
  if (cut >= 2 && is_year(strip(words[cut - 1]))) { sub = strip(words[cut - 1]); --cut; }
  for (size_t i = 0; i < cut; ++i) { if (!title.empty()) title += ' '; title += words[i]; }
  while (!title.empty() && (title.back() == '-' || title.back() == ' ')) title.pop_back();
  if (title.empty()) { title = stem; sub.clear(); }
}

bool same_series(std::string_view a,std::string_view b) {
  auto key=series_key(a); return !key.empty() && key==series_key(b);
}
std::string next_file(std::string_view current,const std::vector<std::string>& siblings) {
  std::vector<std::string> files;
  for(auto& f:siblings) if(is_playable_name(f)) files.push_back(f);
  std::sort(files.begin(),files.end(),[](auto& a,auto& b){return natural_less(a,b);});
  for(size_t i=0;i+1<files.size();++i) if(lower(files[i])==lower(current)) return files[i+1];
  return {};
}

std::string next_episode(std::string_view current, const std::vector<std::string>& siblings) {
  std::vector<std::string> files;
  for (auto& s : siblings) if (is_playable_name(s)) files.push_back(s);
  if (files.size() < 2) return "";
  int cs = 0, ce = 0;
  bool numbered = episode_of(current, cs, ce);
  if (numbered) {
    // the smallest (season, episode) after the current one
    std::string best;
    int bs = 0, be = 0;
    for (auto& f : files) {
      int s = 0, e = 0;
      if (!episode_of(f, s, e) || !same_series(current,f)) continue;
      bool after = s > cs || (s == cs && e > ce);
      if (!after) continue;
      if (best.empty() || s < bs || (s == bs && e < be) || (s == bs && e == be && natural_less(f, best))) {
        best = f; bs = s; be = e;
      }
    }
    if (!best.empty()) return best;
  }
  std::sort(files.begin(), files.end(), [](const std::string& a, const std::string& b) { return natural_less(a, b); });
  std::string cur = lower(current);
  for (size_t i = 0; i + 1 < files.size(); ++i)
    if (lower(files[i]) == cur) return numbered ? "" : files[i + 1];
  return "";
}

namespace {
// a name's shape with its numbers taken out: "[Sub] Show - 01 [1080p].mkv" -> "[sub] show - # [#p]"
// (the extension dropped). Empty when the name has no number at all.
std::string number_shape(std::string_view name) {
  std::string stem(name);
  if (auto dot = stem.rfind('.'); dot != std::string::npos && dot > 0 && stem.size() - dot <= 5) stem.resize(dot);
  std::string out; bool any = false;
  for (size_t i = 0; i < stem.size();) {
    if (stem[i] >= '0' && stem[i] <= '9') {
      while (i < stem.size() && stem[i] >= '0' && stem[i] <= '9') ++i;
      out += '#'; any = true;
    } else out += (char)std::tolower((unsigned char)stem[i++]);
  }
  return any ? out : std::string();
}
}  // namespace

std::string next_in_series(std::string_view current, const std::vector<std::string>& siblings) {
  if (!is_video_name(current)) return "";
  std::vector<std::string> videos;
  for (auto& s : siblings) if (is_video_name(s)) videos.push_back(s);
  if (videos.size() < 2) return "";
  int s = 0, e = 0;
  if (episode_of(current, s, e)) {
    std::string next = next_episode(current, videos);
    return !next.empty() && same_series(current, next) ? next : "";
  }
  const std::string shape = number_shape(current);
  if (shape.empty()) return "";
  std::vector<std::string> same;
  for (auto& v : videos) if (number_shape(v) == shape) same.push_back(v);
  std::sort(same.begin(), same.end(), [](const std::string& a, const std::string& b) { return natural_less(a, b); });
  const std::string cur = lower(current);
  for (size_t i = 0; i + 1 < same.size(); ++i) if (lower(same[i]) == cur) return same[i + 1];
  return "";
}

bool looks_like_series(const std::vector<std::string>& names) {
  int n = 0, s = 0, e = 0;
  for (auto& x : names) if (is_playable_name(x) && episode_of(x, s, e) && ++n >= 2) return true;
  return false;
}

bool path_within(std::string_view root, std::string_view path) {
  auto norm = [](std::string_view p) {
    std::string r;
    for (char c : p) r += c == '/' ? '\\' : (char)std::tolower((unsigned char)c);
    while (r.size() > 3 && r.back() == '\\') r.pop_back();
    return r;
  };
  std::string r = norm(root), p = norm(path);
  if (r.size() < 3 || p.size() < 3) return false;
  auto normal = [](const std::string& v) {
    if (!std::isalpha((unsigned char)v[0]) || v[1] != ':' || v[2] != '\\') return false;
    for (size_t i = 2; i < v.size(); ++i) {
      unsigned char c = v[i];
      if (c < 32 || c == 127 || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return false;
    }
    size_t at = 3;
    while (at < v.size()) {
      size_t end = v.find('\\', at);
      if (end == std::string::npos) end = v.size();
      auto part = v.substr(at, end - at);
      if (part.empty() || part.back() == '.' || part.back() == ' ') return false;
      auto stem = part.substr(0, part.find('.'));
      if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" || stem == "conin$" || stem == "conout$" ||
          (stem.size() == 4 && (stem.substr(0, 3) == "com" || stem.substr(0, 3) == "lpt") && stem[3] >= '0' && stem[3] <= '9')) return false;
      at = end + 1;
    }
    return true;
  };
  if (!normal(r) || !normal(p)) return false;
  // no "." or ".." segments: they would walk out of the root after this check
  size_t k = 0;
  while (k <= p.size()) {
    size_t e = p.find('\\', k);
    if (e == std::string::npos) e = p.size();
    std::string_view seg(p.data() + k, e - k);
    if (seg == "." || seg == "..") return false;
    k = e + 1;
  }
  if (p == r) return true;
  if (p.size() <= r.size() || p.compare(0, r.size(), r) != 0) return false;
  return r.back() == '\\' || p[r.size()] == '\\';
}

// ── JSON ────────────────────────────────────────────────────────────
std::string escape(std::string_view s) { return json_quote(std::string(s)); }

std::string state_json(const State& s) {
  std::string o = "{\"t\":\"state\",\"view\":";
  o += s.playing_view ? "\"play\"" : "\"home\"";
  o += ",\"file\":" + escape(s.file);
  o += ",\"pos\":" + num(s.pos, 2) + ",\"dur\":" + num(s.dur, 2) + ",\"speed\":" + num(s.speed, 2);
  o += ",\"vol\":" + num(s.volume, 0);
  o += std::string(",\"paused\":") + (s.paused ? "true" : "false");
  o += std::string(",\"muted\":") + (s.muted ? "true" : "false");
  o += std::string(",\"ended\":") + (s.ended ? "true" : "false");
  o += std::string(",\"audioOnly\":") + (s.audio_only ? "true" : "false");
  o += ",\"cover\":" + std::to_string(s.cover);
  if (s.audio_only && !s.title.empty()) o += ",\"title\":" + escape(s.title) + ",\"artist\":" + escape(s.artist);
  o += std::string(",\"sub\":{\"has\":") + (s.has_subs ? "true" : "false") + ",\"on\":" + (s.subs_on ? "true" : "false") +
       ",\"delay\":" + num(s.sub_delay, 2) + ",\"size\":" + std::to_string(s.sub_size) +
       ",\"sizeMin\":" + std::to_string(s.sub_size_min) + ",\"sizeMax\":" + std::to_string(s.sub_size_max) +
       ",\"pos\":" + num(s.sub_pos, 1) + ",\"posDefault\":" + num(s.sub_pos_default, 1) +
       ",\"posMin\":" + num(s.sub_pos_min, 1) + ",\"posMax\":" + num(s.sub_pos_max, 1) + "}";
  o += ",\"audio\":{\"n\":" + std::to_string(s.audio_tracks) + ",\"i\":" + std::to_string(s.audio_index) + ",\"m\":" + std::to_string(s.audio_mix) + "}";
  o += ",\"timer\":{\"m\":" + std::to_string(s.timer) + ",\"left\":" + num(s.timer_left, 0) + "}";
  o += ",\"queueRevision\":"+std::to_string(s.queue_revision);
  o += std::string(",\"queueActive\":")+(s.queue_active?"true":"false")+",\"queueBusy\":"+(s.queue_busy?"true":"false");
  o += ",\"queueResult\":"+std::to_string(s.queue_result);
  o += ",\"queueNoticeId\":"+std::to_string(s.queue_notice_id)+",\"queueNotice\":"+escape(s.queue_notice);
  o += ",\"next\":" + escape(s.next);
  o += std::string(",\"nextAuto\":") + (s.next_auto ? "true" : "false");
  if (!s.lang.empty()) o += ",\"lang\":" + escape(s.lang);
  o += ",\"color\":" + escape(s.color);
  o += ",\"media\":" + std::to_string(s.media) + "}";
  return o;
}

bool parse_command(std::string_view text, Command& out) {
  JVal j;
  if (text.size() > kMaxFrame || !json_parse(std::string(text), j) || j.t != JVal::Obj) return false;
  out = Command{};
  const auto& media = j["media"];
  if (media.t != JVal::Null) {
    if (media.t != JVal::Num || media.num() < 0 || media.num() > 9007199254740991.0 || std::floor(media.num()) != media.num()) return false;
    out.has_media = true; out.media = (uint64_t)media.num();
  }
  out.c = j["c"].str();
  if (out.c.empty() || out.c.size() > 24) return false;
  const JVal& v = j["v"];
  if (v.t == JVal::Num || v.t == JVal::Bool) {
    out.v = v.num();
    out.has_v = std::isfinite(out.v);
    if (!out.has_v) out.v = 0;
  }
  out.path = j["path"].str();
  if (out.path.size() > 4096 || out.path.find('\0') != std::string::npos) return false;
  static const char* simple[] = {"toggle", "next", "what"};
  for (auto k : simple) if (out.c == k) return true;
  if(out.c=="queue-add")return !out.path.empty()&&is_playable_name(out.path);
  if(out.c=="queue-folder")return !out.path.empty();
  if(out.c=="queue-clear")return true;
  if (out.c == "open") return !out.path.empty() && is_playable_name(out.path);
  if (v.t != JVal::Num || !out.has_v || std::abs(out.v) > 1000000000.0) return false;
  if(out.c=="queue-move") {
    const auto& to=j["to"];
    if(out.v<1||out.v>1000000000.0||std::floor(out.v)!=out.v||to.t!=JVal::Num||!std::isfinite(to.num())||to.num()<0||to.num()>=512||std::floor(to.num())!=to.num())return false;
    out.to=(int)to.num();return true;
  }
  if(out.c=="queue-play"||out.c=="queue-remove"||out.c=="queue-up"||out.c=="queue-down")
    return out.v>=1&&out.v<=1000000000.0&&std::floor(out.v)==out.v;
  if (out.c == "timer") return out.v >= -1 && out.v <= 600;
  if (out.c == "skip") return std::abs(out.v) <= 600;
  if (out.c == "vol") return out.v >= 0 && out.v <= 200;
  if (out.c == "seek" || out.c == "peek") return out.v >= 0;
  if (out.c == "sub") return out.v == 0 || out.v == 1;
  if (out.c == "subsize") return out.v >= 28 && out.v <= 72;
  if (out.c == "subpos") return out.v >= 12 && out.v <= 97;
  if (out.c == "subdelay") return std::abs(out.v) <= 600;
  if (out.c == "audio") return out.v >= -1 && out.v <= 1000;
  return false;
}

std::string listing_json(const std::string& title, const std::string& path, const std::string& parent,
                         const std::vector<Entry>& entries) {
  std::string o = "{\"t\":\"list\",\"title\":" + escape(title) + ",\"path\":" + escape(path) + ",\"parent\":" + escape(parent) +
                  ",\"items\":[";
  for (size_t i = 0; i < entries.size(); ++i) {
    auto& e = entries[i];
    if (i) o += ',';
    o += "{\"name\":" + escape(e.name) + ",\"path\":" + escape(e.path);
    if (e.dir) o += ",\"dir\":true";
    if (e.series) o += ",\"series\":true";
    if (e.audio) o += ",\"audio\":true";
    if (e.dur > 0) o += ",\"pos\":" + num(e.pos, 1) + ",\"dur\":" + num(e.dur, 1);
    if (e.seen) o += ",\"seen\":true";
    o += '}';
  }
  o += "]}";
  return o;
}

}  // namespace remote
