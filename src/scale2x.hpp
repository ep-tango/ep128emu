// ep128emu -- opt-in, output-only Scale2x pixel art interpolation.
// Written independently from the published 3x3 nearest-edge Scale2x rule.
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef EP128EMU_SCALE2X_HPP
#define EP128EMU_SCALE2X_HPP

#include <cstddef>
#include <limits>

namespace Ep128Emu {
namespace PixelArt {

  // Input pixels are packed colors: uint16_t RGB565 or uint32_t RGB(A).
  // No color mixing or alpha modification. Width/height are SOURCE pixels.
  // Source and destination must be non-overlapping, caller-owned arrays.
  // Output size is exactly 2 * width by 2 * height.
  // Both strides are in pixels, not bytes. On invalid shape: no writes.
  template <typename Pixel>
  inline bool scale2x(const Pixel *src, std::size_t width,
                      std::size_t height, std::size_t srcStride,
                      Pixel *dst, std::size_t dstStride)
  {
    const std::size_t maxValue = std::numeric_limits<std::size_t>::max();
    if (!src || !dst || src == dst || width == 0 || height == 0 ||
        width > maxValue / 2 || height > maxValue / 2 ||
        srcStride < width || dstStride < width * 2 ||
        height > maxValue / srcStride ||
        (height * 2) > maxValue / dstStride)
      return false;

    for (std::size_t y = 0; y < height; y++) {
      const Pixel *top = src + (y ? y - 1 : y) * srcStride;
      const Pixel *mid = src + y * srcStride;
      const Pixel *bottom =
          src + (y + 1 < height ? y + 1 : y) * srcStride;
      Pixel *row0 = dst + (y * 2) * dstStride;
      Pixel *row1 = row0 + dstStride;
      for (std::size_t x = 0; x < width; x++) {
        const std::size_t leftX = (x ? x - 1 : x);
        const std::size_t rightX = (x + 1 < width ? x + 1 : x);
        const Pixel e = mid[x];
        const Pixel b = top[x];
        const Pixel d = mid[leftX];
        const Pixel f = mid[rightX];
        const Pixel h = bottom[x];
        const bool edge = (b != h && d != f);
        const std::size_t outX = x * 2;
        row0[outX] = (edge && d == b ? d : e);
        row0[outX + 1] = (edge && b == f ? f : e);
        row1[outX] = (edge && d == h ? d : e);
        row1[outX + 1] = (edge && h == f ? f : e);
      }
    }
    return true;
  }
  // Classic Scale4x: apply the exact Scale2x edge rule twice.
  // The caller owns a disjoint intermediate 2x buffer; there are no heap
  // allocations or color interpolations within the algorithm itself.
  // Fully preflight BOTH stages before writing any destination pixel.
  template <typename Pixel>
  inline bool scale4x(const Pixel *src, std::size_t width,
                      std::size_t height, std::size_t srcStride,
                      Pixel *intermediate, std::size_t intermediateStride,
                      Pixel *dst, std::size_t dstStride)
  {
    const std::size_t maxValue = std::numeric_limits<std::size_t>::max();
    if (!src || !intermediate || !dst || width == 0 || height == 0 ||
        src == intermediate || src == dst || intermediate == dst ||
        width > maxValue / 4 || height > maxValue / 4 ||
        srcStride < width || intermediateStride < width * 2 ||
        dstStride < width * 4 || height > maxValue / srcStride ||
        height * 2 > maxValue / intermediateStride ||
        height * 4 > maxValue / dstStride)
      return false;
    if (!scale2x(src, width, height, srcStride,
                 intermediate, intermediateStride))
      return false;
    return scale2x(intermediate, width * 2, height * 2,
                   intermediateStride, dst, dstStride);
  }

  // Read one exact pixel of the *virtual* 2x image. This is identical
  // to scale2x()[y2 * (2 * width) + x2], without materializing that image.
  // Internal fast path: caller guarantees x2 < 2*width and y2 < 2*height.
  template <typename Pixel>
  inline Pixel sampleScale2x(const Pixel *src, std::size_t width,
                             std::size_t height, std::size_t srcStride,
                             std::size_t x2, std::size_t y2)
  {
    const std::size_t x = x2 >> 1, y = y2 >> 1;
    const Pixel e = src[y * srcStride + x];
    const Pixel b = src[(y ? y - 1 : y) * srcStride + x];
    const Pixel d = src[y * srcStride + (x ? x - 1 : x)];
    const Pixel f = src[y * srcStride + (x + 1 < width ? x + 1 : x)];
    const Pixel h = src[(y + 1 < height ? y + 1 : y) * srcStride + x];
    if (b == h || d == f)
      return e;
    if ((y2 & 1U) == 0) {
      if ((x2 & 1U) == 0 && d == b) return d;
      if ((x2 & 1U) != 0 && b == f) return f;
    }
    else {
      if ((x2 & 1U) == 0 && d == h) return d;
      if ((x2 & 1U) != 0 && h == f) return f;
    }
    return e;
  }

  // The original Scale2x/Scale4x image is virtual; generate only the
  // pixels that the host window can actually display, with exact nearest
  // mapping and NO pixel blending. For Scale4x, src is the completed 2x
  // intermediate, so sampling its second pass is bit-identical to the full
  // 4x output at the selected virtual coordinates.
  // All strides are PIXELS; all caller buffers are disjoint.
  template <typename Pixel>
  inline bool scale2xViewport(const Pixel *src, std::size_t width,
                              std::size_t height, std::size_t srcStride,
                              Pixel *dst, std::size_t dstWidth,
                              std::size_t dstHeight, std::size_t dstStride)
  {
    const std::size_t maxValue = std::numeric_limits<std::size_t>::max();
    if (!src || !dst || src == dst || !width || !height ||
        !dstWidth || !dstHeight ||
        width > maxValue / 2 || height > maxValue / 2 ||
        srcStride < width || dstStride < dstWidth ||
        height > maxValue / srcStride ||
        dstHeight > maxValue / dstStride ||
        dstWidth > (maxValue - width) / (width * 2) ||
        dstHeight > (maxValue - height) / (height * 2))
      return false;
    // Exact center-sample phases for a host viewport at native size.
    // In the 2x pass, center sampling (2*x+1, 2*y+1) selects the
    // lower-right edge-rule output of source (x,y).  The 4x pass
    // downsamples a prebuilt 2x buffer at (4*x+2, 4*y+2): this selects
    // the upper-left output of source (2*x+1, 2*y+1).  Eliminate
    // per-pixel divisions and virtual-coordinate recomputation without
    // changing colors, clamping, memory ownership or other view sizes.
    if (dstWidth == width && dstHeight == height) {
      for (std::size_t y = 0; y < height; ++y) {
        const Pixel *top = src + (y ? y - 1 : y) * srcStride;
        const Pixel *mid = src + y * srcStride;
        const Pixel *bottom = src + (y + 1 < height ? y + 1 : y) * srcStride;
        Pixel *line = dst + y * dstStride;
        for (std::size_t x = 0; x < width; ++x) {
          const Pixel e = mid[x], b = top[x], h = bottom[x];
          const Pixel d = mid[x ? x - 1 : x];
          const Pixel f = mid[x + 1 < width ? x + 1 : x];
          if (sizeof(Pixel) == 2U)
            line[x] = (b != h && d != f && h == f ? f : e);
          else {
            const Pixel mask = Pixel(0) - Pixel((b != h) & (d != f) & (h == f));
            line[x] = (e & ~mask) | (f & mask);
          }
        }
      }
      return true;
    }
    if ((width % 2U) == 0 && (height % 2U) == 0 &&
        dstWidth == width / 2U && dstHeight == height / 2U) {
      for (std::size_t y = 0; y < dstHeight; ++y) {
        const std::size_t sy = y * 2U + 1U;
        const Pixel *top = src + (sy - 1U) * srcStride;
        const Pixel *mid = src + sy * srcStride;
        const Pixel *bottom = src + (sy + 1U < height ? sy + 1U : sy) * srcStride;
        Pixel *line = dst + y * dstStride;
        for (std::size_t x = 0; x < dstWidth; ++x) {
          const std::size_t sx = x * 2U + 1U;
          const Pixel e = mid[sx], b = top[sx], h = bottom[sx];
          const Pixel d = mid[sx - 1U];
          const Pixel f = mid[sx + 1U < width ? sx + 1U : sx];
          if (sizeof(Pixel) == 2U)
            line[x] = (b != h && d != f && d == b ? d : e);
          else {
            const Pixel mask = Pixel(0) - Pixel((b != h) & (d != f) & (d == b));
            line[x] = (e & ~mask) | (d & mask);
          }
        }
      }
      return true;
    }
    const std::size_t virtualWidth = width * 2;
    const std::size_t virtualHeight = height * 2;
    for (std::size_t y = 0; y < dstHeight; ++y) {
      // Sample the CENTER of each destination pixel. Sampling the
      // upper-left corner suppresses the Scale4x effect at 1x window size
      // and differs from GL_NEAREST and FLTK's own image scaling.
      const std::size_t sourceY =
          (y * virtualHeight + height) / dstHeight;
      Pixel *line = dst + y * dstStride;
      for (std::size_t x = 0; x < dstWidth; ++x) {
        const std::size_t sourceX =
            (x * virtualWidth + width) / dstWidth;
        line[x] = sampleScale2x(src, width, height, srcStride,
                                sourceX, sourceY);
      }
    }
    return true;
  }
}
}

#endif  // EP128EMU_SCALE2X_HPP
