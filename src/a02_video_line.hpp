// SPDX-License-Identifier: GPL-2.0-or-later
// A02: bounds-checked 2DFX overlay on the existing 48x16-pixel video wire.
// Active only in the experimental ENABLE_A02_2DFX build.
#ifndef EP128EMU_A02_VIDEO_LINE_HPP
#define EP128EMU_A02_VIDEO_LINE_HPP
#ifdef ENABLE_A02_2DFX
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace Ep128Emu {
namespace A02VideoLine {
  enum {
    kSlots = 48,
    kPixels = 768,
    kMaxEncodedBytes = 48 * 17
  };

  inline size_t slotLength(uint8_t mode)
  {
    switch (mode) {
    case 0x00: return 1;
    case 0x01: return 2;
    case 0x02: return 3;
    case 0x03: return 4;
    case 0x04: return 5;
    case 0x06: return 7;
    case 0x08: return 9;
    case 0x10: return 17;
    default: return 0;
    }
  }

  inline uint8_t decodePixel(const uint8_t *s, size_t n)
  {
    switch (s[0]) {
    case 0x00: return 0;
    case 0x01: return s[1];
    case 0x02: return s[1 + (n >> 3)];
    case 0x03: return (s[3] & (0x80U >> (n >> 1))) ? s[2] : s[1];
    case 0x04: return s[1 + (n >> 2)];
    case 0x06:
      return (s[(n < 8 ? 3 : 6)] & (0x80U >> (n & 7)))
             ? s[n < 8 ? 2 : 5] : s[n < 8 ? 1 : 4];
    case 0x08: return s[1 + (n >> 1)];
    case 0x10: return s[1 + n];
    default: return 0;
    }
  }

  inline uint8_t tvcPixel(uint8_t logical)
  {
    // 2DFX IGRB nibble -> existing TVCVideo::convertPixelToRGB index.
    static const uint8_t table[16] = {
      0x00, 0xC0, 0x0C, 0xCC, 0x30, 0xF0, 0x3C, 0xFC,
      0x03, 0xC3, 0x0F, 0xCF, 0x33, 0xF3, 0x3F, 0xFF
    };
    return table[logical & 15];
  }

  // Deterministic and transactional: first validate every input slot and
  // output capacity; only then write. On failure output is untouched.
  // The 768 colour/coverage bytes refer to the full 768-pixel PAL line.
  inline bool compose(const uint8_t *src, size_t nBytes,
                      const uint8_t *colours, const uint8_t *coverage,
                      const uint8_t *nickPalette, bool tvc,
                      uint8_t *dst, size_t dstCapacity, size_t& outBytes)
  {
    if (!src || !colours || !coverage || !dst || (!tvc && !nickPalette))
      return false;
    size_t readPos = 0;
    size_t required = 0;
    for (size_t slot = 0; slot < kSlots; ++slot) {
      if (readPos >= nBytes)
        return false;
      const uint8_t mode = src[readPos];
      const size_t bytes = slotLength(mode);
      if (!bytes || bytes > nBytes - readPos)
        return false;
      bool overlay = false;
      // All physical slots can carry 2DFX-selected pixels, including NICK
      // blank and border. The core coverage is the overlay authority.
      for (size_t x = 0; x < 16; ++x)
        overlay |= (coverage[slot * 16 + x] != 0);
      required += (overlay ? 17 : bytes);
      if (required > dstCapacity || required > kMaxEncodedBytes)
        return false;
      readPos += bytes;
    }
    if (readPos != nBytes)
      return false;

    readPos = 0;
    size_t writePos = 0;
    for (size_t slot = 0; slot < kSlots; ++slot) {
      const uint8_t *s = src + readPos;
      const size_t bytes = slotLength(s[0]);
      bool overlay = false;
      for (size_t x = 0; x < 16; ++x)
        overlay |= (coverage[slot * 16 + x] != 0);
      if (!overlay) {
        memcpy(dst + writePos, s, bytes);
        writePos += bytes;
      } else {
        dst[writePos++] = 0x10;
        // TVC's original low-resolution modes have two physical pixels
        // per visible pixel; use odd-position overlay selection per pair.
        // NICK, and TVC high-res 06/10, keep all sixteen pixel positions.
        const bool pairPixels = tvc && s[0] != 0x06 && s[0] != 0x10;
        for (size_t x = 0; x < 16; ++x) {
          const size_t chosen = pairPixels ? (x | size_t(1)) : x;
          const uint8_t c = coverage[slot * 16 + chosen]
                ? (tvc ? tvcPixel(colours[slot * 16 + chosen])
                       : nickPalette[colours[slot * 16 + chosen] & 15])
                : decodePixel(s, pairPixels ? chosen : x);
          dst[writePos++] = c;
        }
      }
      readPos += bytes;
    }
    outBytes = writePos;
    return true;
  }
}
}
#endif
#endif
