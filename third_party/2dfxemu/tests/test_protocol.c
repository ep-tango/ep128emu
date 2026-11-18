/* SPDX-License-Identifier: MIT */
#include "test_helpers.h"
static uint8_t converted(unsigned v){
 unsigned l=((v>>7)&1)|((v>>2)&2)|((v>>3)&4)|((v<<2)&8);
 unsigned r=((v>>6)&1)|((v>>1)&2)|((v>>2)&4)|((v<<3)&8);return l|(r<<4);
}
static void conversions(twodfx_emu*d){
 uint8_t all[256];for(unsigned i=0;i<256;i++)all[i]=i;
 for(unsigned f=0;f<3;f++){
  upload_bytes(d,0x7fff80,all,256,f);
  for(unsigned i=0;i<256;i++){
   unsigned expected=f?converted(i):i;
   CHECK(twodfx_emu_convert_byte(i,f)==expected);
   CHECK(twodfx_emu_resource_byte(d,(0x7fff80+i)&0x7fffff)==expected);
  }
  for(unsigned i=0;i<256;i++){
   unsigned expected=f?converted(i):i;
   uint8_t stream[4]={0x40,i,1,254},header[6]={0,32,0,255,255,127};
   upload_bytes(d,8192,stream,4,0);send_command(d,0x83+f,header,6);
   CHECK(twodfx_emu_resource_job_status(d)==TWODFX_RESOURCE_JOB_BUSY);twodfx_emu_vsync(d);
   CHECK(twodfx_emu_resource_job_status(d)==TWODFX_RESOURCE_JOB_SUCCESS);
   CHECK(twodfx_emu_resource_byte(d,0x7fffff)==expected);
  }
 }
}
static void retained(twodfx_emu*d){
 send_command(d,0x8f,NULL,0);twodfx_2dpt_entry e;
 for(unsigned l=0;l<2;l++)for(unsigned i=0;i<256;i++){CHECK(twodfx_emu_get_2dpt(d,l,i,0,&e));CHECK(e.opcode==0x7f);}
 const uint8_t plot[]={2,0,120,60},nop[]={0},gap[]={1};send_command(d,0x60,nop,1);send_command(d,0x62,plot,4);twodfx_emu_vsync(d);CHECK(pixel(d,120,60)==0);
 send_command(d,0x60,gap,1);twodfx_emu_vsync(d);CHECK(pixel(d,120,60)==15);
 uint8_t fps=1;send_command(d,0x51,&fps,1);uint8_t p2[]={2,0,121,60};send_command(d,0x62,p2,4);twodfx_emu_vsync(d);CHECK(pixel(d,121,60)==15);
 p2[2]=122;send_command(d,0x62,p2,4);twodfx_emu_vsync(d);CHECK(pixel(d,122,60)==0);twodfx_emu_vsync(d);CHECK(pixel(d,122,60)==15);
 fps=255;send_command(d,0x51,&fps,1);CHECK(twodfx_emu_fps(d)==50);
 uint8_t mesh[]={2,0,0,0,255,255,255};send_command(d,0x86,mesh,7);twodfx_3d_mesh_descriptor m;CHECK(twodfx_emu_get_mesh(d,2,0,&m));CHECK(m.vertices==256&&m.triangles==512);
 uint8_t camera[]={255,255,0,128,255,127,255,1,2,3};send_command(d,0x88,camera,10);twodfx_3d_camera c;CHECK(twodfx_emu_get_camera(d,0,&c));CHECK(c.x==-1&&c.y==-32768&&c.z==32767&&c.roll==769&&c.yaw==770&&c.pitch==771);
}
static void pointer(twodfx_emu*d){
 send_command(d,0x8f,NULL,0);uint8_t image[144];memset(image,0x55,144);upload_bytes(d,0x7fffb8,image,144,0);
 uint8_t def[]={184,255,127,0,0},mode[]={15,0},pos[]={0,150,70};send_command(d,0x95,def,5);send_command(d,0x93,pos,3);send_command(d,0x92,mode,2);send_command(d,0x93,pos,3);
 CHECK(pixel(d,150,70)==0);twodfx_emu_vsync(d);CHECK(pixel(d,150,70)==5&&pixel(d,173,81)==5&&pixel(d,174,81)==0);
 memset(image,0x66,144);upload_bytes(d,0x7fffb8,image,144,0);twodfx_emu_vsync(d);CHECK(pixel(d,150,70)==5);
 def[3]=24;send_command(d,0x95,def,5);twodfx_emu_vsync(d);CHECK(pixel(d,150,70)==5);def[3]=0;send_command(d,0x95,def,5);twodfx_emu_vsync(d);CHECK(pixel(d,150,70)==6);
 uint8_t rel[]={255,254};send_command(d,0x94,rel,2);twodfx_emu_vsync(d);CHECK(pixel(d,149,68)==6&&pixel(d,173,81)==0);
 mode[0]=16;send_command(d,0x92,mode,2);twodfx_emu_vsync(d);CHECK(pixel(d,149,68)==6);
 mode[0]=0;send_command(d,0x92,mode,2);send_command(d,0x93,pos,3);mode[0]=15;send_command(d,0x92,mode,2);twodfx_emu_vsync(d);CHECK(pixel(d,149,68)==6);
 size_t n=twodfx_emu_snapshot_size();uint8_t*saved=malloc(n);CHECK(saved&&twodfx_emu_save_snapshot(d,saved,n));send_command(d,0x8f,NULL,0);CHECK(twodfx_emu_load_snapshot(d,saved,n));CHECK(pixel(d,149,68)==6);saved[8]=13;CHECK(!twodfx_emu_load_snapshot(d,saved,n));free(saved);
}
typedef struct { uint8_t bytes[2048]; unsigned n, bit_at, bit_mask; } zx_writer;
static void zx_bit(zx_writer *w,unsigned b){
 if(!w->bit_mask){w->bit_at=w->n;w->bytes[w->n++]=0;w->bit_mask=128;}
 if(b)w->bytes[w->bit_at]|=w->bit_mask;
 w->bit_mask>>=1;
}
static void zx_gamma(zx_writer *w,unsigned n){
 unsigned top=1;while(top<=n/2)top*=2;
 for(top>>=1;top;top>>=1){zx_bit(w,1);zx_bit(w,!!(n&top));}zx_bit(w,0);
}
static void zx_matches(twodfx_emu*d){
 for(unsigned large=0;large<2;++large){
  zx_writer w={{0},0,0,0};uint8_t expected[1024];unsigned n=0,literal=large?301:4,off=large?300:4,repeat=large?600:12;
  zx_gamma(&w,literal);
  for(unsigned i=0;i<literal;++i)w.bytes[w.n++]=expected[n++]=(i*73+18)&255;
  zx_bit(&w,1);
  if(large){w.bytes[w.n++]=213;w.bytes[w.n++]=250;}else w.bytes[w.n++]=248;
  zx_gamma(&w,repeat-1);
  for(unsigned i=0;i<repeat;++i){expected[n]=expected[n-off];++n;}
  zx_bit(&w,0);zx_gamma(&w,2);
  w.bytes[w.n++]=expected[n++]=250;w.bytes[w.n++]=expected[n++]=83;
  zx_bit(&w,0);zx_gamma(&w,7);
  for(unsigned i=0;i<7;++i){expected[n]=expected[n-off];++n;}
  zx_bit(&w,1);w.bytes[w.n++]=1;w.bytes[w.n++]=254;
  for(unsigned f=0;f<3;++f){
   upload_bytes(d,0x7ffffe,w.bytes,w.n,0);
   uint8_t args[]={254,255,127,0,32,0};send_command(d,0x83+f,args,6);twodfx_emu_vsync(d);
   CHECK(twodfx_emu_resource_job_status(d)==TWODFX_RESOURCE_JOB_SUCCESS);
   CHECK(twodfx_emu_resource_job_output_bytes(d)==n);
   for(unsigned i=0;i<n;++i)CHECK(twodfx_emu_resource_byte(d,8192+i)==(f?converted(expected[i]):expected[i]));
  }
 }
}
static void transactions_saved(twodfx_emu*d){
 send_command(d,0x8f,NULL,0);size_t n=twodfx_emu_snapshot_size();uint8_t*save=malloc(n);CHECK(save);
 uint8_t h[]={0,16,0,3,0};send_command(d,0x80,h,5);twodfx_emu_write_f9(d,123);
 CHECK(twodfx_emu_save_snapshot(d,save,n));twodfx_emu_write_f9(d,7);twodfx_emu_write_f9(d,8);
 CHECK(twodfx_emu_load_snapshot(d,save,n));twodfx_emu_write_f9(d,45);twodfx_emu_write_f9(d,67);
 CHECK(twodfx_emu_resource_byte(d,4096)==123&&twodfx_emu_resource_byte(d,4097)==45&&twodfx_emu_resource_byte(d,4098)==67);
 const uint8_t plot[]={0,0};send_command(d,0x62,plot,2);CHECK(twodfx_emu_save_snapshot(d,save,n));
 send_command(d,0x8f,NULL,0);CHECK(twodfx_emu_load_snapshot(d,save,n));twodfx_emu_write_f9(d,145);twodfx_emu_write_f9(d,65);twodfx_emu_vsync(d);CHECK(pixel(d,145,65)==15);
 uint32_t before=frame_hash(d);save[n-100]^=128;CHECK(!twodfx_emu_load_snapshot(d,save,n));CHECK(frame_hash(d)==before);
 CHECK(!twodfx_emu_load_snapshot(d,save,n-1));free(save);
}
int main(void){for(unsigned profile=0;profile<2;profile++){
 twodfx_emu*d=device(profile);twodfx_3d_projection_info p;CHECK(twodfx_emu_get_projection(d,0,&p));CHECK(p.fx==(profile?508:579));
 conversions(d);zx_matches(d);retained(d);pointer(d);transactions_saved(d);send_command(d,0x8f,NULL,0);
 uint8_t header[]={0,0,0,1,0};send_command(d,0x80,header,5);send_command(d,0x40,NULL,0);twodfx_emu_write_f9(d,77);CHECK(twodfx_emu_resource_byte(d,0)==77);
 send_command(d,0x80,header,2);send_command(d,0x8f,NULL,0);CHECK(twodfx_emu_resource_byte(d,0)==0);
 for(unsigned i=0;i<4;i++){uint8_t line[]={0,(uint8_t)(60+i)};send_command(d,0x54+i,line,2);twodfx_emu_hsync(d,60+i);}CHECK(twodfx_emu_irq_asserted(d));CHECK((twodfx_emu_read_f8(d)&30)==0);send_command(d,0x58,NULL,0);CHECK(!twodfx_emu_irq_asserted(d));
 send_command(d,0x8e,NULL,0);twodfx_emu_vsync(d);uint32_t page=frame_hash(d);send_command(d,0x8f,NULL,0);twodfx_emu_vsync(d);CHECK(frame_hash(d)==page);twodfx_emu_destroy(d);
 }puts("PASS: conversions, uploads, ZX1, parser, reset, slots, cadence, 3D packets, cursor, snapshots and IRQ");return 0;}
