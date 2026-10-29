#include "scene_contrast.h"
#include <algorithm>
#include <cmath>
SceneGrid sample_scene_grid(const uint32_t* px,int w,int h){
 SceneGrid s;if(!px||w<=0||h<=0)return s;s.width=w;s.height=h;s.valid=true;
 for(int y=0;y<s.rows;++y)for(int x=0;x<s.cols;++x){
  int x0=x*w/s.cols,y0=y*h/s.rows,x1=std::min(w,std::max(x0+1,(x+1)*w/s.cols)),y1=std::min(h,std::max(y0+1,(y+1)*h/s.rows));
  unsigned r=0,g=0,b=0,n=0;for(int yy=y0;yy<y1;++yy)for(int xx=x0;xx<x1;++xx){auto p=px[yy*w+xx];r+=(p>>16)&255;g+=(p>>8)&255;b+=p&255;++n;}
  s.pixels[y*s.cols+x]={(uint8_t)(r/n),(uint8_t)(g/n),(uint8_t)(b/n)};
 }return s;
}
Rgb scene_background(const SceneGrid& s,double x,double y,int vw,int vh){
 if(!s.valid||vw<=0||vh<=0)return {};
 double factor=std::min((double)vw/s.width,(double)vh/s.height),w=s.width*factor,h=s.height*factor;
 x-=((double)vw-w)/2;y-=((double)vh-h)/2;
 if(x<0||y<0||x>=w||y>=h)return {}; // letterbox belongs to the viewport, not the image
 int col=std::clamp((int)(x/w*s.cols),0,s.cols-1),row=std::clamp((int)(y/h*s.rows),0,s.rows-1);
 return s.pixels[row*s.cols+col];
}
static double luminance(Rgb c){auto f=[](double v){v/=255;return v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4);};return .2126*f(c.r)+.7152*f(c.g)+.0722*f(c.b);}
double scene_contrast(Rgb a,Rgb b){double x=luminance(a),y=luminance(b);return (std::max(x,y)+.05)/(std::min(x,y)+.05);}
Rgb region_ink(const SceneGrid& s,const Rect& r,int vw,int vh,Rgb fallback,bool& dark){
 std::array<Rgb,9> bg{};int rr=0,gg=0,bb=0;
 for(int y=0;y<3;++y)for(int x=0;x<3;++x){auto c=scene_background(s,r.x+(x+.5)*r.w/3,r.y+(y+.5)*r.h/3,vw,vh);bg[y*3+x]=c;rr+=c.r;gg+=c.g;bb+=c.b;}
 Rgb hue=s.valid?Rgb{(uint8_t)(rr/9),(uint8_t)(gg/9),(uint8_t)(bb/9)}:fallback;
 int high=std::max({(int)hue.r,(int)hue.g,(int)hue.b,1});hue={(uint8_t)(hue.r*255/high),(uint8_t)(hue.g*255/high),(uint8_t)(hue.b*255/high)};
 Rgb light=mix({250,250,250},hue,.08),deep=mix({6,6,6},hue,.11);
 std::array<double,9> lum{};for(size_t i=0;i<bg.size();++i)lum[i]=luminance(bg[i]);
 auto score=[&](Rgb ink){double value=luminance(ink),result=100;for(auto l:lum)result=std::min(result,(std::max(value,l)+.05)/(std::min(value,l)+.05));return result;};
 for(int i=0;i<5;++i){if(score(light)<4.5)light=mix(light,{255,255,255},.5);if(score(deep)<4.5)deep=mix(deep,{0,0,0},.5);}
 double ls=score(light),ds=score(deep);
 double current=dark?ds:ls,other=dark?ls:ds;
 if(current<4.5&&other>current*1.12)dark=!dark; // switch only when it improves local contrast
 return dark?deep:light;
}
