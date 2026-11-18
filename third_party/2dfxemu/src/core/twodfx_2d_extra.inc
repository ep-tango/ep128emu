/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Károly Vaczkó
 */


static uint8_t raster_ext_packed(const twodfx_emu *d, uint32_t a, unsigned x) {
  uint8_t v = d->ram[(a + x / 2u) & (TWODFX_EMU_RESOURCE_BYTES - 1u)];
  return (uint8_t)((x & 1u) ? (v >> 4) : (v & 15u));
}
static uint8_t raster_ext_pattern_pixel(const twodfx_emu *d, unsigned bank, unsigned id,
                                int x, int y) {
  const twodfx_address_descriptor *desc = &d->active_patterns[bank & 63u];
  uint32_t a = desc->resource_address + (uint32_t)id * 64u +
               (uint32_t)(y & 7) * 8u;
  return raster_ext_packed(d, a, (unsigned)(x & 15));
}
static void raster_ext_text(twodfx_emu *d, twodfx_raster *r, int x, int y,
                     unsigned id, uint32_t str, uint8_t fg, uint8_t bg) {
  const twodfx_font_descriptor *f = &d->active_fonts[id & 63u];
  unsigned w, h, row_bytes, stride, ci;
  if (!f->valid || !f->width_units || !f->height_units) return;
  w = (unsigned)f->width_units * 8u;
  h = (unsigned)f->height_units * 8u;
  row_bytes = (unsigned)f->width_units;
  stride = row_bytes * h;
  for (ci = 0; ci < 255u; ++ci) {
    uint8_t ch = d->ram[(str + ci) & (TWODFX_EMU_RESOURCE_BYTES-1u)];
    int cx = x + (int)(ci * w);
    unsigned ry, bx;
    if (ch >= 128u) break;
    if (cx > r->x1 && w) break;
    if (bg != d->transparent)
      twodfx_raster_fill_rect(r, cx, y, cx+(int)w-1, y+(int)h-1, bg);
    for (ry = 0; ry < h; ++ry) {
      int py = y + (int)ry;
      if (py < r->y0 || py > r->y1) continue;
      for (bx = 0; bx < row_bytes; ++bx) {
        uint32_t adr = (f->resource_address + (uint32_t)ch*stride +
                        ry*row_bytes + bx) & (TWODFX_EMU_RESOURCE_BYTES-1u);
        uint8_t bits = d->ram[adr];
        unsigned bit;
        if (!bits) continue;
        for (bit = 0; bit < 8u; ++bit) {
          if (bits & (0x80u >> bit))
            twodfx_raster_plot(r,cx+(int)(bx*8u+bit),py,fg);
        }
      }
    }
  }
}
static void raster_ext_pattern_fill(twodfx_emu *d,twodfx_raster *r,
                             int rawx,int rawy,unsigned w,unsigned h,
                             unsigned bank,unsigned id,int origin_x,int origin_y) {
  int x,y,x0,x1,y0,y1;
  if (!d->active_patterns[bank & 63u].valid || !w || !h) return;

  x0=rawx-origin_x; y0=rawy-origin_y;
  x1=x0+(int)w-1; y1=y0+(int)h-1;
  if (x0<r->x0)x0=r->x0;
  if (y0<r->y0)y0=r->y0;
  if (x1>r->x1)x1=r->x1;
  if (y1>r->y1)y1=r->y1;
  for(y=y0;y<=y1;++y) for(x=x0;x<=x1;++x){
    uint8_t c=raster_ext_pattern_pixel(d,bank,id,
      x+origin_x-((bank&64)?0:rawx),y+origin_y-((bank&64)?0:rawy));
    if(!(bank&128)||c!=d->transparent)twodfx_raster_plot(r,x,y,c);
  }
}
static void raster_ext_tilemap(twodfx_emu *d,twodfx_raster *r,int rawx,int rawy,
                        unsigned map_id,unsigned srcx,unsigned srcy,
                        unsigned cols,unsigned rows,int origin_x,int origin_y) {
  const twodfx_tilemap_descriptor *m=&d->active_tilemaps[map_id & 31u];
  const twodfx_address_descriptor *t;
  unsigned cx,cy;
  int dx,dy;
  if (!m->valid || !cols || !rows || srcx>=m->width_cells || srcy>=m->height_cells) return;
  t=&d->active_tilesets[m->tileset & 63u];
  if(!t->valid) return;
  if(cols>m->width_cells-srcx)cols=m->width_cells-srcx;
  if(rows>m->height_cells-srcy)rows=m->height_cells-srcy;
  rawx &= ~1;
  dx=rawx-origin_x; dy=rawy-origin_y;
  for(cy=0;cy<rows;cy++) {
    int ybase=dy+(int)(cy*8u);
    if (ybase > r->y1) break;
    if (ybase+7 < r->y0) continue;
    for(cx=0;cx<cols;cx++) {
      int xbase=dx+(int)(cx*16u), x,y;
      uint32_t mapaddr,tileaddr;
      unsigned tid;
      if (xbase>r->x1) break;
      if (xbase+15<r->x0) continue;
      mapaddr=(m->resource_address+(srcy+cy)*m->width_cells+(srcx+cx)) &
              (TWODFX_EMU_RESOURCE_BYTES-1u);
      tid=d->ram[mapaddr];
      tileaddr=t->resource_address+tid*64u;
      for(y=ybase;y<ybase+8;y++) {
        if(y<r->y0||y>r->y1)continue;
        for(x=xbase;x<xbase+16;x++) {
          uint8_t pixel;
          if(x<r->x0||x>r->x1)continue;
          pixel=raster_ext_packed(d,tileaddr+(unsigned)(y-ybase)*8u,(unsigned)(x-xbase));
          twodfx_raster_plot(r,x,y,pixel);
        }
      }
    }
  }
}
/* Keep the same finite span budget and discovery order as the device. */
#define PATTERN_SPAN_LIMIT 17381u
typedef struct { uint16_t x0, x1, y; } pattern_span;
typedef struct {
  twodfx_raster *r;
  uint8_t *visited, target;
  pattern_span *spans;
  unsigned count;
} pattern_search;
static int pattern_candidate(const pattern_search *s, int x, int y) {
  size_t at = (size_t)y * s->r->width + (unsigned)x;
  return !s->visited[at] && s->r->colour[at] == s->target;
}
static int pattern_schedule(pattern_search *s, int x, int y) {
  if (s->count == PATTERN_SPAN_LIMIT || !pattern_candidate(s, x, y)) return x + 1;
  int left = x, right = x;
  while (left > s->r->x0 && pattern_candidate(s, left - 1, y)) --left;
  while (right < s->r->x1 && pattern_candidate(s, right + 1, y)) ++right;
  memset(s->visited + (size_t)y * s->r->width + left, 1, (size_t)(right - left + 1));
  pattern_span item = {(uint16_t)left, (uint16_t)right, (uint16_t)y};
  s->spans[s->count++] = item;
  return right + 1;
}
static void raster_ext_pattern_bucket(twodfx_emu *d, twodfx_raster *r,
    int sx, int sy, unsigned bank, unsigned pat, int origin_x, int origin_y) {
  if (!d->active_patterns[bank & 63u].valid || sx < r->x0 || sx > r->x1 ||
      sy < r->y0 || sy > r->y1) return;
  pattern_search s;
  s.r = r;
  s.target = r->colour[(size_t)sy * r->width + sx];
  s.count = 0;
  s.visited = (uint8_t *)calloc((size_t)r->width * r->height, 1);
  s.spans = (pattern_span *)malloc(PATTERN_SPAN_LIMIT * sizeof *s.spans);
  if (s.visited && s.spans) {
    pattern_schedule(&s, sx, sy);
    while (s.count) {
      pattern_span run = s.spans[--s.count];
      for (int x = run.x0; x <= run.x1; ++x) {
        uint8_t c = raster_ext_pattern_pixel(d, bank, pat,
            (bank & 64) ? x + origin_x : x - sx,
            (bank & 64) ? run.y + origin_y : run.y - sy);
        if (!(bank & 128) || c != d->transparent) twodfx_raster_plot(r, x, run.y, c);
      }
      for (int row = (int)run.y - 1; row <= (int)run.y + 1; row += 2) {
        if (row < r->y0 || row > r->y1) continue;
        for (int x = run.x0; x <= run.x1;) x = pattern_schedule(&s, x, row);
      }
    }
  }
  free(s.spans);
  free(s.visited);
}

static void raster_ext_round(twodfx_raster *r,int x,int y,unsigned w,unsigned h,
 unsigned rx,unsigned ry,unsigned st,uint8_t fg,uint8_t bg,int fill){
 if(fill)gfx2d_fill_round_rect_raw(r,x,y,w,h,rx,ry,fg);
 else gfx2d_draw_round_rect_raw(r,x,y,w,h,st,rx,ry,fg,bg);
}
static void curve_box(int *x,int *y,unsigned *w,unsigned *h,int ox,int oy,int rx,int ry){
 int left=*x-(int)*w,top=*y-(int)*h,right=*x+(int)*w,bottom=*y+(int)*h;
 if(ox==0 && oy==0){if(left< -rx)left=-rx;if(top< -ry)top=-ry;}
 *x=left;*y=top;*w=right-left+1;*h=bottom-top+1;
}
static void raster_ext_ellipse(twodfx_raster*r,int x,int y,unsigned w,unsigned h,
 unsigned st,uint8_t fg,uint8_t bg,int fill,int ox,int oy,int rx,int ry){
 curve_box(&x,&y,&w,&h,ox,oy,rx,ry);
 if(fill)gfx2d_fill_ellipse_raw(r,x,y,w,h,fg);else gfx2d_draw_ellipse_raw(r,x,y,w,h,st,fg,bg);
}
static void raster_ext_arc_pie(twodfx_raster*r,int x,int y,unsigned w,unsigned h,
 unsigned a,unsigned b,unsigned st,uint8_t fg,int pie,int ox,int oy,int rx,int ry){
 curve_box(&x,&y,&w,&h,ox,oy,rx,ry);
 if(pie)gfx2d_draw_pie_raw(r,x,y,w,h,fg,a,b);else gfx2d_draw_arc_raw(r,x,y,w,h,st,fg,a,b);
}
