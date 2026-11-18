#pragma once
#include <cstddef>
#include <string_view>
inline bool update_temp_directory_name(std::wstring_view name) {
  constexpr std::wstring_view prefix=L"Cue-update-";
  if(!name.starts_with(prefix)||name.size()!=prefix.size()+32)return false;
  for(wchar_t c:name.substr(prefix.size()))if(!((c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f')))return false;
  return true;
}
// The "MZ" at the start of a downloaded installer, however the HTTP reads split the first bytes.
struct ExeHeader {
  char bytes[2] = {};
  int seen = 0;
  bool feed(const char* data, std::size_t n) {
    for (std::size_t i = 0; i < n && seen < 2; ++i) bytes[seen++] = data[i];
    return seen == 2;
  }
  bool ok() const { return seen == 2 && bytes[0] == 'M' && bytes[1] == 'Z'; }
};
