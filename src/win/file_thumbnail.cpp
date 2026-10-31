#include "file_thumbnail.h"
#include "remote_server.h"
#include <thread>
namespace { std::atomic<bool> busy{false}; }
std::shared_ptr<FileThumbnailJob> request_file_thumbnail(std::string path) {
  if(path.empty() || busy.exchange(true)) return {};
  try {
    auto job=std::make_shared<FileThumbnailJob>();
    std::thread([job,path=std::move(path)] {
      SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
      try { job->pixels=file_thumbnail_pixels(path); } catch(...) {}
      job->done.store(true); busy=false;
    }).detach();
    return job;
  } catch(...) { busy=false; return {}; }
}
