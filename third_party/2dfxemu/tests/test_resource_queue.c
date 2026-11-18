/* SPDX-License-Identifier: MIT */
#include "test_helpers.h"

static void submit(twodfx_emu *d,unsigned format,uint32_t src,uint32_t dst) {
  uint8_t p[6]={src,src>>8,src>>16,dst,dst>>8,dst>>16};
  send_command(d,0x83+format,p,6);
}
static void seed(twodfx_emu *d) {
  uint8_t streams[64];
  for(unsigned i=0;i<16;i++) {
    streams[i*4]=0x40;streams[i*4+1]=(uint8_t)(i+33);
    streams[i*4+2]=1;streams[i*4+3]=254;
  }
  upload_bytes(d,0x1000,streams,sizeof streams,0);
}
static void queue_tests(twodfx_emu *d) {
  seed(d);
  for(unsigned round=0;round<1024;round++) {
    send_command(d,0x53,NULL,0);
    for(unsigned i=0;i<16;i++)submit(d,i%3,0x1000+4*i,0x2000+i);
    CHECK(twodfx_emu_resource_job_status(d)==TWODFX_RESOURCE_JOB_BUSY);
    CHECK(twodfx_emu_read_f8(d)&32); /* 16 accepted; full is not overflow. */
    submit(d,0,0x1000,0x3000);
    CHECK(!(twodfx_emu_read_f8(d)&32));
    twodfx_emu_hsync(d,0); /* one opportunity drains all jobs, even at line 0 */
    CHECK(twodfx_emu_resource_job_status(d)==TWODFX_RESOURCE_JOB_SUCCESS);
    CHECK(twodfx_emu_resource_byte(d,0x3000)==0);
    for(unsigned i=0;i<16;i++)
      CHECK(twodfx_emu_resource_byte(d,0x2000+i)==twodfx_emu_convert_byte(i+33,i%3));
    CHECK(!(twodfx_emu_read_f8(d)&32)); /* completion does not acknowledge */
  }
  send_command(d,0x53,NULL,0);CHECK(twodfx_emu_read_f8(d)&32);
  /* Later jobs can consume the earlier job's output, in strict FIFO order. */
  const uint8_t chain[]={0xa4,0x40,0x7a,1,254,1,254};
  upload_bytes(d,0x4000,chain,sizeof chain,0);
  submit(d,0,0x4000,0x4100);submit(d,0,0x4100,0x4200);
  twodfx_emu_hsync(d,1);CHECK(twodfx_emu_resource_byte(d,0x4200)==0x7a);
  /* Decoder failure does not strand the following valid request. */
  const uint8_t bad[]={0x40,0x55,0,0};upload_bytes(d,0x5000,bad,sizeof bad,0);
  submit(d,0,0x5000,0x5100);twodfx_emu_hsync(d,2);
  CHECK(twodfx_emu_resource_job_status(d)==TWODFX_RESOURCE_JOB_ERROR);
  submit(d,0,0x5000,0x5100);submit(d,0,0x1000,0x5200);
  twodfx_emu_hsync(d,3);CHECK(twodfx_emu_resource_byte(d,0x5200)==33);
  CHECK(twodfx_emu_resource_job_status(d)==TWODFX_RESOURCE_JOB_SUCCESS);
  /* The fallback services the whole queue without publishing a partial scene. */
  uint32_t before=frame_hash(d);for(unsigned i=0;i<16;i++)submit(d,0,0x1000+i*4,0x5300);
  twodfx_emu_vsync(d);CHECK(frame_hash(d)==before);
  CHECK(twodfx_emu_resource_byte(d,0x5300)==48);
}
static void snapshots_and_lifecycle(twodfx_emu *d) {
  send_command(d,0x8f,NULL,0);seed(d);
  for(unsigned i=0;i<16;i++)submit(d,i%3,0x1000+i*4,0x2000+i);
  submit(d,0,0x1000,0x3000);
  size_t n=twodfx_emu_snapshot_size();uint8_t *saved=malloc(n);CHECK(saved);
  CHECK(twodfx_emu_save_snapshot(d,saved,n));CHECK(saved[8]==15);
  send_command(d,0x8f,NULL,0);
  CHECK(twodfx_emu_resource_job_status(d)==TWODFX_RESOURCE_JOB_NONE);
  CHECK(twodfx_emu_read_f8(d)&32);
  twodfx_emu_hsync(d,0);CHECK(twodfx_emu_resource_byte(d,0x2000)==0);
  CHECK(twodfx_emu_load_snapshot(d,saved,n));CHECK(!(twodfx_emu_read_f8(d)&32));
  CHECK(twodfx_emu_resource_job_status(d)==TWODFX_RESOURCE_JOB_BUSY);
  twodfx_emu_hsync(d,0);
  for(unsigned i=0;i<16;i++)CHECK(twodfx_emu_resource_byte(d,0x2000+i)==twodfx_emu_convert_byte(i+33,i%3));
  /* Partial argument collection survives independently of accepted jobs. */
  submit(d,0,0x1000,0x6000);
  const uint8_t half[]={4,16,0};send_command(d,0x83,half,3);
  CHECK(twodfx_emu_save_snapshot(d,saved,n));send_command(d,0x8f,NULL,0);
  CHECK(twodfx_emu_load_snapshot(d,saved,n));
  twodfx_emu_write_f9(d,1);twodfx_emu_write_f9(d,0x60);twodfx_emu_write_f9(d,0);
  twodfx_emu_hsync(d,1);CHECK(twodfx_emu_resource_byte(d,0x6000)==33);
  CHECK(twodfx_emu_resource_byte(d,0x6001)==34);
  /* Engine OFF and 25fps do not stop resource work. */
  uint8_t mode=0;send_command(d,0x50,&mode,1);mode=1;send_command(d,0x51,&mode,1);
  submit(d,0,0x1000,0x6100);twodfx_emu_hsync(d,0);
  CHECK(twodfx_emu_resource_byte(d,0x6100)==33);
  /* Egg activation waits for accepted jobs and then clears their status. */
  submit(d,0,0x1000,0x6200);send_command(d,0xc2,NULL,0);twodfx_emu_vsync(d);
  CHECK(twodfx_emu_egg_start_pending(d));CHECK(twodfx_emu_resource_byte(d,0x6200)==33);
  twodfx_emu_vsync(d);CHECK(!twodfx_emu_egg_start_pending(d));
  CHECK(twodfx_emu_resource_job_status(d)==TWODFX_RESOURCE_JOB_NONE);
  /* Terminal entry cancels pending resource work; power cycle clears all. */
  seed(d);submit(d,0,0x1000,0x6300);send_command(d,0x8e,NULL,0);
  twodfx_emu_hsync(d,0);twodfx_emu_vsync(d);
  CHECK(twodfx_emu_resource_job_status(d)==TWODFX_RESOURCE_JOB_NONE);
  CHECK(twodfx_emu_resource_byte(d,0x6300)==0);
  twodfx_emu_power_cycle(d);CHECK(twodfx_emu_read_f8(d)==255);
  free(saved);
}
static void overflow_visual(twodfx_emu *d) {
  send_command(d,0x8f,NULL,0);seed(d);
  for(unsigned i=0;i<17;i++)submit(d,0,0x1000,0x2000);
  send_command(d,0x53,NULL,0);
  CHECK(twodfx_emu_read_f8(d)&32);
  CHECK(twodfx_emu_resource_job_status(d)==TWODFX_RESOURCE_JOB_BUSY);
  submit(d,0,0x1000,0x2000);CHECK(!(twodfx_emu_read_f8(d)&32));
  twodfx_emu_hsync(d,0);
  uint8_t on=1;send_command(d,0x52,&on,1);
  unsigned w=twodfx_emu_framebuffer_width(d),h=twodfx_emu_framebuffer_height(d);
  for(unsigned phase=0;phase<18;phase++) {
    uint8_t colour=255,cover=0;twodfx_emu_vsync(d);
    twodfx_emu_overlay_line(d,w/2,(h-170)/2+10,&colour,&cover,1);
    CHECK(colour==(phase&15));
  }
  send_command(d,0x53,NULL,0);twodfx_emu_vsync(d);
  uint8_t colour=255,cover=0;
  twodfx_emu_overlay_line(d,w/2,(h-170)/2+10,&colour,&cover,1);CHECK(colour==0);
}
int main(void) {
  for(unsigned profile=0;profile<2;profile++) {
    twodfx_emu *d=device(profile);queue_tests(d);overflow_visual(d);snapshots_and_lifecycle(d);twodfx_emu_destroy(d);
  }
  puts("PASS: resource FIFO, capacity, wrap, conversions, dependencies, errors, STATUS5, snapshots, reset, eggs and terminal entry");
  return 0;
}
