#include "../src/core/remote_proto.h"
#include <cassert>
#include <iostream>
int main(){using namespace remote;
 std::vector<std::string> files={"Twin.Peaks.S01E01.mkv","Twin.Peaks.S01E02.mkv","RuPaul.S09E01.mkv","Twin.Peaks.S02E01.mkv"};
 assert(next_episode(files[0],files)==files[1]);
 assert(next_episode(files[1],files)==files[3]);
 assert(next_episode(files[3],files).empty());
 assert(!same_series(files[1],files[2]));
 assert(same_series("[Group] Twin.Peaks.S01E01.1080p.mkv","Twin Peaks - 1x02 - Title.mkv"));
 assert(same_series("Twin Peaks ep 1.mp4","Twin.Peaks.E02.mp4"));
 assert(!same_series("S01E01.mkv","S01E02.mkv")); // ambiguous identity requires manual choice
 assert(!same_series("Show.S01E01.mkv","Show Other.S01E02.mkv"));
 assert(next_episode("Show.S01E01.mkv",{"Show.S01E01.mkv","Other.S01E02.mkv"}).empty());
 assert(next_file("A.S01E09.mkv",{"A.S01E09.mkv","Z.S01E01.mkv"})=="Z.S01E01.mkv");
 State s;s.next="Other.mkv";assert(state_json(s).find("\"nextAuto\":false")!=std::string::npos);
 std::cout<<"Series identity: mixed shows, final episode, next season, separators, release group and manual fallback passed\n";
}
