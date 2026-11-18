/* SPDX-License-Identifier: MIT */
#include "ep128emu_twodfx_adapter.hpp"
int main(void) {
  Ep128emuTwoDfxAdapter enterprise(TWODFX_EMU_ENTERPRISE);
  if (!enterprise.valid()) return 1;
  enterprise.writePort(0xf8,0x50);
  enterprise.writePort(0xf9,1);
  enterprise.videoVSync();
  enterprise.videoHSync(42);
  TwoDfxAdapter tvc(TWODFX_EMU_TVC);
  if (!tvc.valid()) return 2;
  tvc.writePort(0xf8,0x50);
  tvc.writePort(0xf9,1);
  tvc.videoVSync();
  return 0;
}
