// Windows FLTK FL_SHORTCUT events can arrive without a matching FL_KEYUP.
// Isolate each synthesized Alt action so its legacy host bit never latches.
#ifndef EP128EMU_LEGACY_ALT_SHORTCUT_STATE_HPP
#define EP128EMU_LEGACY_ALT_SHORTCUT_STATE_HPP
#include <stdint.h>

static inline uint32_t ep128emuSyntheticAltShortcutMask(int key)
{
  int n = -1;
  if (key == ',') n = 20;
  else if (key == '.') n = 21;
  else if (key >= 'a' && key <= 'w') {
    // Same 1-based action mapping as Ep128EmuGUI::handleFLTKEvent().
    static const unsigned char altMap[23] = {
       0, 24, 25, 26,  0, 28,  0,  0, 30,  0, 31, 32,
       0,  0, 33, 34, 35, 36, 37, 38, 39,  0,  0
    };
    n = int(altMap[key - 'a']) - 1;
  }
  return (n >= 20 && n <= 38 ? (uint32_t(1U) << (n - 8)) : 0U);
}

#endif
