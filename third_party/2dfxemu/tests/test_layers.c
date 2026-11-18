/* SPDX-License-Identifier: MIT */
#include "test_helpers.h"
#include "../src/assets/2dfx_pointer_assets_24x12.h"
#include "../src/assets/2dfx_builtin_logo_small.h"
#include "../src/assets/2dfx_builtin_logo.h"

static void sprite(twodfx_emu *d, unsigned id, unsigned x, unsigned y,
                   unsigned w, unsigned h, unsigned flags, unsigned collision,
                   uint32_t address) {
  uint8_t p[9] = {(uint8_t)(128 | (collision ? 64 : 0) | ((x >> 8) << 4) |
      ((y >> 8) << 3) | ((w >> 8) << 1) | (h >> 8)),
      x, y, address, address >> 8, address >> 16, flags, w, h};
  send_command(d, id, p, 9);
}
static void sprites(twodfx_emu *d, unsigned profile) {
  const unsigned x0 = profile ? 0 : 88, y0 = profile ? 41 : 16;
  const unsigned width = twodfx_emu_framebuffer_width(d), height = twodfx_emu_framebuffer_height(d);
  uint8_t data[12];
  for (unsigned y = 0; y < 3; ++y) for (unsigned x = 0; x < 8; x += 2)
    data[y*4+x/2] = ((y*7+x)%16) | (((y*7+x+1)%16) << 4);
  for (unsigned flags = 0; flags < 16; ++flags) for (unsigned edge = 0; edge < 4; ++edge) {
    send_command(d, 0x8f, NULL, 0);
    upload_bytes(d, 0x7ffffa, data, sizeof data, 0);
    unsigned px = edge == 0 ? x0 + 15 : edge == 1 ? (x0 ? x0 - 3 : 0) : x0 + width - 4;
    unsigned py = edge == 0 ? y0 + 9 : edge == 3 ? y0 + height - 2 : y0 - 1;
    unsigned dx = !!(flags & 8), dy = !!(flags & 4);
    sprite(d, 0, px, py, 7, 3, flags << 4, 0, 0x7ffffa);
    twodfx_emu_vsync(d);
    for (unsigned y = 0; y < height; ++y) {
      uint8_t row[744], cover[744];
      twodfx_emu_overlay_line(d, 0, y, row, cover, width);
      for (unsigned x = 0; x < width; ++x) {
        unsigned expected = 0;
        if (x+x0 >= px && x+x0 < px+(7u<<dx) && y+y0 >= py && y+y0 < py+(3u<<dy)) {
          unsigned sx = (x+x0-px)>>dx, sy = (y+y0-py)>>dy;
          if (flags & 2) sx=6-sx;
          if (flags & 1) sy=2-sy;
          expected = (sy*7+sx)%16;
        }
        CHECK(row[x] == expected);
        CHECK(cover[x] == (expected != 0));
      }
    }
  }
}
static void collisions(twodfx_emu *d) {
  send_command(d, 0x8f, NULL, 0);
  const uint8_t source[] = {1, 0x20};
  upload_bytes(d, 4000, source, 2, 0);
  sprite(d, 0, 120, 60, 2, 1, 0, 1, 4000);
  sprite(d, 1, 120, 60, 2, 1, 0, 1, 4001);
  twodfx_emu_vsync(d);
  CHECK(!twodfx_emu_collision_detected(d));
  CHECK(pixel(d,120,60)==1 && pixel(d,121,60)==2);
  twodfx_emu_vsync(d);
  CHECK(twodfx_emu_collision_detected(d)); /* Different pixels, same two-pixel cell. */
  CHECK(!(twodfx_emu_read_f8(d)&128));
  sprite(d, 0, 120, 60, 1, 1, 0, 1, 4000);
  sprite(d, 1, 121, 60, 1, 1, 0, 1, 4000);
  twodfx_emu_vsync(d);
  CHECK(twodfx_emu_collision_detected(d));
  twodfx_emu_vsync(d);
  CHECK(!twodfx_emu_collision_detected(d)); /* Adjacent boxes do not overlap. */
  sprite(d, 1, 120, 60, 1, 1, 0, 0, 4000);
  twodfx_emu_vsync(d);twodfx_emu_vsync(d);
  CHECK(!twodfx_emu_collision_detected(d));
  uint8_t fps=1;send_command(d,0x51,&fps,1);
  sprite(d, 1, 120, 60, 1, 1, 0, 1, 4000);
  twodfx_emu_vsync(d);
  CHECK(!twodfx_emu_collision_detected(d));
  twodfx_emu_vsync(d); /* Publish collision even on a skipped scene frame. */
  CHECK(twodfx_emu_collision_detected(d));
}
static void overlays(twodfx_emu *d, unsigned profile) {
  send_command(d,0x8f,NULL,0);
  const uint8_t ink[]={0,0x90},fill[]={1,2,100,50,244,100},clip[]={0,0,0,0,0,0};
  send_command(d,0x61,ink,2);send_command(d,0x67,fill,6);send_command(d,0xf9,clip,6);
  const uint8_t pos[]={0,200,100};
  for (unsigned mode=1;mode<=14;++mode) {
    uint8_t select[]={mode,0x24};send_command(d,0x92,select,2);send_command(d,0x93,pos,3);
    twodfx_emu_vsync(d);
    unsigned hx=two_dfx_pointer_hotspots[mode-1][0],hy=two_dfx_pointer_hotspots[mode-1][1];
    for(unsigned y=0;y<12;++y)for(unsigned x=0;x<24;++x){
      unsigned role=(two_dfx_pointer_bitmaps[mode-1][y*6+x/4]>>(6-2*(x%4)))&3;
      CHECK(pixel(d,200-hx+x,100-hy+y)==(role==1?2:role==2?4:9));
    }
  }
  uint8_t hide[]={0,0};send_command(d,0x92,hide,2);
  for(unsigned small=0;small<2;++small){
    uint8_t show[]={small?128:0,150,70,0x24};send_command(d,0x90,show,4);twodfx_emu_vsync(d);
    unsigned w=small?103:206,h=small?40:79,stride=small?26:52;
    const uint8_t *art=small?two_dfx_builtin_logo_small_2bpp:two_dfx_builtin_logo_2bpp;
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
      unsigned role=(art[y*stride+x/4]>>(2*(x%4)))&3;
      CHECK(pixel(d,150+x,70+y)==(role==1?(profile?15:2):role==2?(profile?3:4):9));
    }
  }
  send_command(d,0x91,NULL,0);twodfx_emu_vsync(d);CHECK(pixel(d,200,100)==9);
  uint8_t mode=255;send_command(d,0x50,&mode,1);
  uint8_t c=99,k=99;twodfx_emu_overlay_raw_line(d,110,55,&c,&k,1);CHECK(c==9&&k==1);
  twodfx_emu_overlay_raw_line(d,400,200,&c,&k,1);CHECK(c==0&&k==1);
  mode=1;send_command(d,0x50,&mode,1);twodfx_emu_overlay_raw_line(d,400,200,&c,&k,1);CHECK(k==0);
  mode=0;send_command(d,0x50,&mode,1);twodfx_emu_overlay_raw_line(d,110,55,&c,&k,1);CHECK(k==0);
}
int main(void) {
  for(unsigned profile=0;profile<2;++profile){twodfx_emu *d=device(profile);
    sprites(d,profile);collisions(d);overlays(d,profile);twodfx_emu_destroy(d);
  }
  puts("PASS: sprite transforms, clipping, odd stride, wrap, collision cells/delay, 14 pointer shapes, both logos and coverage");
  return 0;
}
