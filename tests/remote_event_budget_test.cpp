#include "../src/core/remote_proto.h"
#include <cassert>
#include <iostream>
int main() {
  size_t pending=0, connected=0;
  for(int i=0;i<100000;++i) {
    if(remote::auth_event_room(pending) && connected<24) { ++pending; ++connected; }
    if(remote::event_room(pending,true)) ++pending;
    if(i%2 && connected) { assert(remote::event_room(pending,false)); ++pending; --connected; }
    assert(pending <= remote::kMaxEvents);
  }
  while(connected) { assert(remote::event_room(pending,false)); ++pending; --connected; }
  assert(!remote::auth_event_room(pending));
  assert(!remote::event_room(remote::kMaxEvents,false));
  assert(remote::auth_event_room(0));
  std::cout << "remote reconnect flood bounded with disconnect headroom\n";
}
