#include "../src/core/store.h"
#include <cassert>
#include <cstdio>
#include <chrono>
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
  Store store;assert(store.save(path));auto original=bytes(path);
  assert(store.save(path) && bytes(backup)==original); // seed the recovery copy once
  auto stamp=std::filesystem::file_time_type::clock::now()-std::chrono::hours(1);
  std::filesystem::last_write_time(path,stamp);std::filesystem::last_write_time(backup,stamp);
  for(int i=0;i<1000;++i)assert(store.save(path));
  assert(std::filesystem::last_write_time(path)==stamp && std::filesystem::last_write_time(backup)==stamp);
  store.volume=72;assert(store.save(path));assert(bytes(path)!=original && bytes(backup)==original);
  auto changed=bytes(path);assert(store.save(path) && bytes(backup)==changed);
  std::filesystem::last_write_time(path,stamp);std::filesystem::last_write_time(backup,stamp);
  for(int i=0;i<100;++i)assert(store.save(path));
  assert(bytes(path)==changed && bytes(backup)==changed);
  assert(std::filesystem::last_write_time(path)==stamp && std::filesystem::last_write_time(backup)==stamp);
  assert(!std::filesystem::exists(dir/"state.json.tmp"));
  assert(file_contents_equal(path,changed) && !file_contents_equal(path,changed+"x"));
  assert(!file_contents_equal(path,std::string(changed.size(),'x')));
  assert(!file_contents_equal(dir/"missing", ""));
  { std::ofstream f(dir/"empty"); } assert(file_contents_equal(dir/"empty",""));
  std::string large(12000,'x');{ std::ofstream f(dir/"large",std::ios::binary);f<<large; }
  assert(file_contents_equal(dir/"large",large));large[8000]='y';assert(!file_contents_equal(dir/"large",large));
  Store loaded;assert(loaded.load(path) && loaded.volume==72);
  std::filesystem::remove_all(dir);
  std::puts("Settings I/O: after recovery-copy warm-up, 1100 unchanged saves skip file writes/backup rotation; changed settings remain atomic, recoverable and loadable");
}
