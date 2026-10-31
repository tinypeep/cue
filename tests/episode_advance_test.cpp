#include "../src/core/episode_advance.h"
#include <cassert>
#include <iostream>
int main() {
  EpisodeAdvance next;
  std::string discovered="C:\\Series\\S01E02.mkv";
  assert(!next.return_home(true,true)); // idle observed before END_FILE: keep discovery until terminal event
  assert(next.arm(discovered,true));
  assert(!next.return_home(true,false)); // END_FILE queued advance before idle

  discovered.clear(); // discovery/idle state is mutable; the queued request owns its name
  assert(next.pending());
  assert(next.arm("C:\\wrong.mkv",true)); // duplicate EOF keeps the original destination
  auto target=next.take();
  assert(target=="C:\\Series\\S01E02.mkv" && !next.pending());
  assert(next.take().empty());
  assert(next.return_home(true,false)); // no pending advance and no file: ordinary Home
  assert(!next.return_home(false,false));
  assert(!next.arm("",true));
  assert(!next.arm(target,false)); // cancellation, loop and end timer don't schedule
  assert(next.arm(target,true));next.clear();assert(!next.pending());
  assert(next.arm("C:\\Series\\S01E03.mkv",true));
  assert(next.take()=="C:\\Series\\S01E03.mkv");
  std::cout << "Episode handoff: owned destination, duplicate EOF, one-shot timer and cancellation passed\n";
}
