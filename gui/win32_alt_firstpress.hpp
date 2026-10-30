#ifndef EP128EMU_WIN32_ALT_FIRSTPRESS_HPP
#define EP128EMU_WIN32_ALT_FIRSTPRESS_HPP

// Win32/FLTK may misdecode the FIRST physical Alt+key WM_SYSKEYDOWN as
// FL_Alt_L. Recover the actual native VK without changing the guest input
// stream. Letters and digits have stable Windows VK values, independently
// of the active layout. Punctuation is matched against the ACTIVE layout's
// VkKeyScanExW results, with Shift state checked to avoid false matches.
inline int ep128emuWin32RecoverFirstAltKey(
    int fltkKey, int leftAltFltkKey, unsigned int nativeMessage,
    unsigned int nativeVirtualKey, bool leftAltDown, bool rightAltDown,
    bool shiftDown, int commaScan, int periodScan)
{
  if (fltkKey != leftAltFltkKey || nativeMessage != 0x0104U ||
      !leftAltDown || rightAltDown)
    return fltkKey;
  // VkKeyScanExW returns VK in low byte; modifier flags in high byte.
  // Shift (bit 0) is permitted; characters requiring Ctrl/Alt are ignored.
  // Check layout punctuation BEFORE ASCII digits: on localized keyboards,
  // Shift+number may be comma/period instead of the number shortcut.
  const int scan[2] = { commaScan, periodScan };
  const int result[2] = { ',', '.' };
  for (int i = 0; i < 2; ++i) {
    const int v = scan[i];
    if (v < 0 || (v & 0x0600) != 0 ||
        bool((v & 0x0100) != 0) != shiftDown)
      continue;
    if (unsigned(v & 0xFF) == nativeVirtualKey)
      return result[i];
  }
  if (nativeVirtualKey >= unsigned('A') &&
      nativeVirtualKey <= unsigned('Z'))
    return int(nativeVirtualKey - unsigned('A') + unsigned('a'));
  if (nativeVirtualKey >= unsigned('0') &&
      nativeVirtualKey <= unsigned('9'))
    return int(nativeVirtualKey);
  return fltkKey;
}

#endif
