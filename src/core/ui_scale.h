// Persisted IDs: -1 small, 2 medium (the default), 0 large, 1 larger.
constexpr int kDefaultUiSize = 2;
#pragma once
#include <algorithm>
#include <cmath>
inline int ui_size_index(int choice) { return choice == -1 ? 0 : choice == 2 ? 1 : choice == 1 ? 3 : 2; }
inline int ui_size_choice(int index) { constexpr int choices[] = {-1, 2, 0, 1}; return choices[std::clamp(index, 0, 3)]; }
inline int next_ui_size(int choice, int direction) { return ui_size_choice(ui_size_index(choice) + direction); }

// The interface scale (physical px per art px). A pure function of DPI, window and choice: the
// same window always gets the same size, whatever happened before (no hysteresis), so a video
// never opens at one size and "corrects" itself later.
//   - it grows with the window: in step with it up to a roomy window (1.4x the reference of
//     1280 DIPs wide), then at half the rate, so full screen is a little bigger than the
//     window without taking over the picture (for every choice);
//   - width leads (the dock is a row): a 2.39:1 window is as wide as its 16:9 neighbour and
//     gets the same controls; the height only holds it back when it is under 520 DIPs per 1280 of width (wider than 2.46:1);
//   - the four choices are proportions of that (0.5, 0.75, 1, 1.5), on whole or half art px;
//   - a window too small for the design canvas (320 x 180 art px) holds it down.
// choice: persisted id: -1 small, 2 medium, 0 large, 1 larger.
inline double ui_scale_at(unsigned dpi, double k, int choice) {
  if (dpi < 48) dpi = 96;
  static const double mult[4] = {0.5, 0.75, 1.0, 1.5};  // pequena, média, grande, maior
  return std::max(1.0, std::round(dpi / 48.0 * k * mult[ui_size_index(choice)] * 2) / 2);
}
// The scale of the smallest windows (the reference size): what minimum window sizes use.
inline double ui_base_scale(unsigned dpi, int choice) { return ui_scale_at(dpi, 1.0, choice); }
inline double ui_scale(unsigned dpi, int cw, int ch, int choice) {
  if (dpi < 48) dpi = 96;
  double dw = cw * 96.0 / dpi, dh = ch * 96.0 / dpi;  // the window in DIPs
  double k = std::clamp(std::min(dw / 1280.0, dh / 520.0), 1.0, 3.0);
  // past the size of a roomy window (1.4x the reference) the controls grow at half the rate:
  // in full screen they get bigger than in the window, but less than the picture does
  if (k > 1.4) k = 1.4 + 0.5 * (k - 1.4);
  double s = ui_scale_at(dpi, k, choice);
  double fit = std::floor(std::min(cw / 320.0, ch / 180.0) * 2) / 2;
  return std::max(1.0, std::min(s, std::max(1.0, fit)));
}

// Portrait windows recompose the controls into a compact dock about this many px of art wide.
constexpr int kCompactDockW = 204;
// Instead of stretching that dock across the whole width at the landscape scale (tiny icons),
// a portrait window raises the scale until the dock fills a share of the width that follows
// the chosen size: narrower than the window, and bigger.
inline double portrait_scale(double s, int cw, int ch, int choice) {
  if (cw <= 0 || ch < cw * 1.15) return s;
  static const double share[4] = {0.72, 0.80, 0.88, 0.94};
  double want = cw * share[ui_size_index(choice)] / kCompactDockW;
  double ps = std::floor(want * 2) / 2;  // whole or half art px, like the medium size
  return std::max(s, std::min(ps, s + 2));
}
