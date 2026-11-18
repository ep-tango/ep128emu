/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Károly Vaczkó
 * Portable pixel predicates and strokes for the 1.1.4 drawing contract.
 */
static int imin(int a,int b){return a<b?a:b;}
static int imax(int a,int b){return a>b?a:b;}
static void gfx2d_fill_rect_raw(twodfx_raster*r,int x,int y,unsigned w,unsigned h,uint8_t c){
 if(w&&h)twodfx_raster_fill_rect(r,x,y,x+(int)w-1,y+(int)h-1,c);
}
static void gfx2d_draw_line_raw(twodfx_raster*r,int x,int y,int ex,int ey,unsigned n,uint8_t c){
 if(!n)n=1;
 if(y==ey){gfx2d_fill_rect_raw(r,imin(x,ex),y,abs(ex-x)+1,n,c);return;}
 if(x==ex){gfx2d_fill_rect_raw(r,x,imin(y,ey),n*2,abs(ey-y)+1,c);return;}
 int dx=abs(ex-x),dy=abs(ey-y),error=dx-dy,sx=x<ex?1:-1,sy=y<ey?1:-1;
 for(;;){
  if(n==1)twodfx_raster_plot(r,x,y,c);else gfx2d_fill_rect_raw(r,x,y,n*2,n,c);
  if(x==ex&&y==ey)break;
  int twice=2*error;if(twice>=-dy){error-=dy;x+=sx;}if(twice<=dx){error+=dx;y+=sy;}
 }
}
static void gfx2d_triangle_line_raw(twodfx_raster*r,int x,int y,int ex,int ey,unsigned n,uint8_t c){
 int steep=abs(ey-y)>abs(ex-x),a=steep?y:x,b=steep?ey:ex,u=steep?x:y,v=steep?ex:ey;
 if(a>b){int t=a;a=b;b=t;t=u;u=v;v=t;}if(!n)n=1;
 int length=b-a,delta=abs(v-u),sign=v>=u?1:-1;
 for(int k=0;k<=length;k++){
  int minor=u+(length?sign*(int)(((int64_t)k*delta+(length-1)/2)/length):0)-(int)(n/2);
  if(steep)gfx2d_fill_rect_raw(r,minor,a+k,n,1,c);else gfx2d_fill_rect_raw(r,a+k,minor,1,n,c);
 }
}
static void gfx2d_draw_rect_raw(twodfx_raster*r,int x,int y,unsigned w,unsigned h,unsigned n,uint8_t ink,uint8_t paper){
 if(!w||!h)return;
 if(!n)n=1;
 int sx=imin(n*2,w),sy=imin(n,h);
 for(int yy=imax(y,r->y0);yy<=imin(y+(int)h-1,r->y1);yy++)
  for(int xx=imax(x,r->x0);xx<=imin(x+(int)w-1,r->x1);xx++){
   int edge=xx-x<sx||x+(int)w-xx<=sx||yy-y<sy||y+(int)h-yy<=sy;
   if(edge||paper!=r->key)twodfx_raster_plot(r,xx,yy,edge?ink:paper);
  }
}
static int ellipse_pixel(int x,int y,int w,int h,int relaxed){
 if(x<0||y<0||x>=w||y>=h)return 0;
 if(w==1||h==1)return 1;
 int64_t a=w-1,b=h-1,dx=abs(2*x-(w-1)),dy=abs(2*y-(h-1));
 if(relaxed){if(dx)dx--;if(dy)dy--;}
 return dx*dx*b*b+dy*dy*a*a<=a*a*b*b;
}
static void ellipse_paint(twodfx_raster*r,int x,int y,int w,int h,uint8_t c){
 if(w<=0||h<=0)return;
 if(w==1||h==1){gfx2d_draw_line_raw(r,x,y,x+w-1,y+h-1,1,c);return;}
 for(int yy=imax(y,r->y0);yy<=imin(y+h-1,r->y1);yy++)
  for(int xx=imax(x,r->x0);xx<=imin(x+w-1,r->x1);xx++)
   if(ellipse_pixel(xx-x,yy-y,w,h,1))twodfx_raster_plot(r,xx,yy,c);
}
static void gfx2d_fill_ellipse_raw(twodfx_raster*r,int x,int y,unsigned w,unsigned h,uint8_t c){ellipse_paint(r,x,y,w,h,c);}
static void gfx2d_draw_ellipse_raw(twodfx_raster*r,int x,int y,int w,int h,unsigned n,uint8_t ink,uint8_t paper){
 if(!n)n=1;
 int sx=2*n,sy=n,iw=w-2*sx,ih=h-2*sy;
 if(paper!=r->key){ellipse_paint(r,x,y,w,h,ink);ellipse_paint(r,x+sx,y+sy,iw,ih,paper);return;}
 for(int yy=imax(y,r->y0);yy<=imin(y+h-1,r->y1);yy++)
  for(int xx=imax(x,r->x0);xx<=imin(x+w-1,r->x1);xx++)
   if(ellipse_pixel(xx-x,yy-y,w,h,1)&&!ellipse_pixel(xx-x-sx,yy-y-sy,iw,ih,1))twodfx_raster_plot(r,xx,yy,ink);
}
static int round_pixel(int x,int y,int w,int h,int rx,int ry){
 if(x<0||y<0||x>=w||y>=h)return 0;
 if(!rx||!ry|| (y>=ry&&y<h-ry))return 1;
 int cy=y<ry?y:2*ry-(h-1-y),cx=imin(x,w-1-x);
 if(cx>=rx)return 1;
 return ellipse_pixel(cx,cy,2*rx+1,2*ry+1,0);
}
static void gfx2d_fill_round_rect_raw(twodfx_raster*r,int x,int y,int w,int h,int rx,int ry,uint8_t c){
 if(w<=0||h<=0)return;
 rx=imin(rx,(w-1)/2);ry=imin(ry,(h-1)/2);
 for(int yy=imax(y,r->y0);yy<=imin(y+h-1,r->y1);yy++)
  for(int xx=imax(x,r->x0);xx<=imin(x+w-1,r->x1);xx++)
   if(round_pixel(xx-x,yy-y,w,h,rx,ry))twodfx_raster_plot(r,xx,yy,c);
}
static void gfx2d_draw_round_rect_raw(twodfx_raster*r,int x,int y,int w,int h,unsigned n,int rx,int ry,uint8_t ink,uint8_t paper){
 if(w<=0||h<=0)return;
 if(!n)n=1;
 if(!rx||!ry||w<=2||h<=2){gfx2d_draw_rect_raw(r,x,y,w,h,n,ink,paper);return;}
 int sx=n*2,sy=n,iw=w-2*sx,ih=h-2*sy;
 if(paper!=r->key){
  gfx2d_fill_round_rect_raw(r,x,y,w,h,rx,ry,ink);
  gfx2d_fill_round_rect_raw(r,x+sx,y+sy,iw,ih,imax(0,rx-sx),imax(0,ry-sy),paper);return;
 }
 rx=imin(rx,(w-1)/2);ry=imin(ry,(h-1)/2);
 int irx=imin(imax(0,rx-sx),imax(0,iw/2)),iry=imin(imax(0,ry-sy),imax(0,ih/2));
 for(int yy=imax(y,r->y0);yy<=imin(y+h-1,r->y1);yy++)
  for(int xx=imax(x,r->x0);xx<=imin(x+w-1,r->x1);xx++)
   if(round_pixel(xx-x,yy-y,w,h,rx,ry)&&!round_pixel(xx-x-sx,yy-y-sy,iw,ih,irx,iry))twodfx_raster_plot(r,xx,yy,ink);
}
/* One quarter of the 8-bit direction circle, rounded to scale 512. */
static const int16_t direction_quarter[65]={0,13,25,38,50,63,75,88,100,112,124,137,149,161,172,184,196,207,219,230,241,252,263,274,284,295,305,315,325,334,344,353,362,371,379,388,396,404,411,419,426,433,439,445,452,457,463,468,473,478,482,486,490,493,497,500,502,504,506,508,510,511,511,512,512};
static int direction_sin(unsigned a){
 a&=255;unsigned q=a>>6,n=a&63;return q==0?direction_quarter[n]:q==1?direction_quarter[64-n]:q==2?-direction_quarter[n]:-direction_quarter[64-n];
}
static int sector_pixel(int x,int y,int w,int h,unsigned a,unsigned b){
 int64_t vx=(2*x-w+1)*(int64_t)h,vy=(2*y-h+1)*(int64_t)w;
 int64_t first=direction_sin(a)*vy+direction_sin(a+64)*vx;
 int64_t last=-vx*direction_sin(b+64)-vy*direction_sin(b);
 return ((b-a)&255)<=128?(first>=0&&last>=0):(first>=0||last>=0);
}
static void gfx2d_draw_arc_raw(twodfx_raster*r,int x,int y,int w,int h,unsigned n,uint8_t c,unsigned a,unsigned b){
 if(a==b)return;
 if(!n)n=1;
 int sx=2*n,sy=n;
 for(int yy=imax(y,r->y0);yy<=imin(y+h-1,r->y1);yy++)
  for(int xx=imax(x,r->x0);xx<=imin(x+w-1,r->x1);xx++)
   if(ellipse_pixel(xx-x,yy-y,w,h,1)&&!ellipse_pixel(xx-x-sx,yy-y-sy,w-2*sx,h-2*sy,1)&&sector_pixel(xx-x,yy-y,w,h,a,b))twodfx_raster_plot(r,xx,yy,c);
}
static int round512(int n){return n>=0?(n+256)/512:-((-n+256)/512);}
static void gfx2d_draw_pie_raw(twodfx_raster*r,int x,int y,int w,int h,uint8_t c,unsigned a,unsigned b){
 if(w<=0||h<=0||a==b)return;
 unsigned sweep=(b-a)&255,fa=a,fb=b;if(sweep>1&&sweep<254){fa=(a-1)&255;fb=(b+1)&255;}
 for(int yy=imax(y,r->y0);yy<=imin(y+h-1,r->y1);yy++)
  for(int xx=imax(x,r->x0);xx<=imin(x+w-1,r->x1);xx++){
   int lx=xx-x,ly=yy-y,center=abs(2*lx-w+1)<=1&&abs(2*ly-h+1)<=1;
   if(ellipse_pixel(lx,ly,w,h,1)&&(center||sector_pixel(lx,ly,w,h,fa,fb)))twodfx_raster_plot(r,xx,yy,c);
  }
 for(unsigned k=0;k<2;k++){
  unsigned angle=k?b:a;
  int ex=(w-1+round512(direction_sin(angle)*(w-1)))/2;
  int ey=(h-1+round512(-direction_sin(angle+64)*(h-1)))/2;
  gfx2d_draw_line_raw(r,x+(w-1)/2,y+(h-1)/2,x+ex,y+ey,1,c);
 }
}
