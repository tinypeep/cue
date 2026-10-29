// Where the engine really draws a subtitle line, computed the way libass does it:
// the font is sized so that its OS/2 win ascent + descent equal the font size, lines are
// one font size apart (plus the line spacing), and a bottom subtitle sits between the bottom
// margin and the top in proportion to sub-pos. Used for the drag target (only over the text).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct SubGlyph { uint32_t cp; uint16_t adv; };
struct SubFontMetrics { int upm, win_asc, win_desc, fallback; const SubGlyph* glyphs; int count; };

const SubFontMetrics& sub_font_metrics(int font);  // 0 Omni, 1 Clean, 2 Readable
// width in px of one line at font size `fs` px, with `tracking_px` extra after each character
double sub_line_width(const std::string& utf8, int font, double fs, double tracking_px);
