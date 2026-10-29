#include "thumbhost.h"

#include <algorithm>
#include <cstring>
#include <cstdlib>

#include "mpv.h"
#include "platform.h"
#include "../core/preview_session.h"
#include "../core/preview_size.h"

namespace {

struct Header {
  uint32_t magic = 0x32565054;  // "TPV2": media generation travels with every reply
  uint32_t type = 0, id = 0;
  uint32_t session = 0;
  int32_t w = 0, h = 0;
  double t = 0;
};

bool write_all(HANDLE h, const void* p, size_t n) {
  const char* c = static_cast<const char*>(p);
  while (n) {
    DWORD done = 0;
    if (!WriteFile(h, c, (DWORD)std::min<size_t>(n, 1 << 20), &done, nullptr) || !done) return false;
    c += done;
    n -= done;
  }
  return true;
}

bool read_all(HANDLE h, void* p, size_t n) {
  char* c = static_cast<char*>(p);
  while (n) {
    DWORD done = 0;
    if (!ReadFile(h, c, (DWORD)std::min<size_t>(n, 1 << 20), &done, nullptr) || !done) return false;
    c += done;
    n -= done;
  }
  return true;
}

void reply(HANDLE out, uint32_t type, uint32_t id, double t, int w = 0, int h = 0, const uint32_t* px = nullptr,
           uint32_t session = 0) {
  Header hd;
  hd.type = type; hd.id = id; hd.t = t; hd.w = w; hd.h = h;
  hd.session = session;
  write_all(out, &hd, sizeof hd);
  if (px && w > 0 && h > 0) write_all(out, px, (size_t)w * h * 4);
}

// waits for one of the wanted events (others are dropped); false on timeout or end
bool wait_for(Mpv& m, bool loading, double timeout) {
  PreviewWait wait(loading);
  DWORD start = GetTickCount();
  while (GetTickCount() - start < (DWORD)(timeout * 1000)) {
    mpv_event* e = m.api().wait_event(m.handle(), 0.25);
    if (!e) continue;
    auto event = PreviewWait::Event::Other;
    if (e->event_id == MPV_EVENT_FILE_LOADED) event = PreviewWait::Event::Loaded;
    else if (e->event_id == MPV_EVENT_PLAYBACK_RESTART) event = PreviewWait::Event::Restart;
    else if (e->event_id == MPV_EVENT_SHUTDOWN) event = PreviewWait::Event::Shutdown;
    else if (e->event_id == MPV_EVENT_END_FILE) {
      auto* end = static_cast<mpv_event_end_file*>(e->data);
      event = end && end->reason == MPV_END_FILE_REASON_STOP ? PreviewWait::Event::Stop : PreviewWait::Event::End;
    }
    auto result = wait.consume(event);
    if (result != PreviewWait::Result::Pending) return result == PreviewWait::Result::Ready;
  }
  return false;
}

}  // namespace

// ── helper side ─────────────────────────────────────────────────────
int thumb_host_main() {
  HANDLE in = GetStdHandle(STD_INPUT_HANDLE), out = GetStdHandle(STD_OUTPUT_HANDLE);
  // background mode: lowest CPU scheduling, low I/O and memory priority. Playback always wins.
  SetPriorityClass(GetCurrentProcess(), PROCESS_MODE_BACKGROUND_BEGIN);
  Mpv m;
  std::string err;
  if (!m.load(exe_dir(), &err) || !m.create(nullptr, 0, 0, &err)) { reply(out, ThumbMsg::Gone, 0, 0); return 1; }
  auto o = [&](const char* k, const char* v) { m.option(k, v); };
  o("config", "no"); o("terminal", "no"); o("idle", "yes"); o("pause", "yes"); o("keep-open", "always");
  o("vo", "null");
  o("audio", "no"); o("aid", "no"); o("sid", "no"); o("sub-auto", "no");
  o("hwdec", "no");  // the GPU belongs to the playback
  o("hr-seek", "no"); o("vd-lavc-threads", "1"); o("vd-lavc-skiploopfilter", "all"); o("vd-lavc-fast", "yes");
  o("sws-scaler", "fast-bilinear"); o("cache", "no"); o("demuxer-readahead-secs", "0");
  o("load-scripts", "no"); o("ytdl", "no"); o("osd-level", "0"); o("input-default-bindings", "no");
  o("msg-level", "all=no");
  if (m.api().initialize(m.handle()) < 0 || !m.api().command_ret) { reply(out, ThumbMsg::Gone, 0, 0); return 1; }
  std::string line;
  std::vector<uint32_t> px;
  bool loaded = false;
  uint32_t session = 0;
  while (true) {
    // one command per line
    line.clear();
    char ch;
    DWORD n;
    bool eof = false;
    while (true) {
      if (!ReadFile(in, &ch, 1, &n, nullptr) || n == 0) { eof = true; break; }
      if (ch == '\n') break;
      line += ch;
    }
    if (eof || line == "q") break;
    if (line.size() > 2 && line[0] == 'o') {
      // o <session> <width> <path> (the remainder is the full UTF-8 filename)
      size_t sp = line.find(' ', 2), next = sp == std::string::npos ? sp : line.find(' ', sp + 1);
      if (next == std::string::npos) continue;
      session = (uint32_t)std::strtoul(line.c_str() + 2, nullptr, 10);
      std::string vf = "scale=" + line.substr(sp + 1, next - sp - 1) + ":-2";
      std::string path = line.substr(next + 1);
      m.set("vf", vf);
      const char* args[] = {"loadfile", path.c_str(), "replace", nullptr};
      loaded = m.api().command(m.handle(), args) >= 0 && wait_for(m, true, 20);
      reply(out, loaded ? ThumbMsg::Ready : ThumbMsg::Failed, 0, 0, 0, 0, nullptr, session);
    } else if (line.size() > 2 && line[0] == 's') {
      // s <id> <time> <output width>; zero keeps the colour-sampling resolution
      uint32_t id = 0;
      double t = 0;
      int output_width = 0;
      if (sscanf(line.c_str() + 2, "%u %lf %d", &id, &t, &output_width) < 2 || !loaded) {
        reply(out, ThumbMsg::NoFrame, id, t, 0, 0, nullptr, session); continue;
      }
      while (true) {  // forget anything left from earlier requests
        mpv_event* e = m.api().wait_event(m.handle(), 0);
        if (!e || e->event_id == MPV_EVENT_NONE) break;
      }
      char ts[32];
      snprintf(ts, sizeof ts, "%.3f", std::max(0.0, t));
      const char* sk[] = {"seek", ts, "absolute+keyframes", nullptr};
      bool ok = m.api().command(m.handle(), sk) >= 0 && wait_for(m, false, 10);
      mpv_node res{};
      const char* sh[] = {"screenshot-raw", "video", nullptr};
      if (ok && m.api().command_ret(m.handle(), sh, &res) >= 0 && res.format == MPV_FORMAT_NODE_MAP) {
        int64_t w = 0, h = 0, stride = 0;
        const mpv_byte_array* ba = nullptr;
        for (int i = 0; i < res.u.list->num; ++i) {
          const char* k = res.u.list->keys[i];
          const mpv_node& v = res.u.list->values[i];
          if (!strcmp(k, "w") && v.format == MPV_FORMAT_INT64) w = v.u.int64;
          else if (!strcmp(k, "h") && v.format == MPV_FORMAT_INT64) h = v.u.int64;
          else if (!strcmp(k, "stride") && v.format == MPV_FORMAT_INT64) stride = v.u.int64;
          else if (!strcmp(k, "data") && v.format == MPV_FORMAT_BYTE_ARRAY) ba = (const mpv_byte_array*)v.u.ba;
        }
        if (w > 0 && h > 0 && w <= 2048 && h <= 2048 && ba && stride >= w * 4 && ba->size >= (size_t)(stride * h)) {
          const auto size = preview_size((int)w, (int)h, output_width);
          px.resize((size_t)size.w * size.h);
          for (int y = 0; y < size.h; ++y) {
            const int64_t sy = (int64_t)y * h / size.h;
            const uint32_t* src = (const uint32_t*)((const uint8_t*)ba->data + sy * stride);
            uint32_t* dst = px.data() + (size_t)y * size.w;
            for (int x = 0; x < size.w; ++x) dst[x] = src[(int64_t)x * w / size.w] | 0xFF000000u;
          }
          reply(out, ThumbMsg::Frame, id, t, size.w, size.h, px.data(), session);
        } else {
          reply(out, ThumbMsg::NoFrame, id, t, 0, 0, nullptr, session);
        }
        m.api().free_node_contents(&res);
      } else {
        reply(out, ThumbMsg::NoFrame, id, t, 0, 0, nullptr, session);
      }
    } else if (line == "x") {
      const char* st[] = {"stop", nullptr};
      m.api().command(m.handle(), st);
      loaded = false;
    }
  }
  m.destroy();
  return 0;
}

// ── player side ─────────────────────────────────────────────────────
bool ThumbClient::start(HWND notify, UINT msg) {
  if (running()) return true;
  if (proc_) shutdown();  // close the dead process/reader handles before restarting
  notify_ = notify;
  msg_ = msg;
  ++epoch_;
  session_.store(0);
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  HANDLE child_in = nullptr, child_out = nullptr;
  if (!CreatePipe(&child_in, &to_, &sa, 1 << 12)) return false;
  if (!CreatePipe(&from_, &child_out, &sa, 1 << 20)) { CloseHandle(child_in); CloseHandle(to_); to_ = nullptr; return false; }
  SetHandleInformation(to_, HANDLE_FLAG_INHERIT, 0);    // our ends stay ours
  SetHandleInformation(from_, HANDLE_FLAG_INHERIT, 0);
  wchar_t exe[MAX_PATH];
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --thumb-host";
  STARTUPINFOW si{sizeof(si)};
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = child_in;
  si.hStdOutput = child_out;
  si.hStdError = child_out;
  PROCESS_INFORMATION pi{};
  BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                           nullptr, &si, &pi);
  CloseHandle(child_in);
  CloseHandle(child_out);
  if (!ok) { CloseHandle(to_); CloseHandle(from_); to_ = from_ = nullptr; return false; }
  // the helper never outlives the player, even if the player crashes
  job_ = CreateJobObjectW(nullptr, nullptr);
  if (job_) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li{};
    li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &li, sizeof li);
    AssignProcessToJobObject(job_, pi.hProcess);
  }
  ResumeThread(pi.hThread);
  CloseHandle(pi.hThread);
  proc_ = pi.hProcess;
  reader_ = CreateThread(nullptr, 0, read_loop, this, 0, nullptr);
  return true;
}

DWORD WINAPI ThumbClient::read_loop(void* p) {
  auto* self = static_cast<ThumbClient*>(p);
  const uint32_t process = self->epoch_;
  while (true) {
    Header hd;
    if (!read_all(self->from_, &hd, sizeof hd) || hd.magic != 0x32565054 || hd.type == ThumbMsg::Gone) break;
    auto* msg = new ThumbMsg();
    msg->type = (ThumbMsg::Type)hd.type;
    msg->id = hd.id;
    msg->session = hd.session;
    msg->process = process;
    msg->t = hd.t;
    if (hd.type == ThumbMsg::Frame) {
      if (hd.w <= 0 || hd.h <= 0 || hd.w > 2048 || hd.h > 2048) { delete msg; break; }
      msg->w = hd.w;
      msg->h = hd.h;
      msg->px.resize((size_t)hd.w * hd.h);
      if (!read_all(self->from_, msg->px.data(), msg->px.size() * 4)) { delete msg; break; }
    }
    if (!PostMessageW(self->notify_, self->msg_, 1, (LPARAM)msg)) delete msg;
  }
  auto* gone = new ThumbMsg();
  gone->type = ThumbMsg::Gone;
  gone->process = process;
  gone->session = self->session_.load();
  if (!PostMessageW(self->notify_, self->msg_, 1, (LPARAM)gone)) delete gone;
  return 0;
}

void ThumbClient::send(const std::string& line) {
  if (!to_) return;
  std::string l = line + "\n";
  DWORD n = 0;
  WriteFile(to_, l.data(), (DWORD)l.size(), &n, nullptr);  // tiny: fits the pipe, never blocks
}

void ThumbClient::open(const std::string& path, int width, uint32_t session) {
  session_.store(session);
  send("o " + std::to_string(session) + " " + std::to_string(width) + " " + path);
}

void ThumbClient::shot(uint32_t id, double t, int output_width) {
  char b[64];
  snprintf(b, sizeof b, "s %u %.3f %d", id, t, output_width);
  send(b);
}

void ThumbClient::close_file() { send("x"); }

void ThumbClient::shutdown() {
  if (!proc_) return;
  send("q");
  if (to_) { CloseHandle(to_); to_ = nullptr; }
  if (WaitForSingleObject(proc_, 300) == WAIT_TIMEOUT) TerminateProcess(proc_, 0);
  if (reader_) { WaitForSingleObject(reader_, 500); CloseHandle(reader_); reader_ = nullptr; }
  if (from_) { CloseHandle(from_); from_ = nullptr; }
  CloseHandle(proc_);
  proc_ = nullptr;
  if (job_) { CloseHandle(job_); job_ = nullptr; }
}

void ThumbClient::kill() {
  if (!proc_) return;
  TerminateProcess(proc_, 0);
  if (to_) { CloseHandle(to_); to_ = nullptr; }
  // the reader thread ends by itself on the broken pipe; nobody waits for it
  if (reader_) { CloseHandle(reader_); reader_ = nullptr; }
  CloseHandle(proc_);
  proc_ = nullptr;
  if (job_) { CloseHandle(job_); job_ = nullptr; }
}
