/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Károly Vaczkó
 */
#ifndef TWODFX_RASTER_H
#define TWODFX_RASTER_H

#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
  uint8_t *colour, *cover;
  unsigned width, height;
  uint8_t key;
  int x0, y0, x1, y1; /* inclusive clip rectangle */
} twodfx_raster;
int twodfx_raster_bind(twodfx_raster *r, uint8_t *colour, uint8_t *coverage,
                       unsigned width, unsigned height);
void twodfx_raster_clear(twodfx_raster *r);
void twodfx_raster_clip(twodfx_raster *r, int x0, int y0, int x1, int y1);
void twodfx_raster_plot(twodfx_raster *r, int x, int y, uint8_t colour);
void twodfx_raster_line(twodfx_raster *r, int x0, int y0, int x1, int y1, uint8_t colour);
void twodfx_raster_rect(twodfx_raster *r, int x0, int y0, int x1, int y1, uint8_t colour);
void twodfx_raster_fill_rect(twodfx_raster *r, int x0, int y0, int x1, int y1, uint8_t colour);
void twodfx_raster_triangle(twodfx_raster *r, int x0, int y0, int x1, int y1,
                            int x2, int y2, uint8_t colour);
void twodfx_raster_fill_triangle(twodfx_raster *r, int x0, int y0, int x1, int y1,
                                 int x2, int y2, uint8_t colour);
void twodfx_raster_fill_triangle_sample(twodfx_raster*,int,int,int,int,int,int,uint8_t,int);
/* Four-connected fill matching colour; 128 pending scanline seeds; returns modified pixels. */
size_t twodfx_raster_flood(twodfx_raster *r, int x, int y, uint8_t colour);
/* Interleaved low nibble=left pixel, high nibble=right. Transparent pixels
 * don't modify existing output. Stride is bytes, src_bytes bounds ALL reads. */
int twodfx_raster_blit4(twodfx_raster *r, int x, int y, unsigned width, unsigned height,
                        const uint8_t *src, size_t src_bytes, size_t stride, uint8_t transparent);
#ifdef __cplusplus
}
#endif
#endif
