/* SPDX-License-Identifier: MIT */
#include "twodfx_emu.h"
#include <stdint.h>
#include <stdio.h>

static int check_c2_active_commands(twodfx_emu *d, unsigned machine)
{
  /* The public API maps outline/fill commands to their documented wire IDs. */
  static const struct { unsigned slot; uint8_t opcode; } expect[] = {
    { 9u, 0x6bu }, { 11u, 0x6au }, { 61u, 0x68u },
    { 63u, 0x6bu }, { 65u, 0x69u }
  };
  unsigned k;
  for (k = 0; k < sizeof(expect)/sizeof(expect[0]); ++k) {
    twodfx_2dpt_entry e;
    if (!twodfx_emu_get_2dpt(d,0u,expect[k].slot,1,&e) ||
        e.opcode != expect[k].opcode) {
      fprintf(stderr,"C2h profile %u, BG slot %u: expected opcode %02Xh\n",
              machine, expect[k].slot, expect[k].opcode);
      return 0;
    }
  }
  return 1;
}

int main(void)
{
  unsigned machine, id;
  for (machine = 0; machine < 2; ++machine) {
    twodfx_emu *d = twodfx_emu_create((twodfx_machine)machine);
    if (!d) return 1;
    twodfx_emu_write_f8(d,0x50); twodfx_emu_write_f9(d,1);
    for (id = 0; id < 22; ++id) {
      const unsigned code = 0xC0u + id;
      if (machine == 0 && (code == 0xCFu || code == 0xD0u)) continue;
      twodfx_emu_write_f8(d, (uint8_t)code);
      twodfx_emu_vsync(d);
      twodfx_emu_vsync(d);
      twodfx_emu_vsync(d);
      if (!twodfx_emu_egg_active(d)) {
        fprintf(stderr,"Egg %02Xh did not activate on profile %u\n",code,machine);
        twodfx_emu_destroy(d);
        return 2;
      }
      if (code == 0xC2u && !check_c2_active_commands(d,machine)) {
        twodfx_emu_destroy(d);
        return 3;
      }
    }
    twodfx_emu_write_f8(d,0x8e);
    twodfx_emu_vsync(d);
    twodfx_emu_destroy(d);
  }
  puts("Portable 2dfx original-source eggs and SHOW_FW: PASS");
  return 0;
}
