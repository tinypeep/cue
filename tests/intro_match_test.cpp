#include "../src/core/intro_match.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <random>

// Made-up music: notes of a few partials with their own envelope, chosen by a seed. Two episodes
// share the opening and the ending song (the same seed) and differ everywhere else.
static constexpr double kPi = 3.14159265358979323846;
static void music(std::vector<float>& out, double seconds, uint32_t seed) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<double> pitch(110, 880), len(0.12, 0.5), u(0, 1);
  const size_t n = (size_t)(seconds * intro::kRate);
  size_t at = out.size();
  out.resize(at + n, 0);
  double t = 0;
  while (t < seconds) {
    const double f = pitch(rng), d = len(rng), amp = 0.15 + 0.2 * u(rng);
    const size_t s0 = at + (size_t)(t * intro::kRate), s1 = std::min(at + n, s0 + (size_t)(d * 1.6 * intro::kRate));
    for (size_t i = s0; i < s1; ++i) {
      const double x = (double)(i - s0) / intro::kRate;
      const double env = std::exp(-x * 4) * std::min(1.0, x * 200);
      out[i] += (float)(amp * env * (std::sin(2 * kPi * f * x) + 0.5 * std::sin(4 * kPi * f * x) + 0.25 * std::sin(6 * kPi * f * x)));
    }
    t += d;
  }
}
static std::vector<int16_t> pcm(const std::vector<float>& x, double gain, double noise, uint32_t seed) {
  std::mt19937 rng(seed);
  std::normal_distribution<double> g(0, 1);
  std::vector<int16_t> out(x.size());
  for (size_t i = 0; i < x.size(); ++i)
    out[i] = (int16_t)std::clamp(std::lround((x[i] * gain + noise * g(rng)) * 12000), -32768L, 32767L);
  return out;
}

int main() {
  using namespace intro;
  // the two episodes' first minutes: a different cold open, then the same 89 s opening
  std::vector<float> a, b;
  music(a, 47.3, 1); music(a, 89, 100); music(a, kHeadSeconds - 47.3 - 89, 2);
  music(b, 112.6, 3); music(b, 89, 100); music(b, kHeadSeconds - 112.6 - 89, 4);
  auto pa = pcm(a, 1.0, 0.01, 5), pb = pcm(b, 0.8, 0.02, 6);  // another mix, other noise
  auto t0 = std::chrono::steady_clock::now();
  Print ha = make_print(pa.data(), pa.size(), 0);
  Print hb = make_print(pb.data(), pb.size(), 0);
  double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  std::printf("two prints of %.0f s: %.0f ms\n", kHeadSeconds, ms);
  assert(ha.code.size() > 10000);
  t0 = std::chrono::steady_clock::now();
  Match m = longest_shared(ha, hb);
  std::printf("compared: %.1f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
  std::printf("a %.2f-%.2f  b %.2f-%.2f found %d\n", m.a0, m.a1, m.b0, m.b1, (int)m.found); std::fflush(stdout);
  assert(m.found);
  assert(std::abs(m.a0 - 47.3) < 0.6 && std::abs(m.a1 - 136.3) < 0.8);
  assert(std::abs(m.b0 - 112.6) < 0.6 && std::abs(m.b1 - 201.6) < 0.8);

  // nothing shared: nothing found
  std::vector<float> c;
  music(c, kHeadSeconds, 7);
  auto pc = pcm(c, 1.0, 0.01, 8);
  Print hc = make_print(pc.data(), pc.size(), 0);
  assert(!longest_shared(ha, hc).found);
  // silence agrees with silence, and says nothing
  std::vector<int16_t> quiet(60 * kRate, 0);
  Print q1 = make_print(quiet.data(), quiet.size(), 0), q2 = make_print(quiet.data(), quiet.size(), 10);
  assert(!longest_shared(q1, q2).found);

  // whole episodes: the opening from the first minutes, the ending from the last ones, each in its
  // own film's time
  std::vector<float> ta, tb;
  music(ta, 30, 9); music(ta, 88, 200); music(ta, kTailSeconds - 118, 10);   // ending, then a preview
  music(tb, 41, 11); music(tb, 88, 200); music(tb, kTailSeconds - 129, 12);
  auto pta = pcm(ta, 1, 0.01, 13), ptb = pcm(tb, 1.1, 0.01, 14);
  EpisodePrints ea{1420, ha, make_print(pta.data(), pta.size(), tail_start(1420))};
  EpisodePrints eb{1435, hb, make_print(ptb.data(), ptb.size(), tail_start(1435))};
  std::vector<SkipPart> sa, sb;
  shared_parts(ea, eb, sa, sb);
  assert(sa.size() == 2 && sb.size() == 2);
  assert(sa[0].kind == SkipKind::Opening && sa[0].from_sound && std::abs(sa[0].start - 47.3) < 0.6);
  assert(sa[1].kind == SkipKind::Ending && std::abs(sa[1].start - (tail_start(1420) + 30)) < 0.6);
  assert(sb[1].kind == SkipKind::Ending && std::abs(sb[1].end - (tail_start(1435) + 129)) < 0.8);
  // a short film is not looked at
  EpisodePrints short_a = ea, short_b = eb;
  short_a.dur = short_b.dur = 300;
  shared_parts(short_a, short_b, sa, sb);
  assert(sa.empty() && sb.empty());
  assert(head_seconds(1420) == kHeadSeconds && std::abs(head_seconds(600) - 270) < 1e-9);
  assert(tail_start(600) == 360 && tail_start(1420) == 1420 - kTailSeconds);

  // the helper's report, there and back; anything cut short is refused
  std::string bytes = encode(ea);
  EpisodePrints back;
  assert(decode(bytes, back) && back.dur == 1420 && back.head.code == ha.code && back.tail.quiet == ea.tail.quiet);
  assert(!decode(bytes.substr(0, bytes.size() - 1), back));
  assert(!decode("CUEP", back) && !decode("", back));
  bytes[4] = 9;
  assert(!decode(bytes, back));
  // the parts kept between runs
  std::vector<SkipPart> kept = {{SkipKind::Opening, 47.3, 136.25, true}, {SkipKind::Ending, 1180, 1268, true}};
  std::string line = encode_parts(kept);
  assert(line == "O47300,136250 E1180000,1268000");
  std::vector<SkipPart> got;
  assert(decode_parts(line, got) && got == kept);
  assert(decode_parts("", got) && got.empty());
  assert(!decode_parts("X1,2", got) && !decode_parts("O5,3", got) && !decode_parts("O1", got));
  std::cout << "intro_match_test ok\n";
}
