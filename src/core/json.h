// Minimal JSON for Cue's own state file. Not a general-purpose parser.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <cmath>
#include <limits>

struct JVal {
  enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
  bool b = false;
  double n = 0;
  std::string s;
  std::vector<JVal> a;
  std::map<std::string, JVal> o;

  const JVal& operator[](const std::string& k) const;
  double num(double def = 0) const { return t == Num ? n : (t == Bool ? (b ? 1 : 0) : def); }
  template<class Int = int> Int integer(Int def = 0) const {
    double v = num(static_cast<double>(def));
    if (!std::isfinite(v)) return def;
    // Compare before converting; INT64_MAX rounds to 2^63 as a double.
    if (v >= static_cast<double>(std::numeric_limits<Int>::max())) return std::numeric_limits<Int>::max();
    if (v <= static_cast<double>(std::numeric_limits<Int>::lowest())) return std::numeric_limits<Int>::lowest();
    return static_cast<Int>(v);
  }
  std::string str(const std::string& def = "") const { return t == Str ? s : def; }
  bool boolean(bool def = false) const { return t == Bool ? b : (t == Num ? n != 0 : def); }
};

bool json_parse(const std::string& text, JVal& out);
std::string json_quote(const std::string& s);
