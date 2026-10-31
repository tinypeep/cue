// Previews and the film's colour come from a helper process: Cue.exe --thumb-host.
// It runs its own engine at background priority (CPU, disk and memory), decodes on the CPU
// only (it never competes with the playback for the GPU), and if it ever fails, the player
// does not notice. The player talks to it through two pipes; at most one request in flight.
#pragma once
#include <windows.h>

#include <cstdint>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

int thumb_host_main();  // the helper's whole life (when started with --thumb-host)

// One message from the helper, posted to the UI thread (lParam owns it).
struct ThumbMsg {
  enum Type : uint32_t { Ready = 1, Failed = 2, Frame = 3, NoFrame = 4, Gone = 5 } type;
  uint32_t id = 0;
  uint32_t session = 0, process = 0;
  double t = 0;
  int w = 0, h = 0;
  std::vector<uint32_t> px;
};

class ThumbClient {
 public:
  ThumbClient() = default;
  ~ThumbClient();
  ThumbClient(const ThumbClient&) = delete;
  ThumbClient& operator=(const ThumbClient&) = delete;
  bool start(HWND notify, UINT msg);  // spawns the helper (once)
  bool running() const;
  uint32_t epoch() const { return epoch_; }
  void open(const std::string& utf8_path, int width, uint32_t session);
  void shot(uint32_t id, double t, int output_width = 0);
  void close_file();
  void shutdown();
  void kill();  // ends the helper at once, waits for nothing (the app is leaving)

 private:
  struct State;
  std::shared_ptr<State> state_;
  uint32_t epoch_ = 0;
  void send(const std::string& line, bool replace = false);
  static void read_loop(std::shared_ptr<State> state);
};
