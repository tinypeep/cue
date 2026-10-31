// The phone remote's server: a tiny HTTP + WebSocket server on the local network, on its own
// thread. It serves the remote's page, lists the folders the person chose to share, makes small
// thumbnails, and relays commands to the UI thread (WM_APP message + take_events). Nothing here
// touches the player: App decides what every command does.
//
// Who may connect: only private IPv4 peers (the same home network), with a token a phone got by
// typing the six-digit, short-lived, one-use pairing code (or scanning its QR code) while the pairing screen
// was open. A few wrong codes lock pairing for a minute. Hosts that are names, not addresses,
// are refused. Browser origins must match the actual local endpoint.
#pragma once
#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../core/remote_proto.h"

class RemoteServer {
 public:
  struct Event {
    enum Kind { Command, Connected, Disconnected, Paired } kind = Command;
    remote::Command cmd;
    std::string device;  // "iPhone", "Android", "" (Connected / Paired)
    std::string token;   // Paired: remember it
  };
  struct Root { std::string name, path; };  // a shared folder, as the phone shows it
  struct Progress { double pos = 0, dur = 0; bool seen = false; };

  RemoteServer();
  ~RemoteServer();
  // binds the first free port from remote::kPort and starts the thread; false (with a log line)
  // when the network refuses
  bool start(HWND notify, UINT msg, const std::wstring& font_path, std::string* err);
  void stop(bool tell_phones);  // tell_phones: they show "turned off on the computer"
  bool running() const;
  int port() const;
  int clients() const;  // phones connected right now

  // From the UI thread; each is copied under a lock and used by the server thread.
  void set_state(const std::string& json);      // sent to the phones when it changes
  void set_boot(const std::string& script);     // `window.CUE = {...};` (strings, language)
  void set_roots(std::vector<Root> roots);
  void set_progress(std::vector<std::pair<std::string, Progress>> items);  // saved positions
  void set_tokens(std::vector<std::string> tokens);
  void set_pairing(bool open, const std::string& code);  // the code is accepted only while open
  // the timeline frame a phone asked for while turning the wheel (0x00RRGGBB pixels)
  void set_peek(double at, const std::vector<uint32_t>& px, int w, int h);
  std::vector<Event> take_events();

 private:
  struct Impl;
  std::unique_ptr<Impl> d_;
};

// The PC's address on the local network ("192.168.0.12"), or "" when there is none.
std::string lan_address();
// A pairing code: kCodeDigits random digits.
std::string new_pairing_code();

std::vector<uint32_t> file_thumbnail_pixels(const std::string& path);
