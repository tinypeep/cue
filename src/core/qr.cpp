#include "qr.h"

#include <algorithm>
#include <cstdlib>

namespace {

constexpr int kMaxVersion = 10;
// per version 1..10 (index 0 unused): error correction codewords per block, and blocks
constexpr int kEccPerBlock[2][kMaxVersion + 1] = {
    {0, 7, 10, 15, 20, 26, 18, 20, 24, 30, 18},   // L
    {0, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26},  // M
};
constexpr int kBlocks[2][kMaxVersion + 1] = {
    {0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4},  // L
    {0, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5},  // M
};
constexpr int kFormatBits[2] = {1, 0};  // L, M

int raw_data_modules(int ver) {
  int r = (16 * ver + 128) * ver + 64;
  if (ver >= 2) {
    int n = ver / 7 + 2;
    r -= (25 * n - 10) * n - 55;
    if (ver >= 7) r -= 36;
  }
  return r;
}

int data_codewords(int ver, int e) { return raw_data_modules(ver) / 8 - kEccPerBlock[e][ver] * kBlocks[e][ver]; }

uint8_t gf_mul(uint8_t x, uint8_t y) {
  int z = 0;
  for (int i = 7; i >= 0; --i) {
    z = (z << 1) ^ ((z >> 7) * 0x11D);
    z ^= ((y >> i) & 1) * x;
  }
  return (uint8_t)z;
}

std::vector<uint8_t> rs_divisor(int degree) {
  std::vector<uint8_t> r((size_t)degree, 0);
  r[(size_t)degree - 1] = 1;
  uint8_t root = 1;
  for (int i = 0; i < degree; ++i) {
    for (size_t j = 0; j < r.size(); ++j) {
      r[j] = gf_mul(r[j], root);
      if (j + 1 < r.size()) r[j] ^= r[j + 1];
    }
    root = gf_mul(root, 0x02);
  }
  return r;
}

std::vector<uint8_t> rs_remainder(const std::vector<uint8_t>& data, const std::vector<uint8_t>& div) {
  std::vector<uint8_t> r(div.size(), 0);
  for (uint8_t b : data) {
    uint8_t f = b ^ r[0];
    r.erase(r.begin());
    r.push_back(0);
    for (size_t i = 0; i < r.size(); ++i) r[i] ^= gf_mul(div[i], f);
  }
  return r;
}

struct Grid {
  int n;
  std::vector<uint8_t> mod, fn;  // fn: function pattern (never masked)
  explicit Grid(int size) : n(size), mod((size_t)size * size, 0), fn((size_t)size * size, 0) {}
  void set(int x, int y, bool dark, bool function = true) {
    mod[(size_t)y * n + x] = dark;
    if (function) fn[(size_t)y * n + x] = 1;
  }
  bool get(int x, int y) const { return mod[(size_t)y * n + x] != 0; }
};

std::vector<int> alignment_positions(int ver, int size) {
  if (ver == 1) return {};
  int n = ver / 7 + 2;
  int step = (ver * 4 + n * 2 + 1) / (n * 2 - 2) * 2;
  std::vector<int> r;
  for (int i = 0, pos = size - 7; i < n - 1; ++i, pos -= step) r.insert(r.begin(), pos);
  r.insert(r.begin(), 6);
  return r;
}

void draw_format(Grid& g, int e, int mask) {
  int data = kFormatBits[e] << 3 | mask;
  int rem = data;
  for (int i = 0; i < 10; ++i) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
  int bits = (data << 10 | rem) ^ 0x5412;
  auto bit = [&](int i) { return ((bits >> i) & 1) != 0; };
  int n = g.n;
  for (int i = 0; i <= 5; ++i) g.set(8, i, bit(i));
  g.set(8, 7, bit(6));
  g.set(8, 8, bit(7));
  g.set(7, 8, bit(8));
  for (int i = 9; i < 15; ++i) g.set(14 - i, 8, bit(i));
  for (int i = 0; i < 8; ++i) g.set(n - 1 - i, 8, bit(i));
  for (int i = 8; i < 15; ++i) g.set(8, n - 15 + i, bit(i));
  g.set(8, n - 8, true);  // the dark module
}

void draw_version(Grid& g, int ver) {
  if (ver < 7) return;
  int rem = ver;
  for (int i = 0; i < 12; ++i) rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
  long bits = (long)ver << 12 | rem;
  for (int i = 0; i < 18; ++i) {
    bool b = ((bits >> i) & 1) != 0;
    int a = g.n - 11 + i % 3, c = i / 3;
    g.set(a, c, b);
    g.set(c, a, b);
  }
}

void draw_finder(Grid& g, int cx, int cy) {
  for (int dy = -4; dy <= 4; ++dy)
    for (int dx = -4; dx <= 4; ++dx) {
      int x = cx + dx, y = cy + dy;
      if (x < 0 || y < 0 || x >= g.n || y >= g.n) continue;
      int d = std::max(std::abs(dx), std::abs(dy));
      g.set(x, y, d != 2 && d != 4);
    }
}

void draw_functions(Grid& g, int ver, int e) {
  int n = g.n;
  for (int i = 0; i < n; ++i) {
    g.set(6, i, i % 2 == 0);
    g.set(i, 6, i % 2 == 0);
  }
  draw_finder(g, 3, 3);
  draw_finder(g, n - 4, 3);
  draw_finder(g, 3, n - 4);
  auto al = alignment_positions(ver, n);
  int k = (int)al.size();
  for (int i = 0; i < k; ++i)
    for (int j = 0; j < k; ++j) {
      if ((i == 0 && j == 0) || (i == 0 && j == k - 1) || (i == k - 1 && j == 0)) continue;
      for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) g.set(al[i] + dx, al[j] + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
    }
  draw_format(g, e, 0);  // reserves the area; drawn for real once the mask is known
  draw_version(g, ver);
}

void place_codewords(Grid& g, const std::vector<uint8_t>& data) {
  size_t i = 0;
  int n = g.n;
  for (int right = n - 1; right >= 1; right -= 2) {
    if (right == 6) right = 5;
    for (int vert = 0; vert < n; ++vert)
      for (int j = 0; j < 2; ++j) {
        int x = right - j;
        bool upward = ((right + 1) & 2) == 0;
        int y = upward ? n - 1 - vert : vert;
        if (g.fn[(size_t)y * n + x]) continue;
        if (i < data.size() * 8) g.mod[(size_t)y * n + x] = (data[i >> 3] >> (7 - (i & 7))) & 1;
        ++i;  // remainder bits stay light
      }
  }
}

bool mask_bit(int m, int x, int y) {
  switch (m) {
    case 0: return (x + y) % 2 == 0;
    case 1: return y % 2 == 0;
    case 2: return x % 3 == 0;
    case 3: return (x + y) % 3 == 0;
    case 4: return (x / 3 + y / 2) % 2 == 0;
    case 5: return x * y % 2 + x * y % 3 == 0;
    case 6: return (x * y % 2 + x * y % 3) % 2 == 0;
    default: return ((x + y) % 2 + x * y % 3) % 2 == 0;
  }
}

void apply_mask(Grid& g, int m) {
  for (int y = 0; y < g.n; ++y)
    for (int x = 0; x < g.n; ++x)
      if (!g.fn[(size_t)y * g.n + x] && mask_bit(m, x, y)) g.mod[(size_t)y * g.n + x] ^= 1;
}

// The standard's penalty: runs, 2x2 blocks, finder-like patterns, and the dark/light balance
// (scored exactly like the reference encoder, so the same mask is chosen).
long penalty(const Grid& g) {
  const int n = g.n;
  long score = 0;
  using Hist = int[7];
  auto add = [&](int run, Hist& h) {
    if (h[0] == 0) run += n;  // light border before the first run
    for (int i = 6; i > 0; --i) h[i] = h[i - 1];
    h[0] = run;
  };
  auto count = [&](const Hist& h) {
    int u = h[1];
    bool core = u > 0 && h[2] == u && h[3] == u * 3 && h[4] == u && h[5] == u;
    return (core && h[0] >= u * 4 && h[6] >= u ? 1 : 0) + (core && h[6] >= u * 4 && h[0] >= u ? 1 : 0);
  };
  auto line = [&](bool rows) {
    for (int a = 0; a < n; ++a) {
      bool color = false;
      int run = 0;
      Hist h = {0, 0, 0, 0, 0, 0, 0};
      for (int b = 0; b < n; ++b) {
        bool c = rows ? g.get(b, a) : g.get(a, b);
        if (c == color) {
          if (++run == 5) score += 3;
          else if (run > 5) ++score;
        } else {
          add(run, h);
          if (!color) score += count(h) * 40;
          color = c;
          run = 1;
        }
      }
      if (color) { add(run, h); run = 0; }
      add(run + n, h);  // light border after the last run
      score += count(h) * 40;
    }
  };
  line(true);
  line(false);
  for (int y = 0; y < n - 1; ++y)
    for (int x = 0; x < n - 1; ++x) {
      bool c = g.get(x, y);
      if (c == g.get(x + 1, y) && c == g.get(x, y + 1) && c == g.get(x + 1, y + 1)) score += 3;
    }
  long dark = 0, total = (long)n * n;
  for (auto v : g.mod) dark += v;
  long k = (std::labs(dark * 20 - total * 10) + total - 1) / total - 1;
  score += std::max(0L, k) * 10;
  return score;
}

}  // namespace

QrCode qr_encode(const std::string& bytes, QrEcc ecc, int mask, int min_version) {
  QrCode out;
  int e = ecc == QrEcc::L ? 0 : 1;
  int ver = std::max(1, min_version);
  for (; ver <= kMaxVersion; ++ver) {
    int count_bits = ver <= 9 ? 8 : 16;
    if (4 + count_bits + (int)bytes.size() * 8 <= data_codewords(ver, e) * 8) break;
  }
  if (ver > kMaxVersion) return out;

  // the bit stream: byte mode, length, data, terminator, padding
  std::vector<uint8_t> bits;
  auto put = [&](uint32_t v, int n) { for (int i = n - 1; i >= 0; --i) bits.push_back((v >> i) & 1); };
  put(4, 4);
  put((uint32_t)bytes.size(), ver <= 9 ? 8 : 16);
  for (unsigned char c : bytes) put(c, 8);
  int cap = data_codewords(ver, e) * 8;
  put(0, std::min(4, cap - (int)bits.size()));
  put(0, (8 - (int)bits.size() % 8) % 8);
  for (uint8_t pad = 0xEC; (int)bits.size() < cap; pad ^= 0xEC ^ 0x11) put(pad, 8);
  std::vector<uint8_t> data(bits.size() / 8, 0);
  for (size_t i = 0; i < bits.size(); ++i) data[i >> 3] |= bits[i] << (7 - (i & 7));

  // blocks with their error correction, interleaved
  int nb = kBlocks[e][ver], ecl = kEccPerBlock[e][ver];
  int raw = raw_data_modules(ver) / 8;
  int short_blocks = nb - raw % nb, short_len = raw / nb;
  auto div = rs_divisor(ecl);
  std::vector<std::vector<uint8_t>> blocks;
  for (int i = 0, k = 0; i < nb; ++i) {
    int len = short_len - ecl + (i < short_blocks ? 0 : 1);
    std::vector<uint8_t> b(data.begin() + k, data.begin() + k + len);
    k += len;
    auto ec = rs_remainder(b, div);
    if (i < short_blocks) b.push_back(0);  // placeholder, skipped when interleaving
    b.insert(b.end(), ec.begin(), ec.end());
    blocks.push_back(std::move(b));
  }
  std::vector<uint8_t> all;
  for (size_t i = 0; i < blocks[0].size(); ++i)
    for (int j = 0; j < nb; ++j)
      if (i != (size_t)(short_len - ecl) || j >= short_blocks) all.push_back(blocks[j][i]);

  Grid g(ver * 4 + 17);
  draw_functions(g, ver, e);
  place_codewords(g, all);
  int best = mask;
  if (best < 0) {
    long lowest = -1;
    for (int m = 0; m < 8; ++m) {
      Grid t = g;
      apply_mask(t, m);
      draw_format(t, e, m);
      long p = penalty(t);
      if (lowest < 0 || p < lowest) { lowest = p; best = m; }
    }
  }
  apply_mask(g, best);
  draw_format(g, e, best);
  out.size = g.n;
  out.version = ver;
  out.mask = best;
  out.modules = std::move(g.mod);
  return out;
}
