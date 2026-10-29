#include "json.h"

#include <cstdio>
#include <cstdlib>

const JVal& JVal::operator[](const std::string& k) const {
  static const JVal none;
  if (t != Obj) return none;
  auto it = o.find(k);
  return it == o.end() ? none : it->second;
}

namespace {
struct P {
  const std::string& s;
  size_t i = 0;
  void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i; }
  bool lit(const char* w) {
    size_t k = 0;
    while (w[k]) { if (i + k >= s.size() || s[i + k] != w[k]) return false; ++k; }
    i += k;
    return true;
  }
  static void put_utf8(std::string& o, unsigned c) {
    if (c < 0x80) o += (char)c;
    else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 63)); }
    else if (c < 0x10000) { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 63)); o += (char)(0x80 | (c & 63)); }
    else { o += (char)(0xF0 | (c >> 18)); o += (char)(0x80 | ((c >> 12) & 63)); o += (char)(0x80 | ((c >> 6) & 63)); o += (char)(0x80 | (c & 63)); }
  }
  bool str(std::string& out) {
    if (s[i] != '"') return false;
    ++i;
    while (i < s.size() && s[i] != '"') {
      char c = s[i++];
      if (c != '\\') { out += c; continue; }
      if (i >= s.size()) return false;
      char e = s[i++];
      switch (e) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'u': {
          if (i + 4 > s.size()) return false;
          unsigned c1 = (unsigned)std::strtoul(s.substr(i, 4).c_str(), nullptr, 16);
          i += 4;
          if (c1 >= 0xD800 && c1 < 0xDC00 && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
            unsigned c2 = (unsigned)std::strtoul(s.substr(i + 2, 4).c_str(), nullptr, 16);
            i += 6;
            c1 = 0x10000 + ((c1 - 0xD800) << 10) + (c2 - 0xDC00);
          }
          put_utf8(out, c1);
          break;
        }
        default: out += e;
      }
    }
    if (i >= s.size()) return false;
    ++i;
    return true;
  }
  bool val(JVal& v, int depth) {
    if (depth > 32) return false;
    ws();
    if (i >= s.size()) return false;
    char c = s[i];
    if (c == '{') {
      v.t = JVal::Obj; ++i; ws();
      if (i < s.size() && s[i] == '}') { ++i; return true; }
      while (true) {
        ws();
        std::string k;
        if (i >= s.size() || !str(k)) return false;
        ws();
        if (i >= s.size() || s[i] != ':') return false;
        ++i;
        if (!val(v.o[k], depth + 1)) return false;
        ws();
        if (i < s.size() && s[i] == ',') { ++i; continue; }
        if (i < s.size() && s[i] == '}') { ++i; return true; }
        return false;
      }
    }
    if (c == '[') {
      v.t = JVal::Arr; ++i; ws();
      if (i < s.size() && s[i] == ']') { ++i; return true; }
      while (true) {
        v.a.emplace_back();
        if (!val(v.a.back(), depth + 1)) return false;
        ws();
        if (i < s.size() && s[i] == ',') { ++i; continue; }
        if (i < s.size() && s[i] == ']') { ++i; return true; }
        return false;
      }
    }
    if (c == '"') { v.t = JVal::Str; return str(v.s); }
    if (lit("true")) { v.t = JVal::Bool; v.b = true; return true; }
    if (lit("false")) { v.t = JVal::Bool; v.b = false; return true; }
    if (lit("null")) { v.t = JVal::Null; return true; }
    char* end = nullptr;
    v.n = std::strtod(s.c_str() + i, &end);
    if (end == s.c_str() + i) return false;
    v.t = JVal::Num;
    i = (size_t)(end - s.c_str());
    return true;
  }
};
}  // namespace

bool json_parse(const std::string& text, JVal& out) {
  P p{text};
  out = JVal{};
  return p.val(out, 0);
}

std::string json_quote(const std::string& s) {
  std::string o = "\"";
  for (unsigned char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += (char)c;
    }
  }
  return o + "\"";
}
