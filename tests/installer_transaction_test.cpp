// g++ -std=c++20 tests/installer_transaction_test.cpp -o installer_transaction_test
#include "../installer/file_transaction.h"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iterator>
#include <string>
namespace fs = std::filesystem;
static void put(const fs::path& p, const std::string& value) {
  fs::create_directories(p.parent_path());
  std::ofstream out(p, std::ios::binary); out << value; out.close(); assert(out);
}
static std::string get(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
int main() {
  auto root = fs::temp_directory_path() / ("cue-transaction-test-" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directory(root);
  put(root / "Cue.exe", "old exe");
  put(root / "lib/engine.dll", "old engine");
  put(root / "obsolete.dll", "old obsolete");
  // Abandoning an incomplete extraction must not touch the old installation.
  {
    FileTransaction tx(root, root / "stage");
    put(tx.staged("Cue.exe"), "partial"); tx.add("Cue.exe");
  }
  assert(get(root / "Cue.exe") == "old exe");
  assert(!fs::exists(root / "stage"));
  // Fail after replacing a file, installing a new file and removing an obsolete file.
  {
    FileTransaction tx(root, root / "stage");
    put(tx.staged("Cue.exe"), "new exe"); tx.add("Cue.exe");
    put(tx.staged("new.dll"), "new dll"); tx.add("new.dll");
    tx.add("obsolete.dll", true);
    tx.add("lib/engine.dll");  // Missing staged file injects a mid-commit rename failure.
    std::error_code ec; assert(!tx.commit(ec)); assert(ec);
    assert(get(root / "Cue.exe") == "old exe");
    assert(get(root / "lib/engine.dll") == "old engine");
    assert(get(root / "obsolete.dll") == "old obsolete");
    assert(!fs::exists(root / "new.dll"));
  }
  // A successful commit includes generated installer metadata, not just the application.
  {
    FileTransaction tx(root, root / "stage");
    for (const auto* name : {"Cue.exe", "lib/engine.dll", "uninstall.exe", "install.txt"}) {
      put(tx.staged(name), std::string("new ") + name); tx.add(name);
    }
    tx.add("obsolete.dll", true);
    std::error_code ec; assert(tx.commit(ec)); assert(!ec);
  }
  assert(get(root / "Cue.exe") == "new Cue.exe");
  assert(get(root / "lib/engine.dll") == "new lib/engine.dll");
  assert(get(root / "uninstall.exe") == "new uninstall.exe");
  assert(get(root / "install.txt") == "new install.txt");
  assert(!fs::exists(root / "obsolete.dll"));
  assert(!fs::exists(root / "stage"));
  // A directory where a file should go fails before any rename.
  {
    FileTransaction tx(root, root / "stage");
    put(tx.staged("blocked.dll"), "new blocked"); tx.add("blocked.dll");
    fs::create_directories(root / "blocked.dll");
    std::error_code ec; assert(!tx.commit(ec)); assert(ec);
    assert(fs::is_directory(root / "blocked.dll"));
    assert(get(root / "Cue.exe") == "new Cue.exe");
  }
  // A regular file where a parent directory should go also fails up front.
  {
    FileTransaction tx(root, root / "stage");
    // lib is a directory, but lib/engine.dll is a file: use it as the parent.
    put(tx.staged("lib/engine.dll/inner.dll"), "new inner"); tx.add("lib/engine.dll/inner.dll");
    std::error_code ec; assert(!tx.commit(ec)); assert(ec);
    assert(get(root / "lib/engine.dll") == "new lib/engine.dll");
  }
  // Duplicate destinations and escaping paths are rejected with nothing changed.
  {
    FileTransaction tx(root, root / "stage");
    put(tx.staged("Cue.exe"), "dup"); tx.add("Cue.exe"); tx.add("Cue.exe");
    std::error_code ec; assert(!tx.commit(ec)); assert(ec);
    assert(get(root / "Cue.exe") == "new Cue.exe");
  }
  {
    FileTransaction tx(root, root / "stage");
    put(tx.staged("../evil.dll"), "evil"); tx.add("../evil.dll");
    std::error_code ec; assert(!tx.commit(ec)); assert(ec);
    assert(!fs::exists(root.parent_path() / "evil.dll"));
    assert(get(root / "Cue.exe") == "new Cue.exe");
  }
  fs::remove_all(root);
}
