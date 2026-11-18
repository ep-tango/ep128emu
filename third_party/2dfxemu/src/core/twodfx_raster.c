/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Károly Vaczkó
 */
#include "twodfx_raster.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

int twodfx_raster_bind(twodfx_raster *r, uint8_t *colour, uint8_t *coverage,
                       unsigned width, unsigned height) {
  if (!r || !colour || !coverage || !width || !height ||
      width > 65535u || height > 65535u ||
      (size_t)width > SIZE_MAX / height) return 0;
  r->colour = colour; r->cover = coverage;
  r->width = width; r->height = height;
  r->x0 = r->y0 = 0;
  r->x1 = (int)width-1; r->y1 = (int)height-1;
  return 1;
}
void twodfx_raster_clear(twodfx_raster *r) {
  if (!r || !r->colour || !r->cover) return;
  memset(r->colour, 0, (size_t)r->width*r->height);
  memset(r->cover, 0, (size_t)r->width*r->height);
}
void twodfx_raster_clip(twodfx_raster *r, int x0, int y0, int x1, int y1) {
  if (!r) return;
  if (x0 < 0) x0=0;
  if (y0 < 0) y0=0;
  if (x1 >= (int)r->width) x1=(int)r->width-1;
  if (y1 >= (int)r->height) y1=(int)r->height-1;
  r->x0=x0; r->y0=y0; r->x1=x1; r->y1=y1;
}
static int inside(const twodfx_raster *r, int x, int y) {
  return r && r->colour && r->cover &&
         x >= r->x0 && x <= r->x1 && y >= r->y0 && y <= r->y1;
}
void twodfx_raster_plot(twodfx_raster *r, int x, int y, uint8_t colour) {
  if (inside(r,x,y)) {
    size_t p=(size_t)y*r->width+(unsigned)x;
    r->colour[p]=(uint8_t)(colour&15u);
    r->cover[p]=1;
  }
}
static void portable_rect(twodfx_raster*r,int x,int y,unsigned w,unsigned h,uint8_t c){
 if(w&&h)twodfx_raster_fill_rect(r,x,y,x+(int)w-1,y+(int)h-1,c);
}
static void portable_line(twodfx_raster*r,int x,int y,int ex,int ey,unsigned n,uint8_t c){
 if(!n)n=1;
 if(y==ey){portable_rect(r,(x<ex?x:ex),y,abs(ex-x)+1,n,c);return;}
 if(x==ex){portable_rect(r,x,(y<ey?y:ey),n*2,abs(ey-y)+1,c);return;}
 int dx=abs(ex-x),dy=abs(ey-y),error=dx-dy,sx=x<ex?1:-1,sy=y<ey?1:-1;
 for(;;){
  if(n==1)twodfx_raster_plot(r,x,y,c);else portable_rect(r,x,y,n*2,n,c);
  if(x==ex&&y==ey)break;
  int twice=2*error;if(twice>=-dy){error-=dy;x+=sx;}if(twice<=dx){error+=dx;y+=sy;}
 }
}
void twodfx_raster_line(twodfx_raster*r,int x,int y,int ex,int ey,uint8_t c){if(r)portable_line(r,x,y,ex,ey,1,c);}
void twodfx_raster_fill_rect(twodfx_raster *r,int x0,int y0,int x1,int y1,uint8_t colour) {
  int y,x,t;
  if (!r) return;
  if (x0>x1){t=x0;x0=x1;x1=t;}
  if (y0>y1){t=y0;y0=y1;y1=t;}
  if (x0<r->x0)x0=r->x0;
  if (x1>r->x1)x1=r->x1;
  if (y0<r->y0)y0=r->y0;
  if (y1>r->y1)y1=r->y1;
  for (y=y0;y<=y1;y++) for (x=x0;x<=x1;x++) twodfx_raster_plot(r,x,y,colour);
}
void twodfx_raster_rect(twodfx_raster *r,int x0,int y0,int x1,int y1,uint8_t colour) {
  twodfx_raster_line(r,x0,y0,x1,y0,colour);
  twodfx_raster_line(r,x1,y0,x1,y1,colour);
  twodfx_raster_line(r,x1,y1,x0,y1,colour);
  twodfx_raster_line(r,x0,y1,x0,y0,colour);
}
void twodfx_raster_triangle(twodfx_raster *r,int x0,int y0,int x1,int y1,
                            int x2,int y2,uint8_t colour) {
  twodfx_raster_line(r,x0,y0,x1,y1,colour);
  twodfx_raster_line(r,x1,y1,x2,y2,colour);
  twodfx_raster_line(r,x2,y2,x0,y0,colour);
}
static int64_t ceil_ratio(int64_t n,int64_t d){return n/d+(n%d>0);}
void twodfx_raster_fill_triangle_sample(twodfx_raster*r,int ax,int ay,int bx,int by,int cx,int cy,uint8_t color,int half){
 int xs[3]={ax,bx,cx},ys[3]={ay,by,cy};
 if(!r||(int64_t)(bx-ax)*(cy-ay)==(int64_t)(by-ay)*(cx-ax))return;
 int low=ay,high=ay;for(int i=1;i<3;i++){if(ys[i]<low)low=ys[i];if(ys[i]>high)high=ys[i];}
 if(low<r->y0)low=r->y0;
 if(high>r->y1+1)high=r->y1+1;
 for(int y=low;y<high;y++){
  int cuts[3],count=0;
  for(int i=0;i<3;i++){
   int j=(i+1)%3,x0=xs[i],y0=ys[i],x1=xs[j],y1=ys[j];
   if(y0>y1){int t=y0;y0=y1;y1=t;t=x0;x0=x1;x1=t;}
   if(y<y0||y>=y1)continue;
   int64_t den=2*(int64_t)(y1-y0);
   int64_t num=(2*(int64_t)x0-half)*(y1-y0)+(int64_t)(x1-x0)*(2*(int64_t)(y-y0)+half);
   cuts[count++]=(int)ceil_ratio(num,den);
  }
  if(count==2){int l=cuts[0]<cuts[1]?cuts[0]:cuts[1],rr=cuts[0]>cuts[1]?cuts[0]:cuts[1];
   if(rr>l)twodfx_raster_fill_rect(r,l,y,rr-1,y,color);}
 }
}
void twodfx_raster_fill_triangle(twodfx_raster*r,int ax,int ay,int bx,int by,int cx,int cy,uint8_t c){twodfx_raster_fill_triangle_sample(r,ax,ay,bx,by,cx,cy,c,1);}
size_t twodfx_raster_flood(twodfx_raster*r,int x,int y,uint8_t color){
 struct{int x,y;}pending[128];unsigned count=1;size_t changed=0;
 if(!inside(r,x,y))return 0;
 uint8_t old=r->colour[(size_t)y*r->width+x];color&=15;
 if(old==color)return 0;
 pending[0].x=x;pending[0].y=y;
 while(count){
  --count;x=pending[count].x;y=pending[count].y;
  if(r->colour[(size_t)y*r->width+x]!=old)continue;
  int left=x,right=x;
  while(left>r->x0&&r->colour[(size_t)y*r->width+left-1]==old)--left;
  while(right<r->x1&&r->colour[(size_t)y*r->width+right+1]==old)++right;
  twodfx_raster_fill_rect(r,left,y,right,y,color);changed+=right-left+1;
  for(int delta=-1;delta<=1;delta+=2){
   int row=y+delta;if(row<r->y0||row>r->y1)continue;
   int in_run=0;for(int xx=left;xx<=right;xx++){
    int same=r->colour[(size_t)row*r->width+xx]==old;
    if(same&&!in_run&&count<128){pending[count].x=xx;pending[count++].y=row;}
    in_run=same;
   }
  }
 }
 return changed;
}
int twodfx_raster_blit4(twodfx_raster *r,int x,int y,unsigned width,unsigned height,
                        const uint8_t *src,size_t src_bytes,size_t stride,uint8_t transparent) {
  unsigned row,col;
  size_t rowbytes=(size_t)(width/2u)+(width&1u);
  if(!r || !src || !width || !height || width>4096u || height>4096u || stride<rowbytes ||
     (size_t)(height-1)>SIZE_MAX/stride ||
     (size_t)(height-1)*stride>src_bytes ||
     rowbytes>src_bytes-(size_t)(height-1)*stride)return 0;
  for(row=0;row<height;row++) {
    int64_t dy=(int64_t)y+row;
    if (dy<r->y0 || dy>r->y1) continue;
    for(col=0;col<width;col++) {
      int64_t dx=(int64_t)x+col;
      uint8_t b,c;
      if (dx<r->x0 || dx>r->x1) continue;
      b=src[(size_t)row*stride+col/2u];
      c=(uint8_t)((col&1u)?(b>>4):(b&15u));
      if(c!=(transparent&15u)) twodfx_raster_plot(r,(int)dx,(int)dy,c);
    }
  }
  return 1;
}
