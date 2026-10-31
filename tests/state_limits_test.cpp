#include "../src/core/store.h"
#include "../src/core/file_io.h"
#include "../src/core/json.h"
#include <cassert>
#include <climits>
#include "../src/core/release.h"
#include <iostream>
int main() {
  assert(compare_versions("999999999999999999999999999999999.1", "1.1.1") > 0);
  ReleaseInfo release;
  assert(!parse_release(R"({"tag_name":"1.2","assets":[{"name":"Cue-Setup.exe","browser_download_url":"https://example.com/setup.exe","size":1e300}]})", release));
  JVal j;
  assert(json_parse("1e300",j)); assert(j.integer()==INT_MAX); assert(j.integer<int64_t>()==INT64_MAX);
  assert(json_parse("-1e300",j)); assert(j.integer()==INT_MIN); assert(j.integer<int64_t>()==INT64_MIN);
  assert(json_parse("9223372036854775807",j)); assert(j.integer<int64_t>()==INT64_MAX);
  assert(json_parse("12.75",j)); assert(j.integer()==12);
  std::string many="[0"; for(int i=0;i<70000;++i) many+=",0"; many+="]";
  assert(!json_parse(many,j)); // bounded tree, even for compact input
  auto dir=std::filesystem::temp_directory_path()/"cue-state-limits-test";
  std::filesystem::create_directories(dir); auto path=dir/"state.json";
  { std::ofstream f(path); f << R"({"schema":5,"prefs":{"style":{"size":1e300},"ui_scale":-1e300},"session":{"window":{"w":1e300,"h":1e300},"update":{"checked":1e300}},"media":[{"path":"clip.mp4","sid":1e300,"pos":1e300,"dur":1e300}]})"; }
  Store state; assert(state.load(path)); assert(!state.win.has);
  assert(state.default_style.size==80 && state.ui_scale==-1);
  assert(state.recents[0].sid==INT64_MAX && state.recents[0].pos==1e9);
  state.volume=73; assert(state.save(path)); assert(state.save(path));
  { std::ofstream f(path); f << std::string(kMaxStateBytes+1,' '); }
  std::string text; assert(!read_state_file(path,text) && text.empty());
  Store recovered; assert(recovered.load(path) && recovered.volume==73 && recovered.load_status=="recovered");
  std::filesystem::remove_all(dir);
  std::cout << "state bounds: extreme integers, geometry, JSON node budget, oversized-file backup recovery passed\n";
}
