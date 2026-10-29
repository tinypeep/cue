#include "sub_style.h"

#include <cstdio>

const char* const kSubFontFamily[3] = {"Cue Omni", "Cue Clean", "Cue Readable"};
const char* const kSubFontLabel[3] = {"Omni", "Clean", "Readable"};

static std::string rgba(double r, double g, double b, double a) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.3f/%.3f/%.3f/%.3f", r, g, b, a);
  return buf;
}

StyleCaps style_caps(const SubStyle& st, SubKind kind) {
  StyleCaps c;
  if (kind == SubKind::Bitmap) {
    // image subtitles: fonts, sizes and colours do not exist; keep only sync
    c.font = c.size = c.opacity = c.position = false;
  } else if (kind == SubKind::Ass) {
    c.ass_toggle = true;
    if (st.ass_mode == 0) c.font = c.size = c.opacity = c.position = false;
  }
  return c;
}

std::vector<PropSet> sub_style_props(const SubStyle& st, SubKind kind) {
  std::vector<PropSet> v;
  auto add = [&](std::initializer_list<const char*> names, std::string val) {
    PropSet p;
    for (auto n : names) p.names.push_back(n);
    p.value = std::move(val);
    v.push_back(std::move(p));
  };

  char num[32];
  auto f = [&](double x) { std::snprintf(num, sizeof num, "%.2f", x); return std::string(num); };
  add({"sub-ass-override"}, (kind == SubKind::Ass && st.ass_mode == 0) ? "no" : "force");
  add({"sub-font"}, kSubFontFamily[st.font < 0 || st.font > 2 ? 1 : st.font]);
  add({"sub-font-size"}, std::to_string(st.size));
  add({"sub-bold"}, "no");
  add({"sub-blur"}, "0");
  // spacing follows the text size (like em-based tracking and leading): the same setting looks
  // the same at 28 or 72, and a tight negative leading never makes small lines collide
  double em = st.size / 44.0;
  add({"sub-spacing"}, f(st.tracking * em));
  add({"sub-line-spacing", "sub-ass-line-spacing", "ass-line-spacing"}, f(st.line_gap * em));
  add({"sub-color"}, rgba(1, 1, 1, st.text_op / 100.0));
  add({"sub-pos"}, std::to_string((int)(st.pos + 0.5)));

  double bg = st.bg_op / 100.0;
  if (st.bg_op > 0) {
    // A box that hugs the subtitle block, never a full-width bar.
    // Only the box: no outline around the letters (it doubled up into a dark halo).
    // libass sizes the background box from the shadow offset, so that is the padding.
    add({"sub-border-style"}, "background-box");
    add({"sub-back-color"}, rgba(0, 0, 0, bg));
    add({"sub-outline-size", "sub-border-size"}, "0");
    add({"sub-outline-color", "sub-border-color"}, rgba(0, 0, 0, 0));
    add({"sub-shadow-color"}, rgba(0, 0, 0, bg));
    add({"sub-shadow-offset"}, "4");
  } else {
    // Transparent background: the outline is the user's call, from none to thick.
    add({"sub-border-style"}, "outline-and-shadow");
    add({"sub-back-color"}, rgba(0, 0, 0, 0));  // older mpv draws its box from this: clear it
    add({"sub-outline-color", "sub-border-color"}, rgba(0, 0, 0, 0.9 * st.text_op / 100.0));
    add({"sub-outline-size", "sub-border-size"}, f(st.outline * 0.9));
    add({"sub-shadow-offset"}, st.outline > 0 ? "1" : "0");
    add({"sub-shadow-color"}, rgba(0, 0, 0, 0.45 * st.text_op / 100.0));
  }
  return v;
}
