// Minimal JSON for Cue's own state file. Not a general-purpose parser.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

struct JVal {
  enum T { Null, Bool, Num, Str, Arr, Obj } t = Null;
  bool b = false;
  double n = 0;
  std::string s;
  std::vector<JVal> a;
  std::map<std::string, JVal> o;

  const JVal& operator[](const std::string& k) const;
  double num(double def = 0) const { return t == Num ? n : (t == Bool ? (b ? 1 : 0) : def); }
  std::string str(const std::string& def = "") const { return t == Str ? s : def; }
  bool boolean(bool def = false) const { return t == Bool ? b : (t == Num ? n != 0 : def); }
};

bool json_parse(const std::string& text, JVal& out);
std::string json_quote(const std::string& s);
