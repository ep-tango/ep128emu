/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Károly Vaczkó
 * Frame-latched pointer overlay. Custom images are cached, not live RRAM views.
 */
#include "2dfx_pointer_assets_24x12.h"
static void pointer_bounds(const twodfx_emu*d,int *l,int *t,int *r,int *b){
 int tvc=d->machine==TWODFX_EMU_TVC;
 *l=tvc?79:88;*t=tvc?41:18;*r=tvc?686:811;*b=tvc?296:303;
}
static void pointer_reset(twodfx_emu*d,int active){
 int l,t,r,b;pointer_bounds(d,&l,&t,&r,&b);
 memset(&d->pointer_shadow,0,sizeof d->pointer_shadow);
 d->pointer_shadow.x=l+(r-l+1)/2;d->pointer_shadow.y=t+(b-t+1)/2;
 memset(d->pointer_user_shadow,0,144);d->pointer_pending=0;
 if(active){d->pointer_active=d->pointer_shadow;memset(d->pointer_user_active,0,144);}
}
static void pointer_mode(twodfx_emu*d,uint8_t mode,uint8_t colors){
 if(mode>15)return;
 d->pointer_shadow.mode=mode;d->pointer_shadow.colors=colors;
 if(mode)d->pointer_shadow.initialized=1;
}
static void pointer_pos(twodfx_emu*d,int x,int y){
 int l,t,r,b;
 if(!d->pointer_shadow.mode||!d->pointer_shadow.initialized)return;
 pointer_bounds(d,&l,&t,&r,&b);
 d->pointer_shadow.x=x<l?l:x>r?r:x;d->pointer_shadow.y=y<t?t:y>b?b:y;
}
static void pointer_service(twodfx_emu*d){
 if(!d->pointer_pending||d->session==SESSION_UPLOAD||d->session==SESSION_UNZX||d->resource_job==TWODFX_RESOURCE_JOB_BUSY)return;
 for(unsigned i=0;i<144;i++)d->pointer_user_shadow[i]=d->ram[(d->pointer_address+i)&0x7fffff];
 d->pointer_shadow.hx=d->pointer_hx;d->pointer_shadow.hy=d->pointer_hy;
 d->pointer_shadow.valid=1;d->pointer_pending=0;
}
static int pointer_define(twodfx_emu*d,uint32_t a,uint8_t hx,uint8_t hy){
 if(hx>=24||hy>=12)return 0;
 d->pointer_address=a&0x7fffff;d->pointer_hx=hx;d->pointer_hy=hy;
 d->pointer_pending=1;pointer_service(d);return 1;
}
static void pointer_latch(twodfx_emu*d){
 pointer_service(d);d->pointer_active=d->pointer_shadow;
 memcpy(d->pointer_user_active,d->pointer_user_shadow,144);
}
static void pointer_draw(twodfx_emu*d,twodfx_raster*r){
 const portable_pointer*p=&d->pointer_active;
 int l,t,right,b,ox=d->machine==TWODFX_EMU_TVC?0:88,oy=d->machine==TWODFX_EMU_TVC?41:16;
 if(!p->mode||p->mode>15||(p->mode==15&&!p->valid))return;
 int user=p->mode==15,index=p->mode-1;
 int hx=user?p->hx:two_dfx_pointer_hotspots[index][0];
 int hy=user?p->hy:two_dfx_pointer_hotspots[index][1];
 pointer_bounds(d,&l,&t,&right,&b);
 twodfx_raster_clip(r,0,0,r->width-1,r->height-1);
 for(int sy=0;sy<12;sy++)for(int sx=0;sx<24;sx++){
   int x=p->x-hx+sx,y=p->y-hy+sy;uint8_t c;
   if(x<l||x>right||y<t||y>b)continue;
   if(user)c=(d->pointer_user_active[sy*12+sx/2]>>((sx&1)*4))&15;
   else{unsigned role=(two_dfx_pointer_bitmaps[index][sy*6+sx/4]>>(6-(sx&3)*2))&3;
     if(role==0||role==3)continue;
     c=role==1?p->colors>>4:p->colors&15;}
   if(c!=d->transparent)twodfx_raster_plot(r,x-ox,y-oy,c);
 }
}
