// Read-only NICK LPT decoder for the debugger; never modifies the guest.
// Corresponds to Nick::runOneSlot() slots 0..7 in src/nick.cpp.
#ifndef EP128EMU_NICK_LPT_VIEWER_HPP
#define EP128EMU_NICK_LPT_VIEWER_HPP
#include <cstdint>

namespace Ep128Emu {
  struct NickLPTEntry {
    uint16_t address;
    uint16_t lines;
    uint16_t ld1;
    uint16_t ld2;
    uint8_t modeByte;
    uint8_t videoMode;
    uint8_t colorMode;
    uint8_t leftMargin;
    uint8_t rightMargin;
    uint8_t palette[8];
    bool irq, vres, reload, lsbAlt, msbAlt, altInd0, altInd1;
  };

  inline uint16_t nickLPTBase(uint8_t port82, uint8_t port83) {
    return uint16_t((uint16_t(port82) << 4)
                    | (uint16_t(port83 & 0x0F) << 12));
  }

  inline uint32_t nickLPTPhysicalAddress(uint16_t address) {
    // FC, FD, FE and FF represent the NICK's 64KiB video-memory window.
    return 0x003F0000U | uint32_t(address);
  }

  inline const char *nickLPTVideoModeName(uint8_t mode) {
    static const char *const names[8] = {
      "VSYNC", "PIXEL", "LPIXEL", "CH256", "CH128", "CH64",
      "INVALID", "INVALID"
    };
    return names[mode & 7];
  }

  inline unsigned int nickLPTColorCount(uint8_t mode) {
    static const unsigned int counts[4] = { 2U, 4U, 16U, 256U };
    return counts[mode & 3];
  }

  inline uint8_t nickLPTFixedPalette(uint8_t fixBias, uint8_t index) {
    return uint8_t(((fixBias & 0x1F) << 3) | (index & 7));
  }

  // Exact NICK 3/3/2 channel mapping, using 8-bit display intensities.
  // Same bit routing as Nick::convertPixelToRGB() in src/nick.cpp.
  inline void nickLPTColorRGB(uint8_t v, uint8_t& r, uint8_t& g, uint8_t& b) {
    const unsigned int r3 = ((unsigned(v) << 2) & 4U)
                            | ((unsigned(v) >> 2) & 2U)
                            | ((unsigned(v) >> 6) & 1U);
    const unsigned int g3 = ((unsigned(v) << 1) & 4U)
                            | ((unsigned(v) >> 3) & 2U)
                            | ((unsigned(v) >> 7) & 1U);
    const unsigned int b2 = ((unsigned(v) >> 1) & 2U)
                            | ((unsigned(v) >> 5) & 1U);
    r = uint8_t((r3 * 255U + 3U) / 7U);
    g = uint8_t((g3 * 255U + 3U) / 7U);
    b = uint8_t((b2 * 255U + 1U) / 3U);
  }

  // Use text contrast rather than illegible dark ink on dark palette cells.
  inline bool nickLPTNeedsLightText(uint8_t r, uint8_t g, uint8_t b) {
    return (299U * unsigned(r) + 587U * unsigned(g) + 114U * unsigned(b))
           < 138000U;
  }

  inline uint16_t nickLPTByteAddress(uint16_t entryAddr, unsigned byteOffset) {
    return uint16_t((entryAddr & 0xFFF0U) + (byteOffset & 15U));
  }

  inline bool nickLPTEditPrecondition(uint16_t shownBase, uint16_t actualBase,
                                      uint8_t shownByte, uint8_t actualByte) {
    return shownBase == actualBase && shownByte == actualByte;
  }

  inline bool nickLPTParseHexByte(const char *txt, uint8_t& out) {
    if (!txt || !txt[0] || !txt[1] || txt[2]) return false;
    unsigned value = 0U;
    for (unsigned i = 0; i < 2U; ++i) {
      const unsigned char c = (unsigned char) txt[i];
      unsigned n = 0U;
      if (c >= '0' && c <= '9') n = unsigned(c - '0');
      else if (c >= 'A' && c <= 'F') n = unsigned(c - 'A') + 10U;
      else if (c >= 'a' && c <= 'f') n = unsigned(c - 'a') + 10U;
      else return false;
      value = (value << 4) | n;
    }
    out = uint8_t(value);
    return true;
  }

  // The editor's menu labels start with their true 00..0F NICK LPB byte
  // offset. Never use Fl_Choice::value() as the RAM offset: FLTK allocates
  // additional indices for submenus, so labels and indices can diverge.
  inline int nickLPTByteOffsetFromLabel(const char *label) {
    if (!label || !label[0] || !label[1] || label[2] != ' ')
      return -1;
    const char hex[3] = { label[0], label[1], 0 };
    uint8_t value = 0;
    if (!nickLPTParseHexByte(hex, value) || value >= 16U)
      return -1;
    return int(value);
  }

  inline NickLPTEntry decodeNickLPTEntry(uint16_t address,
                                        const uint8_t (&raw)[16]) {
    NickLPTEntry entry = {};
    entry.address = uint16_t(address & 0xFFF0);
    entry.lines = uint16_t(256U - unsigned(raw[0]));
    entry.modeByte = raw[1];
    entry.irq = (raw[1] & 0x80) != 0;
    entry.colorMode = uint8_t((raw[1] >> 5) & 3);
    entry.vres = (raw[1] & 0x10) != 0;
    entry.videoMode = uint8_t((raw[1] >> 1) & 7);
    entry.reload = (raw[1] & 1) != 0;
    entry.lsbAlt = (raw[2] & 0x40) != 0;
    entry.msbAlt = (raw[2] & 0x80) != 0;
    entry.leftMargin = uint8_t(raw[2] & 0x3F);
    entry.altInd1 = (raw[3] & 0x40) != 0;
    entry.altInd0 = (raw[3] & 0x80) != 0;
    entry.rightMargin = uint8_t(raw[3] & 0x3F);
    entry.ld1 = uint16_t(raw[4]) | (uint16_t(raw[5]) << 8);
    entry.ld2 = uint16_t(raw[6]) | (uint16_t(raw[7]) << 8);
    for (unsigned i = 0; i != 8; ++i) entry.palette[i] = raw[8 + i];
    return entry;
  }
}
#endif
