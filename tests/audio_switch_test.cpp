#include "../src/core/audio_mode.h"
#include <cassert>
#include <cstdio>
int main() {
  auto graph = audio_mix_graph({1, 2}, false);
  auto mix = audio_switch_commands(graph, 0);
  assert(mix.size() == 1 && mix[0][2] == graph);
  auto single = audio_switch_commands("", 2);
  assert(single.size() == 3);
  assert(single[1] == (std::vector<std::string>{"set", "aid", "no"}));
  assert(single[2] == (std::vector<std::string>{"set", "aid", "2"}));
  // A seek here is discarded by the graph rebuild: the restart handler owns it.
  for (auto commands : {mix, single, audio_switch_commands(audio_waveform_graph(2), 2)})
    for (const auto& cmd : commands) assert(cmd[0] != "seek");
  std::puts("Audio graph commands never seek before the rebuild finishes");
}
