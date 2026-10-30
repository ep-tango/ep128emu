// EF08Q: original ZX48 BASIC token-key sequence, no Z80 PC/SP modification.
// Native ROM probe: trial-output/ef08q/natural_input_probe.log PASS.
#ifndef EP128EMU_GUI_BETADISK_TRDOS_KEYBOARD_HPP
#define EP128EMU_GUI_BETADISK_TRDOS_KEYBOARD_HPP
#include <cstdint>
#include <cstddef>
namespace Ep128EmuBetaTrial {
struct GuestKey {uint8_t first,second;};
// Host scan numbers as in ZX128VM's keyboardConvTable. The original Sinclair
// keyword tokens RANDOMIZE (T) and USR (CAPS+SYMBOL then L) are NOT ASCII.
static const GuestKey enterDOSKeys[] = {
  {20,255}, // T = RANDOMIZE in ZX48 K mode
  {7,15},   // CAPS SHIFT + SYMBOL SHIFT -> E (extended) cursor mode
  {52,255}, // L = USR in E mode (ROM displays token spacing naturally)
  {25,255}, // 1
  {28,255}, // 5
  {26,255}, // 6
  {25,255}, // 1
  {26,255}, // 6
  {62,255}  // ENTER
};
static const size_t enterDOSKeyCount=sizeof(enterDOSKeys)/sizeof(enterDOSKeys[0]);
}
#endif
