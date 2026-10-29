#include "../src/ui/scene_contrast.h"
#include <cassert>
#include <cstdio>
#include <vector>
int main(){
 for(Rgb bg:{Rgb{240,73,212},Rgb{74,38,25},Rgb{247,132,5},Rgb{8,50,130},Rgb{15,130,70},Rgb{240,220,50},Rgb{128,128,128},Rgb{255,255,255},Rgb{0,0,0}}){
  std::vector<uint32_t> image(96*54,0xff000000u|(bg.r<<16)|(bg.g<<8)|bg.b);
  auto grid=sample_scene_grid(image.data(),96,54);bool dark=false;
  Rgb ink=region_ink(grid,{0,0,192,108},192,108,{},dark);assert(scene_contrast(ink,bg)>=4.5);
 }
 std::vector<uint32_t> image(96*54);for(int y=0;y<54;++y)for(int x=0;x<96;++x)image[y*96+x]=x<48?0xfff049d4:0xff4a2619;
 auto grid=sample_scene_grid(image.data(),96,54);bool a=false,b=false;
 auto left=region_ink(grid,{0,0,90,108},192,108,{},a),right=region_ink(grid,{102,0,90,108},192,108,{},b);
 assert(a&&!b);assert(scene_contrast(left,{240,73,212})>=4.5&&scene_contrast(right,{74,38,25})>=4.5);
 auto letterbox=scene_background(grid,0,0,192,192);assert(letterbox.r==0&&letterbox.g==0&&letterbox.b==0);
 assert(!sample_scene_grid(nullptr,0,0).valid);
 std::puts("Regional contrast: pink, brown, orange, blue, green, yellow, grey, white/black, mixed scene and letterbox mapping passed");
}
