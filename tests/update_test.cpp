#include "../src/core/release.h"
#include "../src/core/version.h"
#include <cassert>
#include "../src/core/update_temp.h"
#include <cstdio>

int main() {
  assert(update_temp_directory_name(L"Cue-update-0123456789abcdef0123456789abcdef"));
  for(auto bad:{L"Cue-update-",L"Cue-update-../",L"Other-update-0123456789abcdef0123456789abcdef",L"Cue-update-0123456789abcdef0123456789abcdeg"})assert(!update_temp_directory_name(bad));
  {
    // the installer's "MZ" is read even when the first HTTP read stops after one byte
    ExeHeader split;
    assert(!split.feed("M", 1) && !split.ok());
    assert(split.feed("Z", 1) && split.ok());
    assert(split.feed("xx", 2) && split.ok());  // later reads cannot change it
    ExeHeader wrong; assert(wrong.feed("Mz", 2) && !wrong.ok());
    ExeHeader one; assert(!one.feed("M", 1) && !one.ok());
  }
  assert(kUpdateEvery==3*24*3600);
  assert(!update_due(1000000+3*24*3600-1,1000000));
  assert(update_due(1000000+3*24*3600,1000000));
  assert(compare_versions("1.1.0", "1.0") == 1);
  assert(compare_versions("v1.1", "1.1.0") == 0);
  assert(compare_versions("1.10.0", "1.9.9") == 1);
  assert(compare_versions("1.2.0-beta", "1.2.0") == -1);
  assert(compare_versions("1.2.0", "1.2.0-beta") == 1);
  assert(compare_versions("2", "1.99.99") == 1);
  assert(compare_versions(CUE_VERSION, "0.9") == 1);
  assert(compare_versions("v1.4", "1.3.2") == 1 && compare_versions("1.4", "1.3.3") == 1);  // the 1.4 release reaches 1.3.x
  assert(short_version("v1.2.0") == "1.2" && short_version("1.2.1") == "1.2.1");

  const int64_t day = 24 * 3600, now = 1800000000;
  assert(update_due(now, 0));
  assert(!update_due(now, now - 2 * day));
  assert(update_due(now, now - 3 * day));
  assert(update_due(now, now + 3 * day));  // clock went back: check again

  const char* ok = R"({"tag_name":"v1.2.0","draft":false,"prerelease":false,"body":"novidades\n- a\n- b",
    "assets":[{"name":"Cue-1.2.0-codigo.zip","browser_download_url":"https://github.com/x/y/releases/download/v1.2.0/c.zip","size":10},
              {"name":"Cue-Setup-1.2.0.exe","browser_download_url":"https://github.com/tinypeep/cue/releases/download/v1.2.0/Cue-Setup-1.2.0.exe",
               "size":31457280,"digest":"sha256:0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef"}]})";
  ReleaseInfo r;
  assert(parse_release(ok, r));
  assert(r.version == "1.2.0" && r.asset_name == "Cue-Setup-1.2.0.exe" && r.size == 31457280);
  assert(r.sha256 == "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
  assert(r.notes.find("novidades") == 0);

  assert(!parse_release(R"({"tag_name":"v1.3.0","prerelease":true,"assets":[]})", r));
  assert(!parse_release(R"({"tag_name":"v1.3.0","assets":[{"name":"notes.txt","browser_download_url":"https://x"}]})", r));
  assert(!parse_release(R"({"tag_name":"v1.3.0","assets":[{"name":"Cue-Setup.exe","browser_download_url":"http://x"}]})", r));
  assert(!parse_release("not json", r));
  ReleaseInfo nd;
  assert(parse_release(R"({"tag_name":"1.4","assets":[{"name":"cue-setup.exe","browser_download_url":"https://x/y"}]})", nd));
  assert(nd.sha256.empty() && nd.version == "1.4");
  // where an update may come from: GitHub and its download hosts, nothing that only looks like them
  for (const char* host : {"github.com", "api.github.com", "GitHub.com", "objects.githubusercontent.com",
                           "release-assets.githubusercontent.com", "a.b.githubusercontent.com"})
    assert(update_host_allowed(host));
  for (const char* host : {"", "githubusercontent.com", "evilgithubusercontent.com", "github.com.evil.example",
                           "evil-github.com", "gist.github.com", "x.github.io", ".githubusercontent.com",
                           "a..githubusercontent.com", "github.com:443", "user@github.com", "objects.githubusercontent.com."})
    assert(!update_host_allowed(host));
  puts("Updates: allowed hosts passed");
  puts("Updates: version order, 3-day schedule, release parsing, installer asset and digest passed");
}
