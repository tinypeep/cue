#include "atmosphere.h"

#include <algorithm>
#include <cmath>

namespace {

struct Hsl { double h, s, l; };

Hsl to_hsl(double r, double g, double b) {
  double mx = std::max({r, g, b}), mn = std::min({r, g, b});
  double l = (mx + mn) / 2, d = mx - mn;
  if (d < 1e-6) return {0, 0, l};
  double s = l > 0.5 ? d / (2 - mx - mn) : d / (mx + mn);
  double h;
  if (mx == r) h = (g - b) / d + (g < b ? 6 : 0);
  else if (mx == g) h = (b - r) / d + 2;
  else h = (r - g) / d + 4;
  return {h / 6, s, l};
}

double hue2(double p, double q, double t) {
  if (t < 0) t += 1;
  if (t > 1) t -= 1;
  if (t < 1.0 / 6) return p + (q - p) * 6 * t;
  if (t < 0.5) return q;
  if (t < 2.0 / 3) return p + (q - p) * (2.0 / 3 - t) * 6;
  return p;
}

Rgb from_hsl(Hsl c) {
  double r, g, b;
  if (c.s <= 0) r = g = b = c.l;
  else {
    double q = c.l < 0.5 ? c.l * (1 + c.s) : c.l + c.s - c.l * c.s, p = 2 * c.l - q;
    r = hue2(p, q, c.h + 1.0 / 3); g = hue2(p, q, c.h); b = hue2(p, q, c.h - 1.0 / 3);
  }
  auto u = [](double v) { return (uint8_t)std::lround(std::clamp(v, 0.0, 1.0) * 255); };
  return {u(r), u(g), u(b)};
}

}  // namespace

FrameColors analyze_frame(const uint32_t* px, int w, int h) {
  FrameColors out;
  if (!px || w < 4 || h < 4) return out;
  constexpr int kBins = 24;
  double wsum[kBins] = {}, rs[kBins] = {}, gs[kBins] = {}, bs[kBins] = {};
  double neutral = 0, total = 0;
  // edge bands (outer 22% of each side) for the ambient light
  double er[4] = {}, eg[4] = {}, eb[4] = {}, ew[4] = {}, el[4] = {}, en[4] = {};
  int step = std::max(1, std::min(w, h) / 48);
  for (int y = 0; y < h; y += step)
    for (int x = 0; x < w; x += step) {
      uint32_t v = px[(size_t)y * w + x];
      double r = ((v >> 16) & 255) / 255.0, g = ((v >> 8) & 255) / 255.0, b = (v & 255) / 255.0;
      double lum = 0.2126 * r + 0.7152 * g + 0.0722 * b;
      Hsl c = to_hsl(r, g, b);
      double fx = (double)x / w, fy = (double)y / h;
      int sides[4] = {fy < 0.22, fx > 0.78, fy > 0.78, fx < 0.22};
      for (int k = 0; k < 4; ++k)
        if (sides[k]) {
          double ww = 0.15 + c.s;  // colour counts more than grey, but grey still lights
          er[k] += r * ww; eg[k] += g * ww; eb[k] += b * ww; ew[k] += ww; el[k] += lum; en[k] += 1;
        }
      total += 1;
      if (lum < 0.07) continue;                 // deep blacks say nothing about the mood
      if (lum > 0.93 && c.s < 0.15) continue;   // blown highlights neither
      if (c.s < 0.12) { neutral += 0.35 + lum; continue; }
      // colourful mid tones weigh most; tiny saturated specks do not win on their own
      double wgt = std::pow(c.s, 1.3) * (0.35 + std::min(lum, 0.7));
      int bin = std::clamp((int)(c.h * kBins), 0, kBins - 1);
      wsum[bin] += wgt; rs[bin] += r * wgt; gs[bin] += g * wgt; bs[bin] += b * wgt;
    }
  if (total <= 0) return out;
  // the most representative hue family (a bin with its neighbours)
  int best = -1;
  double best_score = 0, colored = 0;
  for (int i = 0; i < kBins; ++i) {
    colored += wsum[i];
    double sc = wsum[i] + 0.5 * (wsum[(i + 1) % kBins] + wsum[(i + kBins - 1) % kBins]);
    if (sc > best_score) { best_score = sc; best = i; }
  }
  out.valid = true;
  if (best >= 0 && best_score > 0) {
    double R = 0, G = 0, B = 0, W = 0;
    for (int d = -1; d <= 1; ++d) {
      int i = (best + d + kBins) % kBins;
      double f = d == 0 ? 1.0 : 0.5;
      R += rs[i] * f; G += gs[i] * f; B += bs[i] * f; W += wsum[i] * f;
    }
    Hsl c = to_hsl(R / W, G / W, B / W);
    // tame it: never neon, always readable on the black dock
    c.s = std::min(c.s, 0.55);
    c.l = std::clamp(c.l, 0.62, 0.74);
    out.accent = from_hsl(c);
    // how much of the picture carries that colour decides how much the UI takes it
    double share = best_score / std::max(1e-6, colored + neutral * 0.6);
    double presence = std::min(1.0, colored / (total * 0.12));
    out.strength = std::clamp(share * presence * 1.4, 0.0, 1.0);
  }
  // zones: the outer band of the picture, cut into segments around it
  {
    double zr[FrameColors::kZones] = {}, zg[FrameColors::kZones] = {}, zb[FrameColors::kZones] = {}, zn[FrameColors::kZones] = {};
    auto zone_of = [&](double fx, double fy, int* out) {
      int n = 0;
      if (fy < 0.18) out[n++] = std::clamp((int)(fx * 8), 0, 7);
      if (fx > 0.82) out[n++] = 8 + std::clamp((int)(fy * 5), 0, 4);
      if (fy > 0.82) out[n++] = 13 + std::clamp((int)((1 - fx) * 8), 0, 7);
      if (fx < 0.18) out[n++] = 21 + std::clamp((int)((1 - fy) * 5), 0, 4);
      return n;
    };
    for (int y = 0; y < h; y += step)
      for (int x = 0; x < w; x += step) {
        double fx = (x + 0.5) / w, fy = (y + 0.5) / h;
        int zs[2];
        int n = zone_of(fx, fy, zs);
        if (!n) continue;
        uint32_t v = px[(size_t)y * w + x];
        double r = ((v >> 16) & 255) / 255.0, g = ((v >> 8) & 255) / 255.0, b = (v & 255) / 255.0;
        for (int i = 0; i < n; ++i) { zr[zs[i]] += r; zg[zs[i]] += g; zb[zs[i]] += b; zn[zs[i]] += 1; }
      }
    for (int z = 0; z < FrameColors::kZones; ++z) {
      if (zn[z] <= 0) continue;
      double r = zr[z] / zn[z], g = zg[z] / zn[z], b = zb[z] / zn[z];
      double mx = std::max({r, g, b});
      Hsl c = to_hsl(r, g, b);
      // like the TV: the hue at full strength, its brightness carried separately
      if (c.s > 0.06) c.s = std::min(0.92, c.s * 1.35 + 0.08);
      c.l = c.s > 0.06 ? 0.55 : std::min(0.92, 0.5 + c.l * 0.5);
      out.zone[z] = from_hsl(c);
      out.zone_light[z] = std::clamp(std::pow(mx, 0.8) * 1.15, 0.0, 1.0);
    }
  }
  for (int k = 0; k < 4; ++k) {
    if (ew[k] <= 0) continue;
    Hsl c = to_hsl(er[k] / ew[k], eg[k] / ew[k], eb[k] / ew[k]);
    c.s = std::min(c.s, 0.6);
    c.l = std::clamp(c.l, 0.35, 0.6);
    out.edge[k] = from_hsl(c);
    out.edge_light[k] = std::clamp(el[k] / std::max(1.0, en[k]) * 1.6, 0.0, 1.0);
  }
  return out;
}

bool ColorSmoother::step(Rgb t, double tk, double dt, double tau) {
  if (!primed) { r = t.r; g = t.g; b = t.b; k = tk; primed = true; return true; }
  double a = 1 - std::exp(-dt / std::max(1e-3, tau));
  double nr = r + (t.r - r) * a, ng = g + (t.g - g) * a, nb = b + (t.b - b) * a, nk = k + (tk - k) * a;
  bool moved = std::fabs(nr - r) + std::fabs(ng - g) + std::fabs(nb - b) > 0.02 || std::fabs(nk - k) > 0.0005;
  r = nr; g = ng; b = nb; k = nk;
  return moved;
}

Rgb ColorSmoother::rgb() const {
  auto u = [](double v) { return (uint8_t)std::lround(std::clamp(v, 0.0, 255.0)); };
  return {u(r), u(g), u(b)};
}
