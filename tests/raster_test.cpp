#include "../src/ui/gfx.h"
#include <cassert>
#include <cstdio>
#include <cmath>
int main(){
 const char* grid="### #" "## ##" "#####" "# # #";
 for(double scale:{1.,2.,2.5,3.,3.5,4.}) for(double a:{.1,.5,1.}) for(bool opaque:{false,true}) {
  Canvas fast,ref; fast.resize(40,40);ref.resize(40,40);
  if(opaque){fast.clear({30,50,80});ref.clear({30,50,80});}
  fast.cells(grid,5,4,-2,3,scale,{200,100,50},a);
  for(int y=0;y<4;++y)for(int x=0;x<5;++x)if(grid[y*5+x]=='#')
    ref.fill(-2+x*scale,3+y*scale,scale,scale,{200,100,50},a);
  assert(fast.px==ref.px);
 }
 // Src-over on a transparent destination is exactly the source, even with alpha.
 Canvas c;c.resize(1,1);c.fill_pixels(0,0,1,1,{200,100,50},.5);
 assert(c.px[0]==0x80643219u);
 std::puts("Raster: pixel spans match individual cells at integer/half scales, clipping and alpha passed");
}
