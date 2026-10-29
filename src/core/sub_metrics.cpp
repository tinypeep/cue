#include "sub_metrics.h"

#include <algorithm>


namespace {
#include "sub_metrics.inc"

uint32_t next_utf8(const std::string& s, size_t& i) {
  auto b = [&](size_t k) { return k < s.size() ? (uint32_t)(uint8_t)s[k] : 0u; };
  uint32_t c = b(i);
  if (c < 0x80) { i += 1; return c; }
  if ((c >> 5) == 6) { uint32_t r = ((c & 31) << 6) | (b(i + 1) & 63); i += 2; return r; }
  if ((c >> 4) == 14) { uint32_t r = ((c & 15) << 12) | ((b(i + 1) & 63) << 6) | (b(i + 2) & 63); i += 3; return r; }
  uint32_t r = ((c & 7) << 18) | ((b(i + 1) & 63) << 12) | ((b(i + 2) & 63) << 6) | (b(i + 3) & 63);
  i += 4;
  return r;
}
}

const SubFontMetrics& sub_font_metrics(int font) {
  switch (font) {
    case 0: return kSubFont0;
    case 2: return kSubFont2;
    default: return kSubFont1;
  }
}

double sub_line_width(const std::string& s, int font, double fs, double tracking_px) {
  const SubFontMetrics& m = sub_font_metrics(font);
  double em = fs * m.upm / double(m.win_asc + m.win_desc);  // libass: win metrics span the size
  long units = 0;
  int n = 0;
  for (size_t i = 0; i < s.size();) {
    uint32_t cp = next_utf8(s, i);
    auto it = std::lower_bound(m.glyphs, m.glyphs + m.count, cp, [](const SubGlyph& g, uint32_t c) { return g.cp < c; });
    units += (it != m.glyphs + m.count && it->cp == cp) ? it->adv : m.fallback;
    ++n;
  }
  return units * em / m.upm + tracking_px * std::max(0, n - 1);
}
