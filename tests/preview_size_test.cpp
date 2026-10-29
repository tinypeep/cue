#include "../src/core/preview_size.h"
#include "../src/ui/play.h"
#include <cassert>
#include <cstdio>
int main() {
  const auto hover = preview_size(80,44,40);
  assert(hover.w==40 && hover.h==22);
  const auto colour = preview_size(80,44,0);
  assert(colour.w==80 && colour.h==44);
  auto portrait=preview_size(80,142,40);assert(portrait.w==40 && portrait.h==71);
  auto small=preview_size(16,9,40);assert(small.w==16 && small.h==9);
  auto invalid=preview_size(0,44,40);assert(invalid.w==0 && invalid.h==0);
  auto narrow=preview_size(2048,1,16);assert(narrow.w==16 && narrow.h==1);
  PlayView p;p.resize(1280,720,2);
  assert(p.in.ui_scale==2); // médio is the default; preview quality still follows the actual render scale.
  assert(p.thumb_width()==160 && p.thumb_decode_width()==80 && p.thumb_hover_width()==40);
  std::puts("Hover preview sizing: quarter pixel count, portrait aspect, source resolution for colour sampling, display size and small default passed");
}
