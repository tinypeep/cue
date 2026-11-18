#include "../src/core/store.h"
#include <cassert>
#include <cstdio>
#include <chrono>
#include <limits>
#include "../src/core/file_io.h"
#include <fstream>
#include <iterator>

static std::string bytes(const std::filesystem::path& p) { std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}}; }
int main() {
  auto dir=std::filesystem::temp_directory_path()/"cue-store-io-regression";
  std::filesystem::remove_all(dir);std::filesystem::create_directories(dir);
  auto path=dir/"state.json";auto backup=dir/"state.json.bak";
  Store points;points.touch("clip.mp4",100,1);
  assert(points.set_position("clip.mp4",20,100));
  assert(!points.set_position("clip.mp4",20,100));
  assert(points.set_position("clip.mp4",20,101));
  assert(!points.set_position("missing.mp4",20,100));
  {
    // what the home says beyond a name: a song's artist, a book's chapters (and old files without them)
    auto p2=dir/"home.json";Store a;auto& it=a.touch("C:\\b\\Hobbit.m4b",20000,5);it.artist="J. R. R. Tolkien";it.chapters=19;it.chapter=3;
    a.touch("C:\\v\\film.mkv",100,6);assert(a.save(p2));
    Store b;assert(b.load(p2));auto* r=b.find("C:\\b\\Hobbit.m4b");assert(r&&r->artist=="J. R. R. Tolkien"&&r->chapters==19&&r->chapter==3);
    auto* f=b.find("C:\\v\\film.mkv");assert(f&&f->artist.empty()&&f->chapters==0&&f->chapter==-1);
    assert(bytes(p2).find("chaps")!=std::string::npos);
  }
  Store store;assert(store.save(path));auto original=bytes(path);
  assert(store.save(path) && bytes(backup)==original); // seed the recovery copy once
  auto stamp=std::filesystem::file_time_type::clock::now()-std::chrono::hours(1);
  std::filesystem::last_write_time(path,stamp);std::filesystem::last_write_time(backup,stamp);
  // Read back the stored time: some runtimes (e.g. MinGW's std::filesystem) do not return the
  // exact value that was set, so compare against what the filesystem reports. What matters is
  // that the unchanged saves leave it alone.
  auto stamp_path=std::filesystem::last_write_time(path),stamp_backup=std::filesystem::last_write_time(backup);
  for(int i=0;i<1000;++i)assert(store.save(path));
  assert(std::filesystem::last_write_time(path)==stamp_path && std::filesystem::last_write_time(backup)==stamp_backup);
  store.volume=72;assert(store.save(path));assert(bytes(path)!=original && bytes(backup)==original);
  auto changed=bytes(path);assert(store.save(path) && bytes(backup)==changed);
  std::filesystem::last_write_time(path,stamp);std::filesystem::last_write_time(backup,stamp);
  auto stamp_path2=std::filesystem::last_write_time(path),stamp_backup2=std::filesystem::last_write_time(backup);
  for(int i=0;i<100;++i)assert(store.save(path));
  assert(bytes(path)==changed && bytes(backup)==changed);
  assert(std::filesystem::last_write_time(path)==stamp_path2 && std::filesystem::last_write_time(backup)==stamp_backup2);
  assert(!std::filesystem::exists(dir/"state.json.tmp"));
  assert(file_contents_equal(path,changed) && !file_contents_equal(path,changed+"x"));
  assert(!file_contents_equal(path,std::string(changed.size(),'x')));
  assert(!file_contents_equal(dir/"missing", ""));
  { std::ofstream f(dir/"empty"); } assert(file_contents_equal(dir/"empty",""));
  std::string large(12000,'x');{ std::ofstream f(dir/"large",std::ios::binary);f<<large; }
  assert(file_contents_equal(dir/"large",large));large[8000]='y';assert(!file_contents_equal(dir/"large",large));
  Store loaded;assert(loaded.load(path) && loaded.volume==72);
  // Backup copy failure aborts before renaming over the good file: make the
  // backup path a non-empty directory so remove() + copy_file() both fail.
  store.volume=73;
  std::filesystem::remove_all(backup); std::filesystem::create_directories(backup/"x");
  { std::ofstream f(backup/"x"/"y"); f<<"z"; }
  auto before_backup_fail=bytes(path);
  assert(!store.save(path));  // must not rename without a backup
  assert(bytes(path)==before_backup_fail);  // good file untouched
  assert(!std::filesystem::exists(dir/"state.json.tmp"));  // failed save leaves no stale .tmp
  {
    // A non-finite engine sample never reaches the file (a bare nan/inf would make the next
    // start read the whole state as corrupt and reset every setting).
    auto guard = dir/"bad-state.json";
    Store bad;
    bad.touch("C:\\bad.mp4", std::numeric_limits<double>::infinity(), 9);
    assert(bad.find("C:\\bad.mp4") && bad.find("C:\\bad.mp4")->dur == 0);
    assert(bad.set_position("C:\\bad.mp4", 12, 100));
    assert(!bad.set_position("C:\\bad.mp4", std::numeric_limits<double>::quiet_NaN(), 100));
    assert(bad.find("C:\\bad.mp4")->pos == 12 && bad.find("C:\\bad.mp4")->dur == 100);
    assert(bad.save(guard));
    const std::string text = bytes(guard);
    assert(text.find("nan") == std::string::npos && text.find("inf") == std::string::npos);
    Store back;
    assert(back.load(guard) && back.find("C:\\bad.mp4") && back.find("C:\\bad.mp4")->pos == 12);
  }
  {
    // A second corrupt state is kept beside the first, and the only copy is never overwritten.
    auto cdir = dir/"corrupt";
    std::filesystem::create_directories(cdir);
    auto file = cdir/"state.json";
    { std::ofstream f(file); f << "not json"; }
    { std::ofstream f(cdir/"state.json.corrupt"); f << "first"; }
    Store corrupt;
    assert(!corrupt.load(file));
    assert(!std::filesystem::exists(file));
    assert(bytes(cdir/"state.json.corrupt") == "first");
    assert(bytes(cdir/"state.json.corrupt-1") == "not json");
  }
  std::filesystem::remove_all(dir);
  std::puts("Settings I/O: after recovery-copy warm-up, 1100 unchanged saves skip file writes/backup rotation; changed settings remain atomic, recoverable and loadable");
}
