#include "app.h"
#include "../core/i18n.h"
#include "../core/util.h"
#include <algorithm>
#include <memory>

void App::queue_refresh() {
  next_advance_.clear(); KillTimer(hwnd_,T_NEXT);
  next_count_=-1; next_cancelled_=false;
  play_.next_notice("",now());
  play_.in.queue.clear();
  for(const auto& item:queue_.items())
    play_.in.queue.push_back({item.id,file_name(item.path),item.id==queue_.current(),remote::is_audio_name(item.path)});
  play_.in.queue_revision=queue_.revision();
  play_.in.queue_active=queue_.active();
  find_next_episode();
  play_.changed(now()); ensure_anim(); remote_publish();
}

size_t App::queue_add(const std::vector<std::string>& paths,bool replace) {
  if(paths.empty())return 0;
  if(replace)queue_.clear();
  std::vector<std::string> batch=paths;
  if(!queue_.active()&&!replace&&!path_.empty()&&mode_==Mode::Play) {
    queue_.append({path_}); queue_.select_path(path_);
    batch=MediaQueue::without_first(std::move(batch),path_);
  }
  const size_t added=queue_.append(batch);
  if(added<batch.size())say(tr(str::queue_limit));
  if((replace||mode_!=Mode::Play)&&!queue_.items().empty())queue_play(queue_.items().front().id);
  else queue_refresh();
  return added+(paths.size()-batch.size());  // the playing file counts as accepted
}

// A file that is gone (deleted, moved, a drive unplugged) is skipped at once with a word about
// it, and leaves the list: nothing waits on it.
void App::queue_play(uint32_t id) {
  for(int guard=0;guard<(int)MediaQueue::kLimit;++guard) {
    const auto* item=queue_.get(id); if(!item)return;
    const std::string path=item->path;
    if(path.find("://")!=std::string::npos||GetFileAttributesW(widen(path).c_str())!=INVALID_FILE_ATTRIBUTES) {
      queue_.select(id); open(path); queue_refresh(); return;
    }
    play_.toast(i18n::format(str::queue_skipped,{{"name",file_stem(path)}}),nullptr,now());
    log("queue: skipped a missing file");
    const auto& items=queue_.items();
    auto it=std::find_if(items.begin(),items.end(),[&](const auto& i){return i.id==id;});
    uint32_t following=0;
    if(it!=items.end()&&it+1!=items.end())following=(it+1)->id;
    else if(store_.loop_all&&items.size()>1&&items.front().id!=id)following=items.front().id;
    queue_.remove(id);
    queue_refresh();
    if(!following)return;
    id=following;
  }
}

DWORD WINAPI App::scan_folder(void* value) {
  std::unique_ptr<std::shared_ptr<FolderScan>> owner(static_cast<std::shared_ptr<FolderScan>*>(value));
  auto job=*owner;
  try {
    std::error_code ec;
    size_t visited=0;
    for(std::filesystem::directory_iterator it(widen(job->path),std::filesystem::directory_options::skip_permission_denied,ec),end;
        !ec&&it!=end&&!job->cancelled;it.increment(ec)) {
      if(++visited>4000) { job->limited=true; break; }
      // Skip links/junctions; a folder never expands recursively.
      if(GetFileAttributesW(it->path().c_str())&FILE_ATTRIBUTE_REPARSE_POINT)continue;
      if(it->is_regular_file(ec)&&remote::is_playable_name(narrow(it->path().filename().wstring()))) {
        if(job->files.size()>=MediaQueue::kLimit) {job->limited=true;break;}
        std::string file=narrow(it->path().wstring());
        // Handle/reparse validation can block on the filesystem; keep it off the UI.
        if(job->remote&&!remote_path_allowed(job->path,file))continue;
        job->files.push_back(std::move(file));
      }
    }
    if(ec)job->failed=true;
    std::sort(job->files.begin(),job->files.end(),[](const auto& a,const auto& b){return remote::natural_less(a,b);});
  } catch(...) { job->files.clear();job->failed=true; }
  job->done.store(true,std::memory_order_release);
  return 0;
}

void App::queue_folder(const std::string& path,bool replace,bool from_remote) {
  if(path.empty())return;
  if(folder_scan_) { say(tr(str::queue_reading));queue_notice(tr(str::queue_reading)); return; } // one bounded filesystem worker
  auto job=std::make_shared<FolderScan>();
  job->path=path;job->replace=replace;job->remote=from_remote;
  auto* context=new std::shared_ptr<FolderScan>(job);
  HANDLE worker=CreateThread(nullptr,0,scan_folder,context,CREATE_SUSPENDED,nullptr);
  if(!worker) {delete context;say(tr(str::notice_open_failed));queue_notice(tr(str::notice_open_failed),3);return;}
  SetThreadPriority(worker,THREAD_PRIORITY_BELOW_NORMAL);
  if(ResumeThread(worker)==(DWORD)-1) {
    // The thread never ran: terminate the suspended worker, release the job
    // it owned, and do not leave folder_scan_ waiting on a scan that cannot
    // finish (the timer would spin on a job that never becomes done).
    TerminateThread(worker,0);
    CloseHandle(worker);
    delete context;
    say(tr(str::notice_open_failed));queue_notice(tr(str::notice_open_failed),3);return;
  }
  folder_scan_=job; CloseHandle(worker);
  SetTimer(hwnd_,T_FOLDER,50,nullptr); say(tr(str::queue_reading));queue_notice(tr(str::queue_reading));
}

void App::finish_folder() {
  if(!folder_scan_||!folder_scan_->done.load(std::memory_order_acquire))return;
  auto job=std::move(folder_scan_);KillTimer(hwnd_,T_FOLDER);
  if(job->cancelled) {queue_notice("");return;}
  // Revalidate the shared folder once after enumeration. Child paths were
  // validated by the worker; the UI must not open hundreds of filesystem handles.
  if(job->remote&&!remote_path_ok(job->path,true)) {queue_notice(tr(str::notice_open_failed),3);return;}
  if(job->failed) {say(tr(str::notice_open_failed));queue_notice(tr(str::notice_open_failed),3);return;}
  if(job->files.empty()) {auto label=tr(job->limited?str::queue_limit:str::queue_no_media);say(label);queue_notice(label,job->limited?3:2);return;}
  size_t added=queue_add(job->files,job->replace);
  bool limited=job->limited||added<job->files.size();
  if(limited)say(tr(str::queue_limit));
  queue_notice(tr(limited?str::queue_limit:str::queue_added),added?1:3);
}

void App::queue_thumbnail_tick() {
  if(closing_app_||mode_!=Mode::Play||!play_.queue_open()) {
    KillTimer(hwnd_,T_QUEUE);queue_thumb_timer_=false;queue_thumb_job_.reset();queue_thumb_id_=0;return;
  }
  if(IsIconic(hwnd_))return;
  if(queue_thumb_job_&&queue_thumb_job_->done.load()) {
    play_.queue_thumbnail(queue_thumb_id_,queue_thumb_job_->pixels);
    queue_thumb_job_.reset();queue_thumb_id_=0;ensure_anim();
  }
  if(!queue_thumb_job_) {
    uint32_t id=play_.queue_thumbnail_request();
    if(const auto* item=queue_.get(id)) {
      queue_thumb_job_=request_file_thumbnail(item->path,remote::is_audio_name(item->path));
      if(queue_thumb_job_)queue_thumb_id_=id;
    }
  }
}

void App::queue_notice(const std::string& text,int result) {
  queue_notice_=text;queue_result_=result;++queue_notice_id_;remote_publish();
}
void App::queue_clear() {
  if(folder_scan_)folder_scan_->cancelled=true;
  if(queue_.active())queue_.clear();
  queue_notice_.clear();queue_result_=0;++queue_notice_id_;queue_refresh();
}
