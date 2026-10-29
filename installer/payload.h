// Cue-Setup.exe = the setup program + an archive appended to its end.
//   [stub exe][LZMA "alone" stream of the archive][footer]
// footer (48 bytes): "CUESETUP" | u64 payload offset | u64 payload size | u64 unpacked size | u64 file count
//                   | u32 CRC-32 of the unpacked archive | u32 reserved
// archive: for each file: u16 path length, path (UTF-8, '/' separators), u64 size, then the bytes.
// tools/make_setup.py writes it; installer/setup.cpp reads it.
#pragma once
#include <cstddef>
#include <cstdint>

constexpr char kPayloadMagic[8] = {'C', 'U', 'E', 'S', 'E', 'T', 'U', 'P'};
#pragma pack(push, 1)
struct PayloadFooter {
  char magic[8];
  uint64_t offset, size, unpacked, files;
  uint32_t crc, reserved;
};
#pragma pack(pop)
static_assert(sizeof(PayloadFooter) == 48, "footer layout");

inline uint32_t crc32_update(uint32_t crc, const unsigned char* p, size_t n) {
  static uint32_t table[256];
  static bool ready = false;
  if (!ready) {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
    ready = true;
  }
  crc = ~crc;
  for (size_t i = 0; i < n; ++i) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
  return ~crc;
}
