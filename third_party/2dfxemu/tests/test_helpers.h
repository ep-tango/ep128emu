#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H
#include "twodfx_emu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#c);exit(1);}}while(0)
static inline void send_command(twodfx_emu*d,unsigned op,const uint8_t*p,size_t n){
 twodfx_emu_write_f8(d,op);for(size_t i=0;i<n;i++)twodfx_emu_write_f9(d,p[i]);
}
static inline void upload_bytes(twodfx_emu*d,uint32_t a,const uint8_t*p,unsigned n,unsigned format){
 uint8_t h[5]={a,a>>8,a>>16,n,n>>8};send_command(d,0x80+format,h,5);
 for(unsigned i=0;i<n;i++)twodfx_emu_write_f9(d,p[i]);
}
static inline twodfx_emu*device(unsigned profile){twodfx_emu*d=twodfx_emu_create((twodfx_machine)profile);CHECK(d);uint8_t on=1;send_command(d,0x50,&on,1);return d;}
static inline uint8_t pixel(twodfx_emu*d,unsigned x,unsigned y){uint8_t p=0,c=0;twodfx_emu_overlay_raw_line(d,x,y,&p,&c,1);return p;}
static inline uint32_t frame_hash(twodfx_emu*d){uint8_t row[744],cover[744];uint32_t hash=2166136261u;unsigned w=twodfx_emu_framebuffer_width(d),h=twodfx_emu_framebuffer_height(d);
 for(unsigned y=0;y<h;y++){twodfx_emu_overlay_line(d,0,y,row,cover,w);for(unsigned x=0;x<w;x++)hash=(hash^row[x])*16777619u;}return hash;
}
#endif
