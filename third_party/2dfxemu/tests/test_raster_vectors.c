/* SPDX-License-Identifier: MIT */
#include "test_helpers.h"
typedef struct{uint8_t profile,key,color,op,n,offset[2],clip[5],p[11],camera[10];uint32_t hash;}raster_vector;
#include "raster_vectors.h"
static void entry(twodfx_emu*d,unsigned slot,unsigned op,const uint8_t*p,unsigned n){uint8_t a[12];a[0]=slot;if(n)memcpy(a+1,p,n);send_command(d,op,a,n+1);}
static void setup(twodfx_emu*d){
 uint8_t data[8192],wrap[64],mesh[52];
 for(unsigned i=0;i<8192;i++)data[i]=(i*71+i/7)&255;
 uint8_t text[6]={0,1,2,127,128,3};memcpy(data+7000,text,6);
 for(unsigned i=0;i<64;i++)wrap[i]=(i*23)&255;
 memcpy(data,wrap+16,48);upload_bytes(d,0,data,8192,0);upload_bytes(d,0x7ffff0,wrap,64,0);
 const int16_t v[18]={-80,-60,-90,90,-50,50,0,90,160,-100,80,180,100,80,180,0,-100,100};
 for(unsigned i=0;i<18;i++){mesh[2*i]=(uint16_t)v[i];mesh[2*i+1]=(uint16_t)v[i]>>8;}
 uint8_t faces[16]={0,1,2,243,0,2,3,245,1,4,2,247,0,5,1,249};memcpy(mesh+36,faces,16);upload_bytes(d,10000,mesh,52,0);
 const uint8_t bitmap[]={5,0,8,0,0,161,31},pattern[]={63,240,255,127},font[]={7,0,16,0,0},tiles[]={6,0,0,0},map[]={3,0,24,0,15,15,6},defmesh[]={9,16,39,0,5,0,3};
 send_command(d,0x5c,bitmap,7);send_command(d,0x5d,pattern,4);send_command(d,0x5b,font,5);send_command(d,0x5e,tiles,4);send_command(d,0x5f,map,7);send_command(d,0x86,defmesh,7);
}
int main(void){unsigned count=0;for(unsigned profile=0;profile<2;profile++){
 twodfx_emu*d=device(profile);setup(d);
 for(unsigned i=0;i<sizeof vectors/sizeof vectors[0];i++){
  const raster_vector*v=&vectors[i];if(v->profile!=profile)continue;
  send_command(d,0x40|v->key,NULL,0);send_command(d,0x88,v->camera,10);
  entry(d,0,0x61,&v->color,1);entry(d,1,0x6e,v->offset,2);entry(d,2,0x79,v->clip,5);entry(d,3,v->op,v->p,v->n);entry(d,4,0x7f,NULL,0);
  twodfx_emu_vsync(d);uint32_t actual=frame_hash(d);
  if(actual!=v->hash){fprintf(stderr,"Raster vector %u profile %u opcode %02x: %08x != %08x\n",i,profile,v->op,actual,v->hash);return 1;}count++;
 }twodfx_emu_destroy(d);
}printf("PASS: %u reference raster vectors\n",count);return 0;}
