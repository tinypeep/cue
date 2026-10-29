#include "../src/core/launch_request.h"
#include <cassert>
#include <cstdio>

int main() {
  using namespace launch_request;
  std::vector<std::string> decoded;
  for (const std::vector<std::string>& files : std::vector<std::vector<std::string>>{
      {}, {"C:\\Videos\\clip.mp4"}, {"C:\\Vídeos\\ação 你好 🎬.mkv", "C:\\Legendas\\pt br.srt"},
      {"C:\\" + std::string(2000, 'x') + ".mp4"}}) {
    auto payload = encode(files);
    assert(!payload.empty());
    assert(decode(payload.data(), payload.size(), decoded) && decoded == files);
  }
  assert(!decode(nullptr, 10, decoded));
  assert(!decode("", 0, decoded));
  assert(!decode("\0", 1, decoded));
  assert(!decode("abc", 3, decoded));
  assert(!decode("a\0b\0", 4, decoded)); // missing list terminator
  assert(!decode("a\0\0b\0\0", 6, decoded)); // data after terminator
  assert(!decode("a\0\0\0", 4, decoded));
  assert(decoded.empty());
  assert(encode({""}).empty());
  assert(encode({std::string("a\0b", 3)}).empty());
  assert(encode({std::string(kMaxBytes, 'x')}).empty());
  assert(encode({std::string(kMaxBytes - 2, 'x')}).size() == kMaxBytes);
  assert(encode({std::string(kMaxBytes - 2, 'x'), "x"}).empty());
  assert(!decode("xx", kMaxBytes + 1, decoded));
  std::puts("launch request checks passed");
}
