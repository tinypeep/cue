#include "../src/core/media_geometry.h"
#include "../src/core/video_window.h"
#include <cassert>
#include <cstdio>
#include <cmath>
#include <initializer_list>
int main() {
  MediaGeometry media;
  uint64_t stale=media.request();
  assert(media.accept(stale,{1920,1080,0}));
  auto before=video_window_size(media.shape().aspect(),1920,1040,96,1);
  // A new film always obtains BOTH dimensions, even when width (or the whole
  // tuple) equals the previous film and the scalar observers emit no change.
  for(VideoShape shape : {VideoShape{1920,800,0},{1920,800,0},{1080,1920,0},
       {1080,1080,0},{3840,2160,90},{1920,1080,0}}) {
    media.reset();
    assert(!media.shape().valid());
    auto request=media.request();
    assert(!media.accept(stale,{1920,1080,0}));
    assert(!media.accept(request,{0,0,0}));
    assert(media.accept(request,shape));
    assert(media.shape().width==shape.width && media.shape().height==shape.height);
    auto fitted=video_window_size(media.shape().aspect(),1920,1040,96,1);
    assert(std::abs((double)fitted.w/fitted.h-shape.aspect())<.005);
    if(shape.width==1920 && shape.height==800) assert(fitted.h<before.h);
    stale=request;
  }
  // A newer query for this same file supersedes an older pending result too.
  auto first=media.request(),second=media.request();
  assert(!media.accept(first,{800,1200,0}));
  assert(media.accept(second,{800,1200,0}));
  assert(std::abs(media.shape().aspect()-2./3)<.00001);
  assert(std::abs(VideoShape{1920,1080,270}.aspect()-9./16)<.00001);
  puts("Media geometry: complete per-file dimensions, unchanged width/height, repeated formats, portrait/square/rotation and stale reply rejection passed");
}
