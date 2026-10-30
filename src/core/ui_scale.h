// Persisted IDs: -1 small, 2 medium (the default), 0 large, 3 larger (1 was the old 1.5x "larger": read as 3).
constexpr int kDefaultUiSize = 2;
#pragma once
#include <algorithm>
#include <cmath>
constexpr int kUiSizes = 4;
inline int ui_size_index(int choice) { return choice == -1 ? 0 : choice == 2 ? 1 : (choice == 3 || choice == 1) ? 3 : 2; }
inline int ui_size_choice(int index) { constexpr int choices[] = {-1, 2, 0, 3}; return choices[std::clamp(index, 0, kUiSizes - 1)]; }
inline int next_ui_size(int choice, int direction) { return ui_size_choice(ui_size_index(choice) + direction); }

// The interface scale (physical px per art px). A pure function of DPI, window and choice: the
// same window always gets the same size, whatever happened before (no hysteresis), so a video
// never opens at one size and "corrects" itself later.
//   - it grows with the window: in step with it up to a roomy window (1.4x the reference of
//     1280 DIPs wide), then at half the rate, so full screen is a little bigger than the
//     window without taking over the picture (for every choice);
//   - width leads (the dock is a row): a 2.39:1 window is as wide as its 16:9 neighbour and
//     gets the same controls; the height only holds it back when it is under 520 DIPs per 1280 of width (wider than 2.46:1);
//   - the four choices are proportions of that (0.5, 0.75, 1, 1.25: steps of a quarter, so at
//     the reference window they land exactly on 1, 1.5, 2 and 2.5 px per art px), on whole or
//     half art px so the pixel font stays as even as it can be;
//   - every step is at least half an art px bigger than the one before, at any window: a size
//     that looks the same as its neighbour would feel broken;
//   - a window too small for the design canvas (320 x 180 art px) holds it down.
// The base is Windows' own scale (it already weighs pixel density and viewing distance per
// display); the window factor reaches 6x the reference, so a 5K or 8K screen at 100% still
// grows its controls instead of stopping at 4K's.
// choice: persisted id: -1 small, 2 medium, 0 large, 3 larger.
inline double ui_scale_at(unsigned dpi, double k, int choice) {
  if (dpi < 48) dpi = 96;
  static const double mult[kUiSizes] = {0.5, 0.75, 1.0, 1.25};  // pequena, média, grande, maior
  return std::max(1.0, std::round(dpi / 48.0 * k * mult[ui_size_index(choice)] * 2) / 2);
}
// The scale of the smallest windows (the reference size): what minimum window sizes use.
inline double ui_base_scale(unsigned dpi, int choice) { return ui_scale_at(dpi, 1.0, choice); }
inline double ui_scale(unsigned dpi, int cw, int ch, int choice) {
  if (dpi < 48) dpi = 96;
  double dw = cw * 96.0 / dpi, dh = ch * 96.0 / dpi;  // the window in DIPs
  double k = std::clamp(std::min(dw / 1280.0, dh / 520.0), 1.0, 6.0);
  // past the size of a roomy window (1.4x the reference) the controls grow at half the rate:
  // in full screen they get bigger than in the window, but less than the picture does
  if (k > 1.4) k = 1.4 + 0.5 * (k - 1.4);
  double s = 0;
  for (int i = 0; i <= ui_size_index(choice); ++i) {
    double si = ui_scale_at(dpi, k, ui_size_choice(i));
    s = i ? std::max(si, s + 0.5) : si;
  }
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
  static const double share[kUiSizes] = {0.72, 0.80, 0.88, 0.94};
  double want = cw * share[ui_size_index(choice)] / kCompactDockW;
  double ps = std::floor(want * 2) / 2;  // whole or half art px, like the medium size
  return std::max(s, std::min(ps, s + 2));
}
