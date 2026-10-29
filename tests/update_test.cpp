#include "../src/core/release.h"
#include "../src/core/version.h"
#include <cassert>
#include <cstdio>

int main() {
  assert(compare_versions("1.1.0", "1.0") == 1);
  assert(compare_versions("v1.1", "1.1.0") == 0);
  assert(compare_versions("1.10.0", "1.9.9") == 1);
  assert(compare_versions("1.2.0-beta", "1.2.0") == -1);
  assert(compare_versions("1.2.0", "1.2.0-beta") == 1);
  assert(compare_versions("2", "1.99.99") == 1);
  assert(compare_versions(CUE_VERSION, "0.9") == 1);
  assert(short_version("v1.2.0") == "1.2" && short_version("1.2.1") == "1.2.1");

  const int64_t day = 24 * 3600, now = 1800000000;
  assert(update_due(now, 0));
  assert(!update_due(now, now - 14 * day));
  assert(update_due(now, now - 15 * day));
  assert(update_due(now, now + 3 * day));  // clock went back: check again

  const char* ok = R"({"tag_name":"v1.2.0","draft":false,"prerelease":false,"body":"novidades\n- a\n- b",
    "assets":[{"name":"Cue-1.2.0-codigo.zip","browser_download_url":"https://github.com/x/y/releases/download/v1.2.0/c.zip","size":10},
              {"name":"Cue-Setup-1.2.0.exe","browser_download_url":"https://github.com/gabccin/cue/releases/download/v1.2.0/Cue-Setup-1.2.0.exe",
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
  puts("Updates: version order, 15-day schedule, release parsing, installer asset and digest passed");
}
