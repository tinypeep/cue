#include "../src/core/preview_commands.h"
#include <cassert>
#include <future>
#include <iostream>
int main() {
  PreviewCommands q;
  for (size_t i=0;i<q.kMaxItems;++i) assert(q.push("s 1 0 100"));
  assert(!q.push("s 2 0 100")); // stalled consumer cannot accumulate unbounded work
  assert(q.push("o 2 100 C:\\new.mp4", true)); // new file replaces stale backlog
  std::string s;
  assert(q.take(s) && s == "o 2 100 C:\\new.mp4");
  assert(!q.push(std::string(q.kMaxLine+1, 'a')));
  assert(!q.push("x\nq")); assert(!q.push(std::string("x\0q",3)));
  for(int i=0;i<4;++i) assert(q.push(std::string(q.kMaxLine,'a')));
  assert(!q.push("x")); // byte cap independent of count
  assert(q.push("x",true)); assert(q.take(s) && s=="x");
  auto reader = std::async(std::launch::async,[&] { std::string out; return q.take(out); });
  q.stop();
  assert(reader.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
  assert(!reader.get()); assert(!q.push("x",true));
  PreviewCommands stress;
  auto consumer = std::async(std::launch::async,[&] { std::string out; while(stress.take(out)) assert(out=="x"); });
  for (int i=0;i<100000;++i) assert(stress.push("x",true));
  stress.stop(); consumer.get();
  std::cout << "preview command bounds, cancellation and 100000 replacements passed\n";
}
