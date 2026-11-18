/* SPDX-License-Identifier: MIT */
#include "test_helpers.h"
static void tick(twodfx_emu*d,unsigned n){while(n--)twodfx_emu_vsync(d);}
int main(void){
 size_t size=twodfx_emu_snapshot_size();uint8_t*save=malloc(size);CHECK(save);
 for(unsigned p=0;p<2;p++){
  for(unsigned id=0;id<22;id++){
   if(!p&&(id==15||id==16))continue;
   twodfx_emu*a=device(p),*b=device(p);
   send_command(a,0xc0+id,NULL,0);send_command(b,0xc0+id,NULL,0);
   tick(a,31);tick(b,31);CHECK(frame_hash(a)==frame_hash(b));
   CHECK(twodfx_emu_save_snapshot(a,save,size));tick(a,7);tick(b,13);
   uint32_t later=frame_hash(a);CHECK(twodfx_emu_load_snapshot(a,save,size));tick(a,7);CHECK(frame_hash(a)==later);
   tick(a,6);CHECK(frame_hash(a)==frame_hash(b));
   twodfx_emu_destroy(a);twodfx_emu_destroy(b);
  }
  twodfx_emu*d=device(p);send_command(d,0xd4,NULL,0);tick(d,1);
  uint32_t stages[6];for(unsigned s=0;s<6;s++){stages[s]=frame_hash(d);tick(d,1000);}
  for(unsigned i=0;i<6;i++)for(unsigned j=i+1;j<6;j++)CHECK(stages[i]!=stages[j]);
  unsigned fills=0;for(unsigned slot=0;slot<256;slot++){twodfx_2dpt_entry e;CHECK(twodfx_emu_get_2dpt(d,0,slot,1,&e));fills+=e.opcode==0x74;}CHECK(fills==4);send_command(d,0xdf,NULL,0);CHECK(!twodfx_emu_egg_active(d));send_command(d,0xd6,NULL,0);CHECK(!twodfx_emu_egg_active(d));
  send_command(d,0xd5,NULL,0);tick(d,1);uint32_t first=frame_hash(d);tick(d,2400);CHECK(frame_hash(d)!=first);twodfx_emu_destroy(d);
 }
 free(save);puts("PASS: isolated instances, all demo snapshots, six D4 scenes, D5 progression, retired selectors");return 0;
}
