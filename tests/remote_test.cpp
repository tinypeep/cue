// The remote control's portable half: QR codes, HTTP/WebSocket framing, pairing, files, JSON.
#include "../src/core/qr.h"
#include "../src/core/remote_proto.h"
#include "../src/core/remote_sound.h"
#include "../src/core/json.h"
#include "../src/core/store.h"
#include <filesystem>
#include <cassert>
#include <cstdio>
#include <cstring>

using namespace remote;

static void qr_checks() {
  // reference: Project Nayuki's encoder, same text, ECC M, automatic mask (version 3, mask 3)
  static const char* expected[] = {
    "#######.#..#.###...#..#######", "#.....#.#..###..#.#.#.#.....#", "#.###.#..#......#.##..#.###.#",
    "#.###.#.#...#.##.####.#.###.#", "#.###.#..#....#.#.###.#.###.#", "#.....#..#..##.##..##.#.....#",
    "#######.#.#.#.#.#.#.#.#######", "........#####..###.#.........", "#.##.###...#.#.#.#.##.#..#.##",
    "...#...##..#.###.###..#.#...#", ".#..#####....#..#.#.##....##.", "######..###.#...#.#......#..#",
    ".#.####.##.##.##.#####.#.##..", "##.#...#..#.#.#.##.#####...##", "...#..##.#####.###.#....#####",
    "##..#....##.#.#.#..#.#.#...#.", "##.#.#####....##..#....##..#.", "...##..#.#.#..#.#.........##.",
    "#.#.#.#####..###.##.#.#..##..", "...#.#.#.#######.##.#.##..#..", ".#.#.##.#....##..#.########..",
    "........#####....#.##...#####", "#######.###...#.#####.#.##.#.", "#.....#.#..#.##.#.###...##..#",
    "#.###.#..####..####.########.", "#.###.#.#..#.#.#.##.....#.#.#", "#.###.#.#.###.#.#.#.##......#",
    "#.....#...##...#..#.##.##..#.", "#######.#.##...##...#..###.#.",
  };
  QrCode q = qr_encode("http://192.168.0.12:7878/#c=4812", QrEcc::M);
  assert(q.version == 3 && q.mask == 3 && q.size == 29);
  for (int y = 0; y < q.size; ++y)
    for (int x = 0; x < q.size; ++x) assert(q.dark(x, y) == (expected[y][x] == '#'));
  assert(!q.dark(-1, 0) && !q.dark(0, 99));
  // capacity: version 10-M holds 213 bytes; one more does not fit
  assert(qr_encode(std::string(213, 'a')).version == 10);
  assert(qr_encode(std::string(214, 'a')).size == 0);
  assert(qr_encode("x", QrEcc::L).version == 1);
}

static void http_checks() {
  Request r;
  size_t hl = 0;
  std::string req = "GET /list?k=abc&p=C%3A%5CV%C3%ADdeos+novos HTTP/1.1\r\nHost: 192.168.0.12:7878\r\nUser-Agent: x\r\n\r\n";
  assert(parse_request(req.substr(0, 20), r, hl) == 0);
  assert(parse_request(req, r, hl) == 1 && hl == req.size());
  assert(r.method == "GET" && r.path == "/list");
  assert(r.query["k"] == "abc" && r.query["p"] == "C:\\Vídeos novos");
  assert(r.headers["host"] == "192.168.0.12:7878");
  assert(parse_request("garbage\r\n\r\n", r, hl) == -1);
  assert(parse_request(std::string(kMaxRequest + 10, 'a'), r, hl) == -1);
  assert(url_decode(url_encode("a b/ç?&=")) == "a b/ç?&=");
  std::string resp = http_response(200, "text/plain", "hi");
  assert(resp.find("Content-Length: 2\r\n") != std::string::npos && resp.substr(resp.size() - 2) == "hi");
}

static void ws_checks() {
  // RFC 6455's own example
  assert(ws_accept("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
  assert(hex(sha1("abc")) == "a9993e364706816aba3e25717850c26c9cd0d89d");
  assert(base64("") == "" && base64("f") == "Zg==" && base64("fo") == "Zm8=" && base64("foo") == "Zm9v");
  // a masked "Hello" from the RFC
  const unsigned char masked[] = {0x81, 0x85, 0x37, 0xfa, 0x21, 0x3d, 0x7f, 0x9f, 0x4d, 0x51, 0x58};
  Frame f;
  size_t used = 0;
  std::string buf((const char*)masked, sizeof masked);
  assert(ws_decode(buf.substr(0, 5), f, used) == 0);
  assert(ws_decode(buf, f, used) == 1 && used == sizeof masked && f.opcode == WS_TEXT && f.payload == "Hello");
  const unsigned char unmasked[] = {0x81, 0x05, 'H', 'e', 'l', 'l', 'o'};
  assert(ws_decode(std::string((const char*)unmasked, sizeof unmasked), f, used) == -1);
  std::string out = ws_encode(WS_TEXT, std::string(300, 'x'));
  assert((unsigned char)out[0] == 0x81 && (unsigned char)out[1] == 126 && out.size() == 304);
  assert(ws_encode(WS_PONG, "").size() == 2);
}

static void pairing_checks() {
  assert(private_ipv4(0xC0A8000C));   // 192.168.0.12
  assert(private_ipv4(0x0A000001));   // 10.0.0.1
  assert(private_ipv4(0xAC1F0001));   // 172.31.0.1
  assert(!private_ipv4(0xAC200001));  // 172.32.0.1
  assert(!private_ipv4(0x08080808));  // 8.8.8.8
  assert(ipv4_text(0xC0A8000C) == "192.168.0.12");
  assert(device_name("Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X)") == "iPhone");
  assert(device_name("Mozilla/5.0 (Linux; Android 14; Pixel 8)") == "Android");
  assert(device_name("Mozilla/5.0 (Windows NT 10.0)") == "");
  CodeGuard g;
  for (int i = 0; i < 5; ++i) { assert(g.allowed(i)); g.failed(i); }
  assert(g.allowed(5));
  g.failed(5);
  assert(!g.allowed(6) && !g.allowed(64) && g.allowed(66));
}

static void file_checks() {
  assert(is_video_name("a.MKV") && is_audio_name("b.flac") && !is_playable_name("c.srt") && !is_playable_name("d"));
  assert(natural_less("ep 2.mkv", "ep 10.mkv") && !natural_less("ep 10.mkv", "ep 2.mkv"));
  assert(natural_less("Abc", "abd") && natural_less("a01", "a1b"));
  int s = 0, e = 0;
  assert(episode_of("Show.S01E03.1080p.mkv", s, e) && s == 1 && e == 3);
  assert(episode_of("show s2 e10.mp4", s, e) && s == 2 && e == 10);
  assert(episode_of("Show 1x04.avi", s, e) && s == 1 && e == 4);
  assert(!episode_of("clip 1920x1080.mp4", s, e));
  assert(episode_of("Anime - Ep 12.mkv", s, e) && s == 0 && e == 12);
  assert(episode_of("serie capitulo 7.mp4", s, e) && e == 7);
  assert(!episode_of("the end 2.mp4", s, e));
  assert(!episode_of("movie.mkv", s, e));

  std::vector<std::string> folder = {"Show.S01E10.mkv", "Show.S01E02.mkv", "Show.S01E01.mkv", "Show.S01E01.srt",
                                     "Show.S02E01.mkv", "notes.txt", "Show.S01E03.mkv"};
  assert(next_episode("Show.S01E01.mkv", folder) == "Show.S01E02.mkv");
  assert(next_episode("Show.S01E03.mkv", folder) == "Show.S01E10.mkv");
  assert(next_episode("Show.S01E10.mkv", folder) == "Show.S02E01.mkv");
  assert(next_episode("Show.S02E01.mkv", folder).empty());
  std::vector<std::string> plain = {"aula 10.mp4", "aula 2.mp4", "aula 1.mp4", "leia.pdf"};
  assert(next_episode("aula 2.mp4", plain) == "aula 10.mp4");
  assert(next_episode("aula 10.mp4", plain).empty());
  assert(next_episode("solo.mp4", {"solo.mp4"}).empty());
  assert(looks_like_series(folder) && !looks_like_series(plain));

  assert(path_within("C:\\Users\\a\\Videos", "C:\\Users\\a\\Videos\\x\\y.mkv"));
  assert(path_within("C:\\Users\\a\\Videos", "c:/users/A/videos"));
  assert(!path_within("C:\\Users\\a\\Videos", "C:\\Users\\a\\VideosX\\y.mkv"));
  assert(!path_within("C:\\Users\\a\\Videos", "C:\\Users\\a\\Videos\\..\\secret.txt"));
  assert(!path_within("C:\\Users\\a\\Videos", "C:\\Users\\a"));
  assert(path_within("D:\\", "D:\\Filmes\\a.mkv"));
  assert(!path_within("", "C:\\x"));
}

static void json_checks() {
  State s;
  s.playing_view = true;
  s.file = "Fala \"aí\".mkv";
  s.pos = 12.5;
  s.dur = 100;
  s.next = "S01E04";
  JVal j;
  assert(json_parse(state_json(s), j));
  assert(j["t"].str() == "state" && j["view"].str() == "play" && j["file"].str() == "Fala \"aí\".mkv");
  assert(j["pos"].num() == 12.5 && j["sub"]["posDefault"].num() == 93 && j["next"].str() == "S01E04");
  Command c;
  assert(parse_command("{\"c\":\"seek\",\"v\":61.5}", c) && c.c == "seek" && c.has_v && c.v == 61.5);
  assert(parse_command("{\"c\":\"open\",\"path\":\"C:\\\\a.mkv\"}", c) && c.path == "C:\\a.mkv" && !c.has_v);
  assert(!parse_command("{\"v\":1}", c) && !parse_command("nope", c) && !parse_command("[1]", c));
  std::vector<Entry> es = {{"pasta", "C:\\p", true, false, false, 0, 0}, {"a.mkv", "C:\\p\\a.mkv", false, false, false, 30, 60}};
  assert(json_parse(listing_json("vídeos", "C:\\p", "", es), j));
  assert(j["items"].a.size() == 2 && j["items"].a[0]["dir"].boolean() && j["items"].a[1]["dur"].num() == 60);
}

static void store_checks() {
  auto file = std::filesystem::temp_directory_path() / "cue-remote-store.json";
  Store a;
  assert(!a.remote_on && a.remote_sound && !a.remote_folders_set && a.remote_tokens.empty());
  assert(a.news_seen.empty());  // before 1.1 nobody saw a "what's new": an update shows it once
  a.news_seen = "1.1";
  a.remote_on = true;
  a.remote_sound = false;
  a.remote_folders_set = true;
  a.remote_folders = {{"C:\\Users\\a\\Vídeos", true}, {"D:\\Filmes", false}};
  a.remote_tokens = {std::string(64, 'a'), std::string(64, 'b'), "short"};
  assert(a.save(file));
  Store b;
  assert(b.load(file));
  assert(b.remote_on && !b.remote_sound && b.remote_folders_set && b.remote_folders.size() == 2);
  assert(b.remote_folders[0].path == "C:\\Users\\a\\Vídeos" && b.remote_folders[0].on && !b.remote_folders[1].on);
  assert(b.remote_tokens.size() == 2);  // a malformed token is never trusted
  assert(b.news_seen == "1.1");
  std::filesystem::remove(file);
  std::filesystem::remove(file.string() + ".bak");
}

static void sound_checks() {
  auto loop = remote_sound::wav(remote_sound::Kind::Waiting);
  auto chime = remote_sound::wav(remote_sound::Kind::Connected);
  assert(loop.size() > 44 && std::memcmp(loop.data(), "RIFF", 4) == 0 && std::memcmp(loop.data() + 8, "WAVE", 4) == 0);
  assert(chime.size() > 44 && chime.size() < loop.size());
  // the loop is seamless: its first and last samples are both near silence-to-pad continuity
  auto sample = [&](const std::vector<char>& w, size_t i) {
    return (int)(short)((unsigned char)w[44 + i * 2] | (unsigned char)w[45 + i * 2] << 8);
  };
  size_t n = (loop.size() - 44) / 2;
  assert(std::abs(sample(loop, 0) - sample(loop, n - 1)) < 1200);
  int peak = 0;
  for (size_t i = 0; i < n; ++i) peak = std::max(peak, std::abs(sample(loop, i)));
  assert(peak > 500 && peak < 9000);  // audible, never loud
}

int main() {
  qr_checks();
  http_checks();
  ws_checks();
  pairing_checks();
  file_checks();
  json_checks();
  sound_checks();
  store_checks();
  std::puts("remote checks passed");
}
