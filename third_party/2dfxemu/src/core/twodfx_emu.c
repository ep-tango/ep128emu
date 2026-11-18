/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Károly Vaczkó
 */

#define twodfx_resource_job_status twodfx_emu_job_status_type
#include "twodfx_emu.h"
#undef twodfx_resource_job_status
#include "twodfx_raster.h"
#include "twodfx_3d.h"
#include "2dfx_builtin_logo.h"
#include "2dfx_builtin_logo_small.h"
#include <stdbool.h>
#include "../demos/firmware_page.h"
#include <stdlib.h>
#include <string.h>

#include "twodfx_demo_states.inc"
#define TWODFX_FRAME_PIXELS (TWODFX_EMU_VIDEO_W_MAX * TWODFX_EMU_VIDEO_H_MAX)

enum { SESSION_NONE, SESSION_SPRITE, SESSION_2DPT, SESSION_CTRL,
       SESSION_UPLOAD, SESSION_UNZX };

typedef struct {
  uint32_t source, destination;
  uint8_t format; /* 0=RAW, 1=NICK, 2=TVC, 3=full resource clear */
} portable_resource_job;
typedef struct {uint16_t x,y;uint8_t mode,colors,initialized,valid,hx,hy;} portable_pointer;
struct twodfx_emu {
  portable_pointer pointer_shadow,pointer_active;
  uint8_t pointer_user_shadow[144],pointer_user_active[144];
  uint8_t pointer_pending,pointer_hx,pointer_hy,logo_small;
  uint32_t pointer_address,rng_state;

  twodfx_machine machine;
  uint8_t *ram;
  uint8_t *frame_colour, *frame_coverage;
  uint8_t render_enabled;
  uint8_t collision_reported, collision_pending, collision_ready;
  uint8_t started, awake, terminal;
  uint8_t engine, transparent, fps, overrun_visual,fps_phase;
  uint8_t session, opcode, param_n, param_needed, foreground, spb_id;
  uint8_t params[12];
  uint8_t spb_offset;
  uint8_t upload_header_n, upload_convert;
  uint8_t upload_header[5];
  uint32_t upload_addr, upload_remain;
  uint8_t shadow_spt[64][9], active_spt[64][9];
  twodfx_2dpt_entry shadow_2dpt[2][256];
  twodfx_2dpt_entry active_2dpt[2][256];
  uint8_t ctrl_raw[256][10];
  uint8_t ctrl_size[256];
  uint16_t raster_lines[4];
  uint8_t raster_pending;
  uint64_t video_frames;
  uint8_t logo_visible, egg_input;
  /* Portable egg lifecycle.  Only implemented IDs can be selected. */
  uint8_t egg_id, egg_pending;
  uint8_t egg_fifo[64], egg_head, egg_tail, egg_overflow;
  uint8_t demo_states[TWODFX_DEMO_STATE_BYTES];
  uint8_t logo_art_loaded;
  uint16_t logo_x, logo_y;
  uint8_t logo_main, logo_accent;
  uint8_t logo_art[TWODFX_EMU_LOGO_BYTES];
  twodfx_emu_job_status_type resource_job;
  portable_resource_job jobs[TWODFX_EMU_RESOURCE_QUEUE_CAPACITY];
  uint8_t job_head, job_tail;
  uint8_t overrun_pending, overrun_phase;
  uint32_t job_output_bytes, job_overflow_count;
  twodfx_font_descriptor fonts[64];
  twodfx_bitmap_descriptor bitmaps[256];
  twodfx_address_descriptor patterns[64], tilesets[64];
  twodfx_tilemap_descriptor tilemaps[32];
  /* Published copies: descriptor commands change shadow immediately, never
     the rendering scene before the next accepted frame. */
  twodfx_font_descriptor active_fonts[64];
  twodfx_bitmap_descriptor active_bitmaps[256];
  twodfx_address_descriptor active_patterns[64], active_tilesets[64];
  twodfx_tilemap_descriptor active_tilemaps[32];
  twodfx_3d_scene scene3d;
};

#include "twodfx_pointer.inc"
static uint32_t addr23(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | (((uint32_t)p[2] & 127u) << 16);
}
/* Bit-level format conversion from public NICK/TVC wire definitions. */
static uint8_t source_to_internal(uint8_t in, uint8_t mode) {
  uint8_t left = 0, right = 0;
  unsigned i;
  if (mode == 0) return in;
  /* Both interleaved formats map source 1,5,3,7 to left index bits
     3,2,1,0, and source 0,4,2,6 to the right index. Low nibble is left. */
  for (i = 0; i < 4; ++i) {
    left |= (uint8_t)(((in >> (7u - 2u*i)) & 1u) << (3u - i));
    right |= (uint8_t)(((in >> (6u - 2u*i)) & 1u) << (3u - i));
  }
  if (mode != 0) {
    left = (uint8_t)(((left & 1u) << 3) | (left & 6u) | ((left >> 3) & 1u));
    right = (uint8_t)(((right & 1u) << 3) | (right & 6u) | ((right >> 3) & 1u));
  }
  return (uint8_t)(left | (right << 4));
}
uint8_t twodfx_emu_convert_byte(uint8_t in, unsigned format) {
  return format <= 2u ? source_to_internal(in, (uint8_t)format) : in;
}

typedef struct {
  const uint8_t *ram;
  uint32_t pos, used;
  uint8_t bitbyte, mask;
  uint8_t bad;
} zx1_input;
static uint8_t zx1_byte(zx1_input *r) {
  uint8_t v;
  if (r->used >= TWODFX_EMU_RESOURCE_BYTES) { r->bad = 1; return 0; }
  v = r->ram[r->pos];
  r->pos = (r->pos + 1u) & (TWODFX_EMU_RESOURCE_BYTES - 1u);
  ++r->used;
  return v;
}
static unsigned zx1_bit(zx1_input *r) {
  if (!r->mask) { r->bitbyte = zx1_byte(r); r->mask = 0x80; }
  { unsigned v = (r->bitbyte & r->mask) != 0; r->mask >>= 1; return v; }
}
static uint32_t zx1_gamma(zx1_input *r) {
  uint32_t n = 1;
  for (;;) {
    unsigned b = zx1_bit(r);
    if (r->bad) return 0;
    if (!b) return n;
    if (n > (TWODFX_EMU_RESOURCE_BYTES >> 1)) { r->bad = 1; return 0; }
    n = (n << 1) | zx1_bit(r);
    if (r->bad) return 0;
  }
}

static int zx1_decompress(twodfx_emu *d, uint32_t src, uint32_t dst,
                          uint8_t format, uint32_t *written) {
  zx1_input in;

  uint32_t size = 0, last_offset = 1;
  int phase = 0; /* 0 literal; 1 reuse prior offset; 2 new offset */
  int done = 0, ok = 0;

  memset(&in, 0, sizeof(in));
  in.ram = d->ram;
  in.pos = src & (TWODFX_EMU_RESOURCE_BYTES - 1u);
  while (!in.bad && !done) {
    uint32_t len = 0, off = last_offset, i;
    if (phase == 0) {
      len = zx1_gamma(&in);
      if (len == 0 || len > TWODFX_EMU_RESOURCE_BYTES-size || in.bad) break;
      for (i = 0; i < len && !in.bad; ++i)
        { uint8_t value=zx1_byte(&in);d->ram[(dst+size++)&0x7fffff]=source_to_internal(value,format); }
      if (in.bad) break;
      phase = zx1_bit(&in) ? 2 : 1;
    } else {
      if (phase == 2) {
        uint32_t a = zx1_byte(&in);
        if (in.bad) break;
        if (a & 1u) {
          uint32_t b = zx1_byte(&in);
          int32_t decoded;
          if (in.bad) break;
          decoded = 32512 - (int32_t)((b & 254u)*128u) -
                    (int32_t)(a & 254u) - (int32_t)(b & 1u);
          if (decoded <= 0) { done = 1; break; }
          off = (uint32_t)decoded;
        } else off = 128u - (a >> 1);
        last_offset = off;
        len = zx1_gamma(&in) + 1u;
      } else len = zx1_gamma(&in);
      if (in.bad || len == 0 || off == 0 || off > size ||
          len > TWODFX_EMU_RESOURCE_BYTES-size) break;
      for (i = 0; i < len; ++i) {
        d->ram[(dst+size)&0x7fffff] = d->ram[(dst+size-off)&0x7fffff];
        ++size;
      }
      phase = zx1_bit(&in) ? 2 : 0;
    }
  }
  if (done && !in.bad) {
    *written = size;
    ok = 1;
  }

  return ok;
}
static void clear_resource_jobs(twodfx_emu *d) {
  memset(d->jobs, 0, sizeof d->jobs);
  d->job_head = d->job_tail = 0;
  d->job_output_bytes = d->job_overflow_count = 0;
  d->resource_job = TWODFX_RESOURCE_JOB_NONE;
}
static int queue_resource_job(twodfx_emu *d, uint32_t src, uint32_t dst, uint8_t format) {
  portable_resource_job *job;
  if (d->terminal) return 0;
  if ((uint8_t)(d->job_head - d->job_tail) >= TWODFX_EMU_RESOURCE_QUEUE_CAPACITY) {
    ++d->job_overflow_count;
    d->overrun_pending = 1;
    return 0;
  }
  job = &d->jobs[d->job_head & (TWODFX_EMU_RESOURCE_QUEUE_CAPACITY - 1u)];
  job->source = src & (TWODFX_EMU_RESOURCE_BYTES - 1u);
  job->destination = dst & (TWODFX_EMU_RESOURCE_BYTES - 1u);
  job->format = format;
  ++d->job_head;
  d->resource_job = TWODFX_RESOURCE_JOB_BUSY;
  d->job_output_bytes = 0;
  return 1;
}
static void service_resource_jobs(twodfx_emu *d) {
  /* Serialized host calls cannot interrupt a job. Keep the running entry
     counted until completion and drain the whole FIFO at this opportunity.
     HSYNC services resources independently of scene publication and FPS. */
  while (d->job_tail != d->job_head) {
    const portable_resource_job job = d->jobs[d->job_tail & (TWODFX_EMU_RESOURCE_QUEUE_CAPACITY - 1u)];
    uint32_t n = 0;
    int ok;
    if (job.format == 3) {
      memset(d->ram, 0, TWODFX_EMU_RESOURCE_BYTES);
      n = TWODFX_EMU_RESOURCE_BYTES;
      ok = 1;
    } else ok = zx1_decompress(d, job.source, job.destination, job.format, &n);
    d->job_output_bytes = ok ? n : 0;
    ++d->job_tail;
    d->resource_job = d->job_tail != d->job_head ? TWODFX_RESOURCE_JOB_BUSY :
                     ok ? TWODFX_RESOURCE_JOB_SUCCESS : TWODFX_RESOURCE_JOB_ERROR;
  }
}
static void clear_sessions(twodfx_emu *d) {
  d->session = SESSION_NONE;
  d->param_n = d->param_needed = 0;
  d->upload_header_n = 0;
  d->upload_remain = 0;
  d->spb_offset = 0;
}
static void blank_entries(twodfx_emu *d) {
  size_t l;
  memset(d->shadow_2dpt, 0, sizeof d->shadow_2dpt);
  memset(d->active_2dpt, 0, sizeof d->active_2dpt);
  
  for (l = 0; l < 2; l++) {
    for(unsigned i=0;i<256;i++)d->shadow_2dpt[l][i].opcode = 0x7f;
    for(unsigned i=0;i<256;i++)d->active_2dpt[l][i].opcode = 0x7f;
  }
}
static void egg_stop(twodfx_emu *d) {
  d->egg_id=0xffu;
  d->egg_pending=0;
  d->egg_head=d->egg_tail=d->egg_overflow=0;

}
static int egg_supported(const twodfx_emu *d, unsigned id) {
  if (id > 21u) return 0;
  /* CF bit patterns and D0 tearing diagnostic are TVC-only. */
  return (id != 15u && id != 16u) || d->machine == TWODFX_EMU_TVC;
}
static void egg_select(twodfx_emu *d,unsigned id) {
  egg_stop(d);
  if(egg_supported(d,id)) {
    d->egg_id=(uint8_t)id;
    d->egg_pending=1;
  }
}
static void egg_enqueue(twodfx_emu *d,uint8_t value) {
  uint8_t next=(uint8_t)((d->egg_head+1u)&63u);
  if(next==d->egg_tail) d->egg_overflow=1;
  else {d->egg_fifo[d->egg_head]=value;d->egg_head=next;}
}
static void clear_application(twodfx_emu *d) {
  egg_stop(d);
  pointer_reset(d,1);d->logo_small=0;d->rng_state=0x9e3779b9u;
  memset(d->ram, 0, TWODFX_EMU_RESOURCE_BYTES);
  memset(d->frame_colour, 0, TWODFX_FRAME_PIXELS);
  memset(d->frame_coverage, 0, TWODFX_FRAME_PIXELS);
  memset(d->shadow_spt, 0, sizeof d->shadow_spt);
  memset(d->active_spt, 0, sizeof d->active_spt);
  memset(d->ctrl_raw, 0, sizeof d->ctrl_raw);
  memset(d->fonts, 0, sizeof d->fonts);
  memset(d->bitmaps, 0, sizeof d->bitmaps);
  memset(d->patterns, 0, sizeof d->patterns);
  memset(d->tilesets, 0, sizeof d->tilesets);
  memset(d->tilemaps, 0, sizeof d->tilemaps);
  memset(d->active_fonts, 0, sizeof d->active_fonts);
  memset(d->active_bitmaps, 0, sizeof d->active_bitmaps);
  memset(d->active_patterns, 0, sizeof d->active_patterns);
  memset(d->active_tilesets, 0, sizeof d->active_tilesets);
  memset(d->active_tilemaps, 0, sizeof d->active_tilemaps);
  twodfx_3d_reset(&d->scene3d);
  d->scene3d.projection_shadow.fx=d->machine==TWODFX_EMU_TVC?508:579;
  d->scene3d.projection_active=d->scene3d.projection_shadow;
  clear_resource_jobs(d);
  d->overrun_pending = d->overrun_phase = 0;
  memset(d->ctrl_size, 0, sizeof d->ctrl_size);
  memset(d->raster_lines, 0, sizeof d->raster_lines);
  d->raster_pending = 0;
  d->collision_reported = d->collision_pending = d->collision_ready = 0;
  d->transparent = d->overrun_visual = d->logo_visible = 0;
  d->logo_x = d->logo_y = 0;
  d->logo_main = 15;
  d->logo_accent = 3;
  /* Preserve host-provided immutable logo artwork through application reset. */
  d->fps = 50;d->fps_phase=0;
  d->egg_input = 0;
  /* Normal renderer enabled after reset; explicit opt-out remains available. */
  d->render_enabled = 1;
  blank_entries(d);
  d->foreground = 0;
  clear_sessions(d);
}
twodfx_emu *twodfx_emu_create(twodfx_machine m) {
  twodfx_emu *d;
  if (m != TWODFX_EMU_ENTERPRISE && m != TWODFX_EMU_TVC) return NULL;
  d = (twodfx_emu *)calloc(1, sizeof *d);
  if (!d) return NULL;
  d->ram = (uint8_t *)calloc(TWODFX_EMU_RESOURCE_BYTES, 1);
  if (!d->ram) { free(d); return NULL; }
  d->frame_colour = (uint8_t *)calloc(TWODFX_FRAME_PIXELS, 1);
  d->frame_coverage = (uint8_t *)calloc(TWODFX_FRAME_PIXELS, 1);
  if (!d->frame_colour || !d->frame_coverage) {
    free(d->frame_colour); free(d->frame_coverage); free(d->ram); free(d); return NULL;
  }
  d->machine = m;
  if(!demo_states_reset(d->demo_states)){twodfx_emu_destroy(d);return NULL;}
  twodfx_emu_power_cycle(d);
  if (!twodfx_emu_set_logo_art(d, two_dfx_builtin_logo_2bpp,
                               TWO_DFX_BUILTIN_LOGO_PACKED_BYTES)) {
    twodfx_emu_destroy(d);
    return NULL;
  }
  return d;
}
int twodfx_emu_set_logo_art(twodfx_emu *d,const void *packed,size_t nbytes) {
  const uint8_t *b=(const uint8_t *)packed;
  size_t i;
  if(!d || !b || nbytes!=TWODFX_EMU_LOGO_BYTES)return 0;
  for(i=0;i<TWODFX_EMU_LOGO_BYTES;i++) {
    uint8_t v=b[i];
    if((v & (uint8_t)(v>>1) & 0x55u)!=0)return 0; /* any 2-bit field 11 */
  }
  memcpy(d->logo_art,b,TWODFX_EMU_LOGO_BYTES);
  d->logo_art_loaded=1;
  return 1;
}
void twodfx_emu_clear_logo_art(twodfx_emu *d) {
  if(d) (void)twodfx_emu_set_logo_art(d, two_dfx_builtin_logo_2bpp,
                                      TWO_DFX_BUILTIN_LOGO_PACKED_BYTES);
}
int twodfx_emu_logo_art_loaded(const twodfx_emu *d) {
  return d && d->logo_art_loaded;
}

void twodfx_emu_destroy(twodfx_emu *d) {
  if (d) { free(d->ram); free(d->frame_colour); free(d->frame_coverage); free(d); }
}
void twodfx_emu_power_cycle(twodfx_emu *d) {
  if (!d) return;
  demo_states_reset(d->demo_states);
  d->started = d->awake = d->terminal = 0;
  d->engine = 0;
  d->video_frames = 0;
  clear_application(d);
}
static unsigned twod_args(uint8_t c) {
  /* Argument count includes slot ID. See v1.1.4 2DPT wire contract. */
  switch (c) {
    case 0x60: case 0x7f: return 1;  /* NOP / RET */
    case 0x61: case 0x6f: return 2;  /* SET_COLOR / SKIP_NEXT */
    case 0x6e: return 3;             /* SET_XY_OFFSET */
    case 0x62: case 0x65: return 4;  /* PLOT / BUCKET_FILL */
    case 0x63: case 0x64: case 0x66: case 0x67:
    case 0x6a: case 0x6b: case 0x74: case 0x79: return 6;
    case 0x68: case 0x69: case 0x6c: case 0x6d:
    case 0x70: case 0x72: case 0x73: return 8;
    case 0x71: return 5;              /* BLIT */
    case 0x75: case 0x76: return 9;  /* TRIANGLE */
    case 0x77: return 10;            /* CROP_BLIT */
    case 0x78: case 0x7a: case 0x7b: case 0x7c: return 12;
    default: return 0;
  }
}
static unsigned ctrl_args(uint8_t c) {
  switch (c) {
    case 0x50: case 0x51: case 0x52: return 1;
    case 0x54: case 0x55: case 0x56: case 0x57: return 2;
    case 0x5b: return 5;
    case 0x5c: case 0x5f: case 0x86: return 7;
    case 0x5d: case 0x5e: case 0x90: return 4;
    case 0x87: return 8;
    case 0x88: return 10;
    case 0x92: case 0x94: return 2;
    case 0x93: return 3;
    case 0x95: return 5;
    default: return 0;
  }
}
static void begin_ctrl(twodfx_emu *d, uint8_t cmd) {
  d->param_needed = (uint8_t)ctrl_args(cmd);
  if (!d->param_needed) return;
  d->session = SESSION_CTRL;
  d->opcode = cmd;
  d->param_n = 0;
}
void twodfx_emu_write_f8(twodfx_emu *d, uint8_t c) {
  if (!d || d->terminal) return;
  /* SYS_RESET is the exception to strict upload/UNZX sessions. */
  if (d->awake && c == 0x8f) {
    uint8_t mode = d->engine;
    clear_application(d);  /* Synchronous guest SYS_RESET of the logical device state. */
    d->engine = mode;
    return;
  }
  if (!d->awake) {
    if (c == 0x50) { clear_sessions(d); d->session = SESSION_CTRL; d->opcode = c; d->param_needed = 1; }
    else { clear_sessions(d); }
    return;
  }
  if (d->session == SESSION_UPLOAD || d->session == SESSION_UNZX) return;
  clear_sessions(d);
  if (c < 0x40) { d->session = SESSION_SPRITE; d->spb_id = c; return; }
  if (c < 0x50) { d->transparent = (uint8_t)(c & 15u); return; }
  if (c == 0x53) { d->overrun_pending = 0; return; }
  if (c == 0x58) { d->raster_pending = 0; return; }
  if ((c >= 0x50 && c <= 0x5f) || (c >= 0x86 && c <= 0x88) || c == 0x90 || (c>=0x92 && c<=0x95)) {
    begin_ctrl(d, c); return;
  }
  if (c >= 0x60 && c <= 0x7f) {
    d->foreground = 0;
    d->param_needed = (uint8_t)twod_args(c);
    if (d->param_needed) { d->session = SESSION_2DPT; d->opcode = c; }
    return;
  }
  if (c >= 0xe0) {
    d->param_needed = (uint8_t)twod_args((uint8_t)(c & 0x7f));
    if (d->param_needed) { d->session = SESSION_2DPT; d->opcode = (uint8_t)(c & 0x7f); d->foreground = 1; }
    return;
  }
  if (c >= 0x80 && c <= 0x82) { d->session = SESSION_UPLOAD; d->upload_convert = (uint8_t)(c - 0x80); return; }
  if (c >= 0x83 && c <= 0x85) { d->session = SESSION_UNZX; d->opcode = c; d->param_needed = 6; return; }
  if (c == 0x8e) { egg_stop(d); clear_resource_jobs(d); d->terminal = 1; return; } /* render later */
  if (c == 0x91) { d->logo_visible = 0; return; }
  if(c>=0xc0u && c<=0xdfu) {egg_select(d,c&31u);return;}
}
static void ctrl_commit(twodfx_emu *d) {
  uint8_t c = d->opcode;
  memcpy(d->ctrl_raw[c], d->params, d->param_n);
  d->ctrl_size[c] = d->param_n;
  if (c == 0x50) {
    d->engine = d->params[0] == 0 ? TWODFX_EMU_ENGINE_OFF :
                (d->params[0] == 255 ? TWODFX_EMU_ENGINE_FORCED : TWODFX_EMU_ENGINE_NORMAL);
    if (d->params[0]) d->awake = 1; /* Wake completes on the control write. */
  } else if (c == 0x51) {d->fps = d->params[0]==1 ? 25 : 50;d->fps_phase=0;}
  else if (c == 0x52) d->overrun_visual = d->params[0] != 0;
  else if (c >= 0x54 && c <= 0x57) {
    unsigned i = c - 0x54;
    uint16_t y = (uint16_t)(((uint16_t)d->params[0] << 8) | d->params[1]);
    d->raster_lines[i] = y <= 311 ? y : 0;
    d->raster_pending &= (uint8_t)~(1u << i);
  } else if (c == 0x90) {
    d->logo_x = (uint16_t)((((uint16_t)(d->params[0] >> 1) & 3u) << 8) | d->params[1]);
    d->logo_y = (uint16_t)((((uint16_t)d->params[0] & 1u) << 8) | d->params[2]);
    d->logo_main = d->machine == TWODFX_EMU_TVC ? 15u : (uint8_t)(d->params[3] >> 4);
    d->logo_accent = d->machine == TWODFX_EMU_TVC ? 3u : (uint8_t)(d->params[3] & 15u);
    d->logo_visible = 1;d->logo_small=(d->params[0]>>7)&1;
  }
  else if(c==0x92)pointer_mode(d,d->params[0],d->params[1]);
  else if(c==0x93)pointer_pos(d,((d->params[0]>>1)&3)*256+d->params[1],(d->params[0]&1)*256+d->params[2]);
  else if(c==0x94)pointer_pos(d,d->pointer_shadow.x+(int8_t)d->params[0],d->pointer_shadow.y+(int8_t)d->params[1]);
  else if(c==0x95)pointer_define(d,addr23(d->params),d->params[3],d->params[4]);
  else if (c == 0x5b) {
    /* Public firmware v1.1.4 DEFINE_FONT command encoding. */
    twodfx_font_descriptor *f = &d->fonts[d->params[0] & 63u];
    f->resource_address = addr23(d->params+1);
    f->width_units = (uint8_t)(((d->params[4] >> 4) & 15u) + 1u);
    f->height_units = (uint8_t)((d->params[4] & 15u) + 1u);
    f->valid = 1;
  } else if (c == 0x5c) {
    twodfx_bitmap_descriptor *b = &d->bitmaps[d->params[0]];
    b->resource_address = addr23(d->params+1);
    b->width = (uint16_t)(((uint16_t)((d->params[4] >> 1) & 3u) << 8) | d->params[5]);
    b->height = (uint16_t)(((uint16_t)(d->params[4] & 1u) << 8) | d->params[6]);
    b->valid = b->width != 0 && b->height != 0;
  } else if (c == 0x5d || c == 0x5e) {
    twodfx_address_descriptor *a = c == 0x5d ? &d->patterns[d->params[0] & 63u]
                                             : &d->tilesets[d->params[0] & 63u];
    a->resource_address = addr23(d->params+1);
    a->valid = 1;
  } else if (c == 0x5f) {
    twodfx_tilemap_descriptor *t = &d->tilemaps[d->params[0] & 31u];
    t->resource_address = addr23(d->params+1);
    t->width_cells = (uint16_t)d->params[4] + 1u;
    t->height_cells = (uint16_t)d->params[5] + 1u;
    t->tileset = d->params[6] & 63u;
    t->valid = 1;
  }
  else if (c == 0x86) {
    unsigned nv=(unsigned)d->params[4]+1u;
    unsigned nt=1u+(((unsigned)d->params[5]&1u)<<8)+(unsigned)d->params[6];
    twodfx_3d_define(&d->scene3d,d->params[0],addr23(d->params+1),nv,nt);
  } else if (c == 0x87) {
    twodfx_3d_scene_set_projection(&d->scene3d,
      (uint16_t)((unsigned)d->params[0]|((unsigned)d->params[1]<<8)),
      (uint16_t)((unsigned)d->params[2]|((unsigned)d->params[3]<<8)),
      (uint16_t)((unsigned)d->params[4]|((unsigned)d->params[5]<<8)),
      (uint16_t)((unsigned)d->params[6]|((unsigned)d->params[7]<<8)));
  } else if (c == 0x88) {
    twodfx_3d_scene_set_camera(&d->scene3d,d->params);
  }
}
void twodfx_emu_write_f9(twodfx_emu *d, uint8_t value) {
  if (!d || d->terminal) return;
  if (d->session == SESSION_UPLOAD) {
    if (d->upload_header_n < 5) {
      d->upload_header[d->upload_header_n++] = value;
      if (d->upload_header_n == 5) {
        d->upload_addr = addr23(d->upload_header);
        d->upload_remain = (uint32_t)d->upload_header[3] | ((uint32_t)d->upload_header[4] << 8);
        if (!d->upload_remain) d->upload_remain = 65536;
      }
    } else if (d->upload_remain) {
      d->ram[d->upload_addr & (TWODFX_EMU_RESOURCE_BYTES-1)] = source_to_internal(value, d->upload_convert);
      d->upload_addr++;
      if (!--d->upload_remain) clear_sessions(d);
    }
    return;
  }
  if (d->session == SESSION_UNZX) {
    if (d->param_n < 6) d->params[d->param_n++] = value;
    if (d->param_n == 6) {
      (void)queue_resource_job(d, addr23(d->params), addr23(d->params+3),
                               (uint8_t)(d->opcode - 0x83));
      clear_sessions(d);
    }
    return;
  }
  if (d->session == SESSION_SPRITE) {
    if (d->spb_offset < 9) d->shadow_spt[d->spb_id][d->spb_offset++] = value;
    return;
  }
  if (d->session == SESSION_2DPT) {
    d->params[d->param_n++] = value;
    if (d->param_n == d->param_needed) {
      unsigned slot = d->params[0];
      twodfx_2dpt_entry *e = &d->shadow_2dpt[d->foreground][slot];
      memset(e, 0, sizeof *e);
      e->opcode = d->opcode;
      e->nbytes = (uint8_t)(d->param_n - 1);
      memcpy(e->payload, d->params + 1, e->nbytes);
      clear_sessions(d);
    }
    return;
  }
  if (d->session == SESSION_CTRL) {
    d->params[d->param_n++] = value;
    if (d->param_n == d->param_needed) {
      ctrl_commit(d);
      clear_sessions(d);
    }
    return;
  }
  if (d->awake) {
    d->egg_input=value;
    if(d->egg_id!=0xffu) egg_enqueue(d,value);
  }
}
int twodfx_emu_egg_active(const twodfx_emu *d) {return d && d->egg_id!=0xffu;}
unsigned twodfx_emu_egg_id(const twodfx_emu *d) {return d ? d->egg_id : 255u;}
int twodfx_emu_egg_start_pending(const twodfx_emu *d) {return d && d->egg_pending;}
int twodfx_emu_egg_pop_input(twodfx_emu *d,uint8_t *out) {
  if(!d || !out || d->egg_pending || d->egg_id==0xffu ||
     d->egg_head==d->egg_tail)return 0;
  *out=d->egg_fifo[d->egg_tail];
  d->egg_tail=(uint8_t)((d->egg_tail+1u)&63u);
  return 1;
}
int twodfx_emu_egg_input_overflow(const twodfx_emu *d) {
  return d && d->egg_overflow;
}
void twodfx_emu_egg_clear_input_overflow(twodfx_emu *d) {
  if(d)d->egg_overflow=0;
}
uint8_t twodfx_emu_read_f8(const twodfx_emu *d) {
  uint8_t status = 0xff;
  if (!d) return 0xff;
  if (d->awake) status &= (uint8_t)~(1u << 6);
  status &= (uint8_t)~((uint8_t)(d->raster_pending & 15u) << 1);
  /* Sticky queue overflow shares active-low STATUS5 with render overrun.
     Execution-duration overruns are not synthesized by this device model. */
  if (d->overrun_pending) status &= (uint8_t)~0x20u;
  if (d->collision_reported) status &= (uint8_t)~0x80u;
  return status;
}
void twodfx_emu_hsync(twodfx_emu *d, uint16_t y) {
  unsigned i;
  if (!d || !d->awake || d->terminal) return;
  service_resource_jobs(d);
  for (i = 0; i < 4; i++) if (d->raster_lines[i] && d->raster_lines[i] == y)
    d->raster_pending |= (uint8_t)(1u << i);
}

static unsigned hi_x(uint8_t high, uint8_t low) {
  return ((unsigned)((high >> 4) & 3u) << 8) | low;
}
static unsigned hi_y(uint8_t high, uint8_t low) {
  return ((unsigned)((high >> 3) & 1u) << 8) | low;
}
static unsigned hi_w(uint8_t high, uint8_t low) {
  return ((unsigned)((high >> 1) & 3u) << 8) | low;
}
static unsigned hi_h(uint8_t high, uint8_t low) {
  return ((unsigned)(high & 1u) << 8) | low;
}
static uint8_t resource_pixel(const twodfx_emu *d,
                              const twodfx_bitmap_descriptor *b,
                              unsigned sx, unsigned sy) {
  uint32_t rowbytes, address;
  uint8_t packed;
  /* Caller validates descriptor and source rectangle. */
  rowbytes = ((uint32_t)b->width + 1u) / 2u;
  address = (b->resource_address + (uint32_t)sy * rowbytes + sx / 2u) &
            (TWODFX_EMU_RESOURCE_BYTES - 1u);
  packed = d->ram[address];
  return (uint8_t)((sx & 1u) ? (packed >> 4) : (packed & 15u));
}
static void resource_bitmap(twodfx_emu *d, twodfx_raster *r,
                            unsigned id, int dx, int dy,
                            unsigned sx, unsigned sy, unsigned sw, unsigned sh,
                            unsigned dw, unsigned dh, int use_key) {
  const twodfx_bitmap_descriptor *b;
  int x0, y0, x1, y1, x, y;
  if (id >= 256u || !sw || !sh || !dw || !dh) return;
  b = &d->active_bitmaps[id];
  if (!b->valid || !b->width || !b->height) return;
  x0 = dx > r->x0 ? dx : r->x0;
  y0 = dy > r->y0 ? dy : r->y0;
  x1 = dx + (int)dw - 1; if (x1 > r->x1) x1 = r->x1;
  y1 = dy + (int)dh - 1; if (y1 > r->y1) y1 = r->y1;
  for (y = y0; y <= y1; ++y) {
    uint32_t source_y = sy + (uint32_t)(y - dy) * sh / dh;
    if (source_y >= b->height) continue;
    for (x = x0; x <= x1; ++x) {
      uint32_t source_x = sx + (uint32_t)(x - dx) * sw / dw;
      uint8_t c;
      if (source_x >= b->width) continue;
      c = resource_pixel(d, b, source_x, source_y);
      if (!use_key || c != d->transparent) twodfx_raster_plot(r,x,y,c);
    }
  }
}
#include "twodfx_geometry.inc"
#include "twodfx_2d_extra.inc"
static void replay_2d(twodfx_emu *d,twodfx_raster *r,unsigned layer) {
  unsigned slot;
  uint8_t fg=15u,bg=d->transparent;
  r->key=d->transparent;
  /* v1.1.4 2D coordinates are raw scanout coordinates; the raster is
     indexed only over the stored window.  TVC stores x=0..703 while EP
     stores raw x=88..831.  Both store rows starting at profile first Y. */
  const int raw_origin_x = d->machine == TWODFX_EMU_ENTERPRISE ? 88 : 0;
  const int raw_origin_y = d->machine == TWODFX_EMU_ENTERPRISE ? 16 : 41;
  int ox=-raw_origin_x,oy=-raw_origin_y;
  twodfx_raster_clip(r,0,0,(int)r->width-1,(int)r->height-1);
  for(slot=0;slot<256u;slot++) {
    const twodfx_2dpt_entry *e=&d->active_2dpt[layer][slot];
    const uint8_t *p=e->payload;
    unsigned x,y,w,h,stroke;
    int px,py;
    if(e->opcode==0x7fu)break;
    switch(e->opcode) {
      case 0x7a: case 0x7b: case 0x7c:
        if(e->nbytes==11u) {
          /* Optical centre comes from the selected physical visible region,
             never from the command offset or current clipping rectangle. */
          int cx=d->machine==TWODFX_EMU_TVC?(79+686)/2:(88+811)/2;
          int cy=d->machine==TWODFX_EMU_TVC?(41+296)/2:(18+305)/2;
          twodfx_3d_render(&d->scene3d,d->ram,r,p,
            (unsigned)(e->opcode-0x7au),fg,
            ox+raw_origin_x,oy+raw_origin_y,
            raw_origin_x,raw_origin_y,cx,cy);
        }
        break;
      case 0x60: break;
      case 0x61:
        if(e->nbytes==1u){ fg=(uint8_t)((p[0]>>4)&15u);bg=(uint8_t)(p[0]&15u); }
        break;
      case 0x6e:
        if(e->nbytes==2u){ox=(int8_t)p[0]-raw_origin_x;oy=(int8_t)p[1]-raw_origin_y;}
        break;
      case 0x6f:
        if(e->nbytes==1u)slot+=(unsigned)p[0];
        break;
      case 0x79:
        if(e->nbytes==5u) {
          x=hi_x(p[0],p[1]);y=hi_y(p[0],p[2]);
          w=hi_w(p[0],p[3]);h=hi_h(p[0],p[4]);
          twodfx_raster_clip(r,(int)x-raw_origin_x,(int)y-raw_origin_y,
                               (int)x-raw_origin_x+(int)w-1,
                               (int)y-raw_origin_y+(int)h-1);
        }
        break;
      case 0x62: case 0x65:
        if(e->nbytes==3u){
          px=(int)hi_x(p[0],p[1])+ox;
          py=(int)hi_y(p[0],p[2])+oy;
          if(e->opcode==0x62)twodfx_raster_plot(r,px,py,fg);
          else twodfx_raster_flood(r,px,py,fg);
        }
        break;
      case 0x63: case 0x64: case 0x66: case 0x67:
      case 0x6a: case 0x6b:
        if(e->nbytes != 5u)break;
        x=hi_x(p[0],p[1]);y=hi_y(p[0],p[2]);
        w=hi_w(p[0],p[3]);h=hi_h(p[0],p[4]);
        stroke=((unsigned)p[0]>>6)+1u;
        px=(int)x+ox;py=(int)y+oy;
        if(e->opcode==0x63 || e->opcode==0x64)
          gfx2d_draw_line_raw(r,px,py,(int)w+ox,(int)h+oy,stroke,
                      e->opcode==0x63?fg:bg);
        else if(e->opcode==0x67 && w && h)
          twodfx_raster_fill_rect(r,px,py,px+(int)w-1,py+(int)h-1,fg);
        else if(e->opcode==0x66)
          gfx2d_draw_rect_raw(r,px,py,w,h,stroke,fg,bg);
        else if (e->opcode==0x6a || e->opcode==0x6b)
          raster_ext_ellipse(r,px,py,w,h,stroke,fg,bg,e->opcode==0x6b,ox+raw_origin_x,oy+raw_origin_y,raw_origin_x,raw_origin_y);
        break;
      case 0x68: case 0x69: case 0x6c: case 0x6d:
      case 0x70: case 0x72: case 0x73:
        if(e->nbytes!=7u)break;
        if(e->opcode==0x73) {
          unsigned map=(unsigned)p[0]>>3;
          px=(int)((((unsigned)p[0]>>1)&3u)<<8 | p[1])+ox;
          py=(int)(((unsigned)(p[0]&1u)<<8)|p[2])+oy;
          raster_ext_tilemap(d,r,px+raw_origin_x,py+raw_origin_y,map,
                      p[3],p[4],p[5],p[6],raw_origin_x,raw_origin_y);
          break;
        }
        px=(int)hi_x(p[0],p[1])+ox;py=(int)hi_y(p[0],p[2])+oy;
        if(e->opcode==0x70) {
          uint32_t address=addr23(p+4);
          raster_ext_text(d,r,px,py,p[3],address,fg,bg);
          break;
        }
        w=hi_w(p[0],p[3]);h=hi_h(p[0],p[4]);
        stroke=((unsigned)p[0]>>6)+1u;
        if(e->opcode==0x68 || e->opcode==0x69)
          raster_ext_round(r,px,py,w,h,p[5],p[6],stroke,fg,bg,e->opcode==0x69);
        else if(e->opcode==0x6c || e->opcode==0x6d)
          raster_ext_arc_pie(r,px,py,w,h,p[5],p[6],stroke,fg,e->opcode==0x6d,ox+raw_origin_x,oy+raw_origin_y,raw_origin_x,raw_origin_y);
        else if(e->opcode==0x72)
          raster_ext_pattern_fill(d,r,px+raw_origin_x,py+raw_origin_y,w,h,(p[5]&63)|(p[0]&192),p[6],
                           raw_origin_x,raw_origin_y);
        break;
      case 0x74:
        if(e->nbytes==5u){
          px=(int)hi_x(p[0],p[1])+ox;py=(int)hi_y(p[0],p[2])+oy;
          raster_ext_pattern_bucket(d,r,px,py,(p[3]&63)|(p[0]&192),p[4],raw_origin_x,raw_origin_y);
        }
        break;
      case 0x75: case 0x76:
        if(e->nbytes==8u){
          int x0=(int)hi_x(p[0],p[1])+ox, y0=(int)hi_y(p[0],p[2])+oy;
          int x1=(int)hi_w(p[0],p[3])+ox, y1=(int)hi_h(p[0],p[4])+oy;
          int x2=(int)hi_x(p[5],p[6])+ox, y2=(int)hi_y(p[5],p[7])+oy;
          if(e->opcode==0x75){
            if(bg!=d->transparent)twodfx_raster_fill_triangle_sample(r,x0,y0,x1,y1,x2,y2,bg,0);
            stroke=(p[0]>>6)+1;
            gfx2d_triangle_line_raw(r,x0,y0,x1,y1,stroke,fg);
            gfx2d_triangle_line_raw(r,x1,y1,x2,y2,stroke,fg);
            gfx2d_triangle_line_raw(r,x2,y2,x0,y0,stroke,fg);
          }
          else twodfx_raster_fill_triangle(r,x0,y0,x1,y1,x2,y2,fg);
        }
        break;
      case 0x71:
        if(e->nbytes==4u){
          const twodfx_bitmap_descriptor *b=&d->active_bitmaps[p[3]];
          resource_bitmap(d,r,p[3],(int)hi_x(p[0],p[1])+ox,
             (int)hi_y(p[0],p[2])+oy,0,0,b->width,b->height,
             b->width,b->height,(p[0]&0x80u)!=0u);
        }
        break;
      case 0x77: case 0x78:
        if(e->nbytes==(e->opcode==0x77?9u:11u)){
          unsigned sx=hi_x(p[0],p[1]),sy=hi_y(p[0],p[2]);
          unsigned sw=hi_w(p[0],p[3]),sh=hi_h(p[0],p[4]);
          unsigned dx=hi_x(p[5],p[6]),dy=hi_y(p[5],p[7]);
          unsigned dw=e->opcode==0x77?sw:hi_w(p[5],p[8]);
          unsigned dh=e->opcode==0x77?sh:hi_h(p[5],p[9]);
          unsigned bitmap=e->opcode==0x77?p[8]:p[10];
          resource_bitmap(d,r,bitmap,(int)dx+ox,(int)dy+oy,
                          sx,sy,sw,sh,dw,dh,(p[0]&0x80u)!=0u);
        }
        break;
      default: break;
    }
  }
}
/* Sprite raster/collision model: independent C99 behavioural implementation.
 * Source pixels are packed low-nibble-first, with ceil(width/2) stride.
 * Positions on the wire are absolute scanout coordinates. The crop window is
 * the *stored framebuffer*, which differs from the visible TVC window. */
typedef struct {
  uint32_t addr;
  int x, y, x_end, y_end, raw_x, raw_y;
  unsigned width, height, out_width, out_height;
  unsigned stride;
  uint8_t enabled, collision, flip_x, flip_y, double_x, double_y;
} sprite_view;
static sprite_view sprite_decode(const twodfx_emu *d, const uint8_t spb[9]) {
  sprite_view v;
  unsigned x, y, w, h;
  const unsigned first_x = d->machine == TWODFX_EMU_ENTERPRISE ? 88u : 0u;
  const unsigned first_y = d->machine == TWODFX_EMU_ENTERPRISE ? 16u : 41u;
  const int max_x = (int)twodfx_emu_framebuffer_width(d);
  const int max_y = (int)twodfx_emu_framebuffer_height(d);
  memset(&v, 0, sizeof v);
  x = ((unsigned)(spb[0] & 0x30u) << 4) | spb[1];
  y = ((unsigned)(spb[0] & 0x08u) << 5) | spb[2];
  w = ((unsigned)(spb[0] & 0x06u) << 7) | spb[7];
  h = ((unsigned)(spb[0] & 0x01u) << 8) | spb[8];
  v.collision = (uint8_t)((spb[0] & 0x40u) != 0u);
  v.double_x = (uint8_t)((spb[6] & 0x80u) != 0u);
  v.double_y = (uint8_t)((spb[6] & 0x40u) != 0u);
  v.flip_x = (uint8_t)((spb[6] & 0x20u) != 0u);
  v.flip_y = (uint8_t)((spb[6] & 0x10u) != 0u);
  if (!(spb[0] & 0x80u) || !w || !h) return v;
  v.width = w;
  v.height = h;
  v.stride = (w + 1u) >> 1;
  v.addr = ((uint32_t)(spb[5] & 0x7fu) << 16) |
           ((uint32_t)spb[4] << 8) | spb[3];
  v.out_width = w << v.double_x;
  v.out_height = h << v.double_y;
  v.x = v.raw_x = (int)x - (int)first_x;
  v.y = v.raw_y = (int)y - (int)first_y;
  v.x_end = v.x + (int)v.out_width;
  v.y_end = v.y + (int)v.out_height;
  /* Intersect only the stored framebuffer. Offscreen TVC pixels remain live. */
  if (v.x < 0) v.x = 0;
  if (v.y < 0) v.y = 0;
  if (v.x_end > max_x) v.x_end = max_x;
  if (v.y_end > max_y) v.y_end = max_y;
  v.enabled = (uint8_t)(v.x < v.x_end && v.y < v.y_end);
  return v;
}
/* Recover wire-origin pixel for a clipped destination coordinate. */
static uint8_t sprite_colour(const twodfx_emu *d, const sprite_view *v,
                             int x, int y) {
  unsigned original_x = (unsigned)(x - v->raw_x);
  unsigned original_y = (unsigned)(y - v->raw_y);
  unsigned sx = original_x >> v->double_x;
  unsigned sy = original_y >> v->double_y;
  uint32_t a;
  uint8_t b;
  if (sx >= v->width || sy >= v->height) return d->transparent;
  if (v->flip_x) sx = v->width - 1u - sx;
  if (v->flip_y) sy = v->height - 1u - sy;
  a = (v->addr + sy * v->stride + (sx >> 1)) & (TWODFX_EMU_RESOURCE_BYTES - 1u);
  b = d->ram[a];
  return (uint8_t)((sx & 1u) ? b >> 4 : b & 0x0fu);
}
static void sprite_paint(twodfx_emu *d, twodfx_raster *r,
                         const sprite_view *v) {
  int x,y;
  if (!v->enabled) return;
  for (y = v->y; y < v->y_end; ++y)
    for (x = v->x; x < v->x_end; ++x) {
      uint8_t c = sprite_colour(d,v,x,y);
      if (c != d->transparent) twodfx_raster_plot(r,x,y,c);
    }
}
/* Collision is a two-horizontal-pixel cell approximation, not a per-pixel
 * result. A pair must FIRST have a genuine geometric bbox overlap; cells
 * merely touching with disjoint 1px boxes are not counted as collisions. */
static uint8_t sprite_cell_nontransparent(const twodfx_emu *d,
                                           const sprite_view *v,
                                           int x, int y) {
  if (y < v->y || y >= v->y_end) return 0;
  if (x >= v->x && x < v->x_end && sprite_colour(d,v,x,y) != d->transparent)
    return 1;
  if (x+1 >= v->x && x+1 < v->x_end &&
      sprite_colour(d,v,x+1,y) != d->transparent) return 1;
  return 0;
}
static uint8_t detect_collision(const twodfx_emu *d) {
  sprite_view views[64];
  unsigned i,j;
  for (i=0; i<64u; ++i) views[i]=sprite_decode(d,d->active_spt[i]);
  for (i=0; i<64u; ++i) {
    const sprite_view *a=&views[i];
    if (!a->enabled || !a->collision) continue;
    for (j=i+1u; j<64u; ++j) {
      const sprite_view *b=&views[j];
      int xmin,xmax,ymin,ymax,x,y;
      if (!b->enabled || !b->collision) continue;
      xmin=a->x>b->x?a->x:b->x;
      xmax=a->x_end<b->x_end?a->x_end:b->x_end;
      ymin=a->y>b->y?a->y:b->y;
      ymax=a->y_end<b->y_end?a->y_end:b->y_end;
      if (xmin>=xmax || ymin>=ymax) continue;
      for (y=ymin; y<ymax; ++y)
        for (x=xmin & ~1; x<xmax; x+=2)
          if (sprite_cell_nontransparent(d,a,x,y) &&
              sprite_cell_nontransparent(d,b,x,y)) return 1;
    }
  }
  return 0;
}
static void sprite_layer(twodfx_emu *d,twodfx_raster *r) {
  unsigned i;
  for(i=0;i<64u;++i) {
    sprite_view v=sprite_decode(d,d->active_spt[i]);
    sprite_paint(d,r,&v);
  }
}

static void draw_builtin_logo(twodfx_emu *d,twodfx_raster *r) {
  int ox=d->machine==TWODFX_EMU_ENTERPRISE?88:0;
  int oy=d->machine==TWODFX_EMU_ENTERPRISE?16:41;
  unsigned ly,lx,w=d->logo_small?103:206,h=d->logo_small?40:79,stride=d->logo_small?26:52;
  const uint8_t*art=d->logo_small?two_dfx_builtin_logo_small_2bpp:d->logo_art;
  if(!d->logo_visible || !d->logo_art_loaded)return;
  twodfx_raster_clip(r,0,0,(int)r->width-1,(int)r->height-1);
  for(ly=0;ly<h;ly++) {
    int y=(int)d->logo_y+(int)ly-oy;
    if(y<0 || y>=(int)r->height)continue;
    for(lx=0;lx<w;lx++) {
      uint8_t v=art[ly*stride+(lx>>2)];
      unsigned colour=(unsigned)((v>>((lx&3u)*2u))&3u);
      int x=(int)d->logo_x+(int)lx-ox;
      if(colour && x>=0 && x<(int)r->width)
        twodfx_raster_plot(r,x,y,(uint8_t)(colour==1u?d->logo_main:d->logo_accent));
    }
  }
}

static void render_frame(twodfx_emu *d) {
  twodfx_raster r;
  unsigned width = d->machine == TWODFX_EMU_TVC ? TWODFX_EMU_TVC_VIDEO_W : TWODFX_EMU_VIDEO_W_MAX;
  unsigned height = d->machine == TWODFX_EMU_TVC ? TWODFX_EMU_TVC_VIDEO_H : TWODFX_EMU_VIDEO_H_MAX;
  memset(d->frame_colour,0,TWODFX_FRAME_PIXELS);
  memset(d->frame_coverage,0,TWODFX_FRAME_PIXELS);
  if (!twodfx_raster_bind(&r,d->frame_colour,d->frame_coverage,width,height))return;
  twodfx_raster_clear(&r);
  memset(d->frame_colour,d->transparent,(size_t)width*height);
  if (!d->render_enabled) return;
  replay_2d(d,&r,0u);
  /* BG clip is a playlist-local state; it must not clip the sprite layer. */
  twodfx_raster_clip(&r,0,0,(int)width-1,(int)height-1);
  sprite_layer(d,&r);
  replay_2d(d,&r,1u);
  draw_builtin_logo(d,&r);
  pointer_draw(d,&r);
  if (d->overrun_visual && d->overrun_pending) {
    int x=(int)width/2-20, y=((int)height-170)/2;
    uint8_t colour=d->overrun_phase;
    d->overrun_phase=(uint8_t)((colour+1u)&15u);
    twodfx_raster_clip(&r,0,0,(int)width-1,(int)height-1);
    twodfx_raster_fill_rect(&r,x,y,x+39,y+129,colour);
    raster_ext_ellipse(&r,x,y+150,40,20,1,colour,colour,1,0,0,0,0);
  }
}
#include "twodfx_public_api_adapter.inc"
#include "twodfx_source_eggs.inc"
void twodfx_emu_set_render_enabled(twodfx_emu *d, int enabled) {
  if(d) d->render_enabled = (uint8_t)(enabled!=0);
}
int twodfx_emu_render_enabled(const twodfx_emu *d) {
  return d ? d->render_enabled : 0;
}
void twodfx_emu_vsync(twodfx_emu *d) {
  if (!d) return;
  d->video_frames++;
  if (!d->awake) return;
  if (d->terminal == 1u) {
    d->overrun_pending=d->overrun_phase=d->overrun_visual=0;
    pointer_reset(d,1);d->logo_visible=0;d->transparent=0;d->render_enabled=1;
    egg_api_bind(d);
    twodfx_firmware_page_build_shadow_scene("1.1.4");
    egg_api_bind(NULL);
    memcpy(d->active_spt,d->shadow_spt,sizeof d->active_spt);
    memcpy(d->active_2dpt,d->shadow_2dpt,sizeof d->active_2dpt);
    memcpy(d->active_fonts,d->fonts,sizeof d->active_fonts);
    memcpy(d->active_bitmaps,d->bitmaps,sizeof d->active_bitmaps);
    memcpy(d->active_patterns,d->patterns,sizeof d->active_patterns);
    memcpy(d->active_tilesets,d->tilesets,sizeof d->active_tilesets);
    memcpy(d->active_tilemaps,d->tilemaps,sizeof d->active_tilemaps);
    render_frame(d);
    d->engine=TWODFX_EMU_ENGINE_FORCED;
    d->transparent=0;
    d->fps=50;
    d->logo_visible=0;
    d->collision_pending=d->collision_ready=d->collision_reported=0;
    d->raster_pending=0;
    d->terminal=2u;  /* completed terminal framebuffer */
    return;
  }
  if (d->terminal) return;
  /* Collision is reported only after the previous completed scene becomes
     observable at the following VSYNC. 25fps skipped frames retain it. */
  if (d->collision_ready) {
    d->collision_reported = d->collision_pending;
    d->collision_ready = 0;
  }
  if (d->resource_job == TWODFX_RESOURCE_JOB_BUSY) {
    service_resource_jobs(d);
    return; /* no scene publication while resource job runs */
  }
  if(d->engine==TWODFX_EMU_ENGINE_OFF && !d->egg_pending)return;
  if (d->fps == 50 || ((d->fps_phase^=1u)!=0)) {
    /* Start at the accepted scene boundary, not inside the F8h write. */
    /* The reference scheduler runs the start callback instead of tick on
       activation; the first periodic tick is at the NEXT accepted scene. */
    if (d->egg_pending) source_egg_start(d,d->egg_id);
    else if (d->egg_id != 0xffu) source_egg_tick(d,d->egg_id);
    if (d->resource_job == TWODFX_RESOURCE_JOB_BUSY) return;
    memcpy(d->active_spt, d->shadow_spt, sizeof d->active_spt);
    memcpy(d->active_2dpt, d->shadow_2dpt, sizeof d->active_2dpt);
    memcpy(d->active_fonts, d->fonts, sizeof d->active_fonts);
    memcpy(d->active_bitmaps, d->bitmaps, sizeof d->active_bitmaps);
    memcpy(d->active_patterns, d->patterns, sizeof d->active_patterns);
    memcpy(d->active_tilesets, d->tilesets, sizeof d->active_tilesets);
    memcpy(d->active_tilemaps, d->tilemaps, sizeof d->active_tilemaps);
    twodfx_3d_latch(&d->scene3d);
    pointer_latch(d);
    render_frame(d);
    d->collision_pending = detect_collision(d);
    d->collision_ready = 1;
  }
  /* Rendering redraw is frame-latched; no 2dfx render-duration simulation. */
}
int twodfx_emu_irq_asserted(const twodfx_emu *d) { return d && d->raster_pending != 0; }
uint8_t twodfx_emu_raster_pending(const twodfx_emu *d) { return d ? d->raster_pending : 0; }
unsigned twodfx_emu_framebuffer_width(const twodfx_emu *d) {
  return d && d->machine == TWODFX_EMU_TVC ? TWODFX_EMU_TVC_VIDEO_W : TWODFX_EMU_VIDEO_W_MAX;
}
unsigned twodfx_emu_framebuffer_height(const twodfx_emu *d) {
  return d && d->machine == TWODFX_EMU_TVC ? TWODFX_EMU_TVC_VIDEO_H : TWODFX_EMU_VIDEO_H_MAX;
}
size_t twodfx_emu_overlay_line(const twodfx_emu *d, unsigned x, unsigned y,
                                uint8_t *colours, uint8_t *coverage, size_t width) {
  size_t i,count=0;
  unsigned fbwidth=twodfx_emu_framebuffer_width(d);
  unsigned fbheight=twodfx_emu_framebuffer_height(d);
  if(colours)memset(colours,0,width);
  if(coverage)memset(coverage,0,width);
  if(!d || !colours || !coverage ||
     (!d->render_enabled && d->terminal != 2u) ||
     d->engine==TWODFX_EMU_ENGINE_OFF || !d->awake || d->terminal == 1u ||
     x>=fbwidth || y>=fbheight)return 0;
  for(i=0;i<width && i<(size_t)fbwidth-x;i++) {
    size_t offset=(size_t)y*fbwidth+x+i;
    uint8_t c=d->frame_colour[offset];
    /* Physical FORCED /EXTC ignores transparent-key comparator. NORMAL
       selects only drawn non-key pixels. Execution-duration overruns are not simulated. */
    uint8_t active=(uint8_t)(d->engine==TWODFX_EMU_ENGINE_FORCED ||
                            (c!=d->transparent));
    colours[i]=c;
    coverage[i]=active;
    count+=active;
  }
  return count;
}
size_t twodfx_emu_overlay_raw_line(const twodfx_emu *d,unsigned raw_x,
                                   unsigned raw_y,uint8_t *colours,
                                   uint8_t *coverage,size_t width) {
  unsigned ox=d && d->machine==TWODFX_EMU_ENTERPRISE?88u:0u;
  unsigned oy=d && d->machine==TWODFX_EMU_ENTERPRISE?16u:41u;
  size_t shift=0;
  if(colours)memset(colours,0,width);
  if(coverage)memset(coverage,0,width);
  if(!d || !colours || !coverage || raw_y<oy ||
     raw_y-oy>=twodfx_emu_framebuffer_height(d))return 0;
  if(raw_x<ox) {
    shift=(size_t)(ox-raw_x);
    if(shift>=width)return 0;
    raw_x=ox;
  }
  return twodfx_emu_overlay_line(d,raw_x-ox,raw_y-oy,
                                 colours+shift,coverage+shift,width-shift);
}
twodfx_engine_mode twodfx_emu_engine_mode(const twodfx_emu *d) {
  return d ? (twodfx_engine_mode)d->engine : TWODFX_EMU_ENGINE_OFF;
}
twodfx_machine twodfx_emu_machine(const twodfx_emu *d) { return d ? d->machine : TWODFX_EMU_ENTERPRISE; }
uint8_t twodfx_emu_transparent_colour(const twodfx_emu *d) { return d ? d->transparent : 0; }
uint8_t twodfx_emu_fps(const twodfx_emu *d) { return d ? d->fps : 50; }
uint8_t twodfx_emu_resource_byte(const twodfx_emu *d, uint32_t a) {
  return d ? d->ram[a & (TWODFX_EMU_RESOURCE_BYTES-1)] : 0;
}
uint8_t twodfx_emu_sprite_shadow_byte(const twodfx_emu *d, unsigned s, unsigned o) {
  return d && s < 64 && o < 9 ? d->shadow_spt[s][o] : 0;
}
uint8_t twodfx_emu_sprite_active_byte(const twodfx_emu *d, unsigned s, unsigned o) {
  return d && s < 64 && o < 9 ? d->active_spt[s][o] : 0;
}
int twodfx_emu_get_2dpt(const twodfx_emu *d, unsigned fg, unsigned slot, int active, twodfx_2dpt_entry *out) {
  if (!d || !out || fg > 1 || slot > 255) return 0;
  *out = active ? d->active_2dpt[fg][slot] : d->shadow_2dpt[fg][slot];
  return 1;
}
uint64_t twodfx_emu_video_frame_count(const twodfx_emu *d) { return d ? d->video_frames : 0; }
int twodfx_emu_collision_detected(const twodfx_emu *d) { return d && d->collision_reported; }

twodfx_emu_job_status_type twodfx_emu_resource_job_status(const twodfx_emu *d) {
  return d ? d->resource_job : TWODFX_RESOURCE_JOB_NONE;
}
uint32_t twodfx_emu_resource_job_output_bytes(const twodfx_emu *d) {
  return d ? d->job_output_bytes : 0;
}
int twodfx_emu_get_font(const twodfx_emu *d, unsigned i, twodfx_font_descriptor *out) {
  if (!d || !out || i >= 64) return 0;
  *out = d->fonts[i]; return 1;
}
int twodfx_emu_get_bitmap(const twodfx_emu *d, unsigned i, twodfx_bitmap_descriptor *out) {
  if (!d || !out || i >= 256) return 0;
  *out = d->bitmaps[i]; return 1;
}
int twodfx_emu_get_pattern(const twodfx_emu *d, unsigned i, twodfx_address_descriptor *out) {
  if (!d || !out || i >= 64) return 0;
  *out = d->patterns[i]; return 1;
}
int twodfx_emu_get_tileset(const twodfx_emu *d, unsigned i, twodfx_address_descriptor *out) {
  if (!d || !out || i >= 64) return 0;
  *out = d->tilesets[i]; return 1;
}
int twodfx_emu_get_tilemap(const twodfx_emu *d, unsigned i, twodfx_tilemap_descriptor *out) {
  if (!d || !out || i >= 32) return 0;
  *out = d->tilemaps[i]; return 1;
}

/*
 * Stable, endianness-independent snapshot format v15.
 * Deliberately independent of C struct padding and host CPU alignment.
 * The complete device state, including a partially received command or
 * resource job, is recorded. The current version includes renderer/framebuffer
 * state; older formats are rejected rather than reinterpreted.
 */
#define TWODFX_SNAPSHOT_HEADER_BYTES 20u
#define TWODFX_SNAPSHOT_FORMAT_VERSION 15u

typedef struct {
  const uint8_t *reader;
  uint8_t *writer;
  size_t capacity, offset;
  int bad;
} snapshot_io;

static void snap_bytes(snapshot_io *s, void *data, size_t n) {
  if (s->bad) return;
  if (n > s->capacity - s->offset) { s->bad = 1; return; }
  if (s->reader) memcpy(data, s->reader + s->offset, n);
  else if (s->writer) memcpy(s->writer + s->offset, data, n);
  s->offset += n;
}
static void snap_u8(snapshot_io *s, uint8_t *v) {
  snap_bytes(s, v, 1);
}
static void snap_u16(snapshot_io *s, uint16_t *v) {
  uint8_t b[2] = { (uint8_t)*v, (uint8_t)(*v >> 8) };
  snap_bytes(s, b, sizeof b);
  if (s->reader && !s->bad)
    *v = (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8));
}
static void snap_i16(snapshot_io *s, int16_t *v) {
  uint16_t bits=(uint16_t)*v;
  snap_u16(s,&bits);
  if(s->reader&&!s->bad)*v=(int16_t)(bits<=32767u?(int)bits:(int)bits-65536);
}
static void snap_u32(snapshot_io *s, uint32_t *v) {
  uint8_t b[4]; unsigned i;
  for (i = 0; i < 4; ++i) b[i] = (uint8_t)(*v >> (8u*i));
  snap_bytes(s, b, sizeof b);
  if (s->reader && !s->bad) {
    *v = 0;
    for (i = 0; i < 4; ++i) *v |= (uint32_t)b[i] << (8u*i);
  }
}
static void snap_u64(snapshot_io *s, uint64_t *v) {
  uint8_t b[8]; unsigned i;
  for (i = 0; i < 8; ++i) b[i] = (uint8_t)(*v >> (8u*i));
  snap_bytes(s, b, sizeof b);
  if (s->reader && !s->bad) {
    *v = 0;
    for (i = 0; i < 8; ++i) *v |= (uint64_t)b[i] << (8u*i);
  }
}
static void snap_2dpt(snapshot_io *s, twodfx_2dpt_entry entries[2][256]) {
  unsigned l, i;
  for (l = 0; l < 2; ++l) for (i = 0; i < 256; ++i) {
    snap_u8(s, &entries[l][i].opcode);
    snap_u8(s, &entries[l][i].nbytes);
    snap_bytes(s, entries[l][i].payload, sizeof entries[l][i].payload);
  }
}
static void snap_body(snapshot_io *s, twodfx_emu *d) {
  uint8_t machine = (uint8_t)d->machine;
  uint8_t job = (uint8_t)d->resource_job;
  unsigned i;
  snap_u8(s,&d->logo_small);snap_u32(s,&d->rng_state);
  for(unsigned k=0;k<2;k++){
    portable_pointer*p=k?&d->pointer_active:&d->pointer_shadow;
    snap_u16(s,&p->x);snap_u16(s,&p->y);snap_u8(s,&p->mode);snap_u8(s,&p->colors);
    snap_u8(s,&p->initialized);snap_u8(s,&p->valid);snap_u8(s,&p->hx);snap_u8(s,&p->hy);
  }
  snap_bytes(s,d->pointer_user_shadow,144);snap_bytes(s,d->pointer_user_active,144);
  snap_u8(s,&d->pointer_pending);snap_u8(s,&d->pointer_hx);snap_u8(s,&d->pointer_hy);
  snap_u32(s,&d->pointer_address);
  snap_u8(s, &machine);
  if (s->reader && !s->bad) d->machine = (twodfx_machine)machine;
  snap_u8(s, &d->started);
  snap_u8(s, &d->awake);
  snap_u8(s, &d->terminal);
  snap_u8(s, &d->engine);
  snap_u8(s, &d->transparent);
  snap_u8(s, &d->fps);snap_u8(s,&d->fps_phase);
  snap_u8(s, &d->overrun_visual);
  snap_u8(s, &d->session);
  snap_u8(s, &d->opcode);
  snap_u8(s, &d->param_n);
  snap_u8(s, &d->param_needed);
  snap_u8(s, &d->foreground);
  snap_u8(s, &d->spb_id);
  snap_bytes(s, d->params, sizeof d->params);
  snap_u8(s, &d->spb_offset);
  snap_u8(s, &d->upload_header_n);
  snap_u8(s, &d->upload_convert);
  snap_bytes(s, d->upload_header, sizeof d->upload_header);
  snap_u32(s, &d->upload_addr);
  snap_u32(s, &d->upload_remain);
  snap_bytes(s, d->shadow_spt, sizeof d->shadow_spt);
  snap_bytes(s, d->active_spt, sizeof d->active_spt);
  snap_2dpt(s, d->shadow_2dpt);
  snap_2dpt(s, d->active_2dpt);
  snap_bytes(s, d->ctrl_raw, sizeof d->ctrl_raw);
  snap_bytes(s, d->ctrl_size, sizeof d->ctrl_size);
  for (i = 0; i < 4; ++i) snap_u16(s, &d->raster_lines[i]);
  snap_u8(s, &d->raster_pending);
  snap_u64(s, &d->video_frames);
  snap_u8(s, &d->logo_visible);
  snap_u8(s, &d->egg_input);
  snap_u8(s,&d->egg_id);
  snap_u8(s,&d->egg_pending);
  snap_u8(s,&d->egg_head);
  snap_u8(s,&d->egg_tail);
  snap_u8(s,&d->egg_overflow);
  snap_bytes(s,d->egg_fifo,sizeof d->egg_fifo);
  snap_bytes(s,d->demo_states,TWODFX_DEMO_STATE_BYTES);
  /* Public logo artwork and optional host override. */
  snap_u8(s, &d->logo_art_loaded);
  snap_u16(s, &d->logo_x);
  snap_u16(s, &d->logo_y);
  snap_u8(s, &d->logo_main);
  snap_u8(s, &d->logo_accent);
  snap_bytes(s,d->logo_art,sizeof d->logo_art);
  snap_u8(s, &job);
  if (s->reader && !s->bad) d->resource_job = (twodfx_emu_job_status_type)job;
  snap_u8(s, &d->job_head);
  snap_u8(s, &d->job_tail);
  snap_u8(s, &d->overrun_pending);
  snap_u8(s, &d->overrun_phase);
  snap_u32(s, &d->job_output_bytes);
  snap_u32(s, &d->job_overflow_count);
  for (i = 0; i < TWODFX_EMU_RESOURCE_QUEUE_CAPACITY; ++i) {
    snap_u32(s, &d->jobs[i].source);
    snap_u32(s, &d->jobs[i].destination);
    snap_u8(s, &d->jobs[i].format);
  }
  for (i = 0; i < 64; ++i) {
    snap_u32(s, &d->fonts[i].resource_address);
    snap_u8(s, &d->fonts[i].width_units);
    snap_u8(s, &d->fonts[i].height_units);
    snap_u8(s, &d->fonts[i].valid);
  }
  for (i = 0; i < 256; ++i) {
    snap_u32(s, &d->bitmaps[i].resource_address);
    snap_u16(s, &d->bitmaps[i].width);
    snap_u16(s, &d->bitmaps[i].height);
    snap_u8(s, &d->bitmaps[i].valid);
  }
  for (i = 0; i < 64; ++i) {
    snap_u32(s, &d->patterns[i].resource_address);
    snap_u8(s, &d->patterns[i].valid);
  }
  for (i = 0; i < 64; ++i) {
    snap_u32(s, &d->tilesets[i].resource_address);
    snap_u8(s, &d->tilesets[i].valid);
  }
  for (i = 0; i < 32; ++i) {
    snap_u32(s, &d->tilemaps[i].resource_address);
    snap_u16(s, &d->tilemaps[i].width_cells);
    snap_u16(s, &d->tilemaps[i].height_cells);
    snap_u8(s, &d->tilemaps[i].tileset);
    snap_u8(s, &d->tilemaps[i].valid);
  }
  for (i = 0; i < 64; ++i) {
    snap_u32(s, &d->active_fonts[i].resource_address);
    snap_u8(s, &d->active_fonts[i].width_units);
    snap_u8(s, &d->active_fonts[i].height_units);
    snap_u8(s, &d->active_fonts[i].valid);
  }
  for (i = 0; i < 256; ++i) {
    snap_u32(s, &d->active_bitmaps[i].resource_address);
    snap_u16(s, &d->active_bitmaps[i].width);
    snap_u16(s, &d->active_bitmaps[i].height);
    snap_u8(s, &d->active_bitmaps[i].valid);
  }
  for (i = 0; i < 64; ++i) {
    snap_u32(s, &d->active_patterns[i].resource_address);
    snap_u8(s, &d->active_patterns[i].valid);
  }
  for (i = 0; i < 64; ++i) {
    snap_u32(s, &d->active_tilesets[i].resource_address);
    snap_u8(s, &d->active_tilesets[i].valid);
  }
  for (i = 0; i < 32; ++i) {
    snap_u32(s, &d->active_tilemaps[i].resource_address);
    snap_u16(s, &d->active_tilemaps[i].width_cells);
    snap_u16(s, &d->active_tilemaps[i].height_cells);
    snap_u8(s, &d->active_tilemaps[i].tileset);
    snap_u8(s, &d->active_tilemaps[i].valid);
  }
  snap_bytes(s, d->ram, TWODFX_EMU_RESOURCE_BYTES);
  snap_u8(s, &d->render_enabled);
  snap_u8(s, &d->collision_reported);
  snap_u8(s, &d->collision_pending);
  snap_u8(s, &d->collision_ready);
  snap_bytes(s, d->frame_colour, TWODFX_FRAME_PIXELS);
  snap_bytes(s, d->frame_coverage, TWODFX_FRAME_PIXELS);
  /* Serialized, byte-aligned byte-serialized, padding-independent 3D state. */
  for(i=0;i<256u;i++) {
    snap_u32(s,&d->scene3d.mesh_shadow[i].address);
    snap_u16(s,&d->scene3d.mesh_shadow[i].vertices);
    snap_u16(s,&d->scene3d.mesh_shadow[i].faces);
  }
  for(i=0;i<256u;i++) {
    snap_u32(s,&d->scene3d.mesh_active[i].address);
    snap_u16(s,&d->scene3d.mesh_active[i].vertices);
    snap_u16(s,&d->scene3d.mesh_active[i].faces);
  }
  {
    twodfx_3d_pose *c[2]={&d->scene3d.camera_shadow,&d->scene3d.camera_active};
    twodfx_3d_projection *pr[2]={&d->scene3d.projection_shadow,&d->scene3d.projection_active};
    unsigned k;
    for(k=0;k<2u;k++){
      /* u16 read/write preserves signed 16-bit bit patterns. */
      snap_i16(s,&c[k]->x);
      snap_i16(s,&c[k]->y);
      snap_i16(s,&c[k]->z);
      snap_u16(s,&c[k]->roll);snap_u16(s,&c[k]->yaw);snap_u16(s,&c[k]->pitch);
      snap_u16(s,&pr[k]->fx);snap_u16(s,&pr[k]->fy);
      snap_u16(s,&pr[k]->near_z);snap_u16(s,&pr[k]->far_z);
    }
  }

}
static void snapshot_put_u32(uint8_t *b, uint32_t v) {
  unsigned i;
  for (i = 0; i < 4; ++i) b[i] = (uint8_t)(v >> (i*8u));
}
static uint32_t snapshot_get_u32(const uint8_t *b) {
  return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
         ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
/* Ethernet/ZIP CRC-32, IEEE 802.3 polynomial, over the canonical body. */
static uint32_t snapshot_crc32(const uint8_t *b, size_t n) {
  uint32_t crc = UINT32_MAX;
  size_t i;
  for (i = 0; i < n; ++i) {
    unsigned bit;
    crc ^= b[i];
    for (bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1u) ? UINT32_C(0xedb88320) : 0u);
  }
  return ~crc;
}
size_t twodfx_emu_snapshot_size(void) {
  twodfx_emu d;
  snapshot_io s;
  memset(&d, 0, sizeof d);
  memset(&s, 0, sizeof s);
  s.capacity = SIZE_MAX; /* counting only; does not touch 8 MiB RAM */
  snap_body(&s, &d);
  return s.bad || s.offset > SIZE_MAX-TWODFX_SNAPSHOT_HEADER_BYTES ?
    0 : s.offset + TWODFX_SNAPSHOT_HEADER_BYTES;
}
int twodfx_emu_save_snapshot(const twodfx_emu *d, void *output, size_t capacity) {
  static const uint8_t magic[8] = { '2','D','F','X','S','N','A','P' };
  snapshot_io s;
  size_t n = twodfx_emu_snapshot_size();
  uint8_t *p = (uint8_t *)output;
  if (!d || !p || !n || capacity < n || n-TWODFX_SNAPSHOT_HEADER_BYTES > UINT32_MAX)
    return 0;
  memcpy(p, magic, 8);
  p[8] = (uint8_t)TWODFX_SNAPSHOT_FORMAT_VERSION;
  p[9] = p[10] = p[11] = 0;
  snapshot_put_u32(p+12, (uint32_t)(n-TWODFX_SNAPSHOT_HEADER_BYTES));
  memset(&s, 0, sizeof s);
  s.writer = p+TWODFX_SNAPSHOT_HEADER_BYTES;
  s.capacity = n-TWODFX_SNAPSHOT_HEADER_BYTES;
  /* The encoder writes no fields, only reads them, from the input state. */
  snap_body(&s, (twodfx_emu *)(uintptr_t)d);
  if (s.bad || s.offset != s.capacity) return 0;
  snapshot_put_u32(p+16, snapshot_crc32(p+TWODFX_SNAPSHOT_HEADER_BYTES, s.capacity));
  return 1;
}
static int snapshot_valid(const twodfx_emu *d) {
  if(d->logo_small>1||d->pointer_pending>1||d->pointer_hx>=24||d->pointer_hy>=12||d->pointer_address>0x7fffff)return 0;
  for(unsigned k=0;k<2;k++){
    const portable_pointer*p=k?&d->pointer_active:&d->pointer_shadow;
    if(p->mode>15||p->initialized>1||p->valid>1||p->hx>=24||p->hy>=12||p->x>1023||p->y>511)return 0;
  }
  unsigned l, i;
  if (d->machine > TWODFX_EMU_TVC || d->started > 1 || d->awake > 1 ||
      d->terminal > 2 || d->engine > TWODFX_EMU_ENGINE_FORCED ||
      d->transparent > 15 || (d->fps != 25 && d->fps != 50) ||
      d->overrun_visual > 1 || d->session > SESSION_UNZX ||
      d->param_n > sizeof d->params || d->param_needed > sizeof d->params ||
      d->param_n > d->param_needed || d->foreground > 1 || d->spb_id >= 64 ||
      d->spb_offset > 9 || d->upload_header_n > 5 ||
      d->upload_convert > 2 || d->upload_remain > 65536 ||
      d->logo_visible > 1 || d->logo_art_loaded > 1 ||
      d->logo_x > 1023u || d->logo_y > 511u ||
      d->logo_main > 15u || d->logo_accent > 15u ||
      d->resource_job > TWODFX_RESOURCE_JOB_ERROR ||
      d->raster_pending > 15 || d->overrun_pending > 1 || d->overrun_phase > 15 ||
      (uint8_t)(d->job_head - d->job_tail) > TWODFX_EMU_RESOURCE_QUEUE_CAPACITY ||
      ((d->resource_job == TWODFX_RESOURCE_JOB_BUSY) != (d->job_head != d->job_tail)) ||
      (d->terminal && d->job_head != d->job_tail) ||
      d->job_output_bytes > TWODFX_EMU_RESOURCE_BYTES ||
      d->render_enabled > 1u ||
      d->collision_reported > 1u || d->collision_pending > 1u ||
      d->collision_ready > 1u ||
      (d->egg_id != 0xffu && !egg_supported(d,d->egg_id)) ||
      d->egg_pending > 1u || d->egg_head >= 64u || d->egg_tail >= 64u ||
      d->egg_overflow > 1u ||
      (d->egg_id==0xffu && (d->egg_pending || d->egg_head || d->egg_tail || d->egg_overflow)))
    return 0;
  for (i = 0; i < TWODFX_EMU_RESOURCE_QUEUE_CAPACITY; ++i)
    if (d->jobs[i].format > 3 || d->jobs[i].source >= TWODFX_EMU_RESOURCE_BYTES ||
        d->jobs[i].destination >= TWODFX_EMU_RESOURCE_BYTES) return 0;
  /* A CRC-valid image is not necessarily a safe parser state.  In
     particular CTRL has only ten bytes of backing ctrl_raw storage. */
  switch (d->session) {
    case SESSION_NONE:
      if (d->param_n != 0 || d->param_needed != 0 || d->spb_offset != 0 ||
          d->upload_header_n != 0 || d->upload_remain != 0) return 0;
      break;
    case SESSION_CTRL:
      if (ctrl_args(d->opcode) == 0 ||
          d->param_needed != ctrl_args(d->opcode) || d->param_n>=d->param_needed ||
          d->param_needed > sizeof d->ctrl_raw[0]) return 0;
      break;
    case SESSION_2DPT:
      if (twod_args(d->opcode) == 0 ||
          d->param_needed != twod_args(d->opcode) || d->param_n>=d->param_needed) return 0;
      break;
    case SESSION_UNZX:
      if (d->opcode < 0x83 || d->opcode > 0x85 ||
          d->param_needed != 6) return 0;
      break;
    case SESSION_UPLOAD:
      if (d->upload_header_n == 5 && d->upload_remain == 0) return 0;
      if (d->upload_header_n < 5 && d->upload_remain != 0) return 0;
      break;
    case SESSION_SPRITE:
      if (d->param_n != 0 || d->param_needed != 0) return 0;
      break;
    default: return 0;
  }
  for (l = 0; l < 2; ++l) for (i = 0; i < 256; ++i) {
    const twodfx_2dpt_entry *a = &d->shadow_2dpt[l][i];
    const twodfx_2dpt_entry *b = &d->active_2dpt[l][i];
    if (a->nbytes > 11 || b->nbytes > 11) return 0;
  }
  for (i = 0; i < 64; ++i) {
    if (d->fonts[i].valid > 1 || d->fonts[i].resource_address >= TWODFX_EMU_RESOURCE_BYTES ||
        d->fonts[i].width_units > 16 || d->fonts[i].height_units > 16 ||
        d->patterns[i].valid > 1 || d->tilesets[i].valid > 1 ||
        d->patterns[i].resource_address >= TWODFX_EMU_RESOURCE_BYTES ||
        d->tilesets[i].resource_address >= TWODFX_EMU_RESOURCE_BYTES) return 0;
  }
  for (i = 0; i < 256; ++i) {
    if (d->bitmaps[i].valid > 1 || d->bitmaps[i].resource_address >= TWODFX_EMU_RESOURCE_BYTES)
      return 0;
  }
  for (i = 0; i < 32; ++i) {
    if (d->tilemaps[i].valid > 1 || d->tilemaps[i].resource_address >= TWODFX_EMU_RESOURCE_BYTES ||
        d->tilemaps[i].width_cells > 256 || d->tilemaps[i].height_cells > 256 ||
        d->tilemaps[i].tileset >= 64) return 0;
  }
  /* Independently reject CRC-valid malformed published descriptors. */
  for (i=0;i<64u;i++) {
    if(d->active_fonts[i].valid>1 ||
       d->active_fonts[i].resource_address>=TWODFX_EMU_RESOURCE_BYTES ||
       d->active_fonts[i].width_units>16 || d->active_fonts[i].height_units>16 ||
       d->active_patterns[i].valid>1 ||
       d->active_patterns[i].resource_address>=TWODFX_EMU_RESOURCE_BYTES ||
       d->active_tilesets[i].valid>1 ||
       d->active_tilesets[i].resource_address>=TWODFX_EMU_RESOURCE_BYTES)return 0;
  }
  for (i=0;i<256u;i++)
    if(d->active_bitmaps[i].valid>1 ||
       d->active_bitmaps[i].resource_address>=TWODFX_EMU_RESOURCE_BYTES)return 0;
  for (i=0;i<32u;i++)
    if(d->active_tilemaps[i].valid>1 ||
       d->active_tilemaps[i].resource_address>=TWODFX_EMU_RESOURCE_BYTES ||
       d->active_tilemaps[i].width_cells>256 || d->active_tilemaps[i].height_cells>256 ||
       d->active_tilemaps[i].tileset>=64)return 0;
  /* CRC-valid mesh state must not permit an OOB geometry read. */
  for(i=0;i<256u;i++) {
    const twodfx_3d_mesh *m=&d->scene3d.mesh_shadow[i];
    const twodfx_3d_mesh *a=&d->scene3d.mesh_active[i];
    if(m->address>=TWODFX_EMU_RESOURCE_BYTES ||
       a->address>=TWODFX_EMU_RESOURCE_BYTES ||
       m->vertices>256u || a->vertices>256u ||
       m->faces>512u || a->faces>512u ||
       (m->faces && (!m->vertices ||
         (uint64_t)m->address+(uint64_t)m->vertices*6u+(uint64_t)m->faces*4u>TWODFX_EMU_RESOURCE_BYTES)) ||
       (a->faces && (!a->vertices ||
         (uint64_t)a->address+(uint64_t)a->vertices*6u+(uint64_t)a->faces*4u>TWODFX_EMU_RESOURCE_BYTES)))return 0;
  }
  {
    const twodfx_3d_pose *p[2]={&d->scene3d.camera_shadow,&d->scene3d.camera_active};
    const twodfx_3d_projection *pr[2]={&d->scene3d.projection_shadow,&d->scene3d.projection_active};
    for(i=0;i<2u;i++) {
      if(p[i]->roll>=1024u || p[i]->yaw>=1024u || p[i]->pitch>=1024u ||
         !pr[i]->fx || !pr[i]->fy || !pr[i]->near_z ||
         pr[i]->far_z<=pr[i]->near_z)return 0;
    }
  }
  for(i=0;i<TWODFX_EMU_LOGO_BYTES;i++) {
    uint8_t v=d->logo_art[i];
    if((v & (uint8_t)(v>>1) & 0x55u)!=0)return 0;
  }
  /* A valid CRC alone cannot make an illegal coverage/colour image safe. */
  for (i=0;i<TWODFX_FRAME_PIXELS;i++)
    if (d->frame_colour[i]>15u || d->frame_coverage[i]>1u) return 0;
  return 1;
}
int twodfx_emu_load_snapshot(twodfx_emu *d, const void *input, size_t length) {
  static const uint8_t magic[8] = { '2','D','F','X','S','N','A','P' };
  const uint8_t *p = (const uint8_t *)input;
  size_t n = twodfx_emu_snapshot_size();
  snapshot_io s;
  twodfx_emu *replacement, previous;
  if (!d || !p || length != n || n < TWODFX_SNAPSHOT_HEADER_BYTES ||
      memcmp(p, magic, 8) || p[8] != TWODFX_SNAPSHOT_FORMAT_VERSION ||
      p[9] || p[10] || p[11] ||
      snapshot_get_u32(p+12) != (uint32_t)(n-TWODFX_SNAPSHOT_HEADER_BYTES) ||
      snapshot_get_u32(p+16) != snapshot_crc32(p+TWODFX_SNAPSHOT_HEADER_BYTES,
                                                 n-TWODFX_SNAPSHOT_HEADER_BYTES))
    return 0;
  replacement = twodfx_emu_create(d->machine);
  if (!replacement) return 0;
  memset(&s, 0, sizeof s);
  s.reader = p+TWODFX_SNAPSHOT_HEADER_BYTES;
  s.capacity = n-TWODFX_SNAPSHOT_HEADER_BYTES;
  snap_body(&s, replacement);
  if (s.bad || s.offset != s.capacity || replacement->machine != d->machine ||
      !snapshot_valid(replacement)) {
    twodfx_emu_destroy(replacement);
    return 0;
  }
  previous = *d;
  *d = *replacement;
  *replacement = previous;
  twodfx_emu_destroy(replacement);
  return 1;
}

int twodfx_emu_get_mesh(const twodfx_emu *d,unsigned id,int active,
                        twodfx_3d_mesh_descriptor *out) {
  const twodfx_3d_mesh *m;
  if(!d||!out||id>=256u)return 0;
  m=active?&d->scene3d.mesh_active[id]:&d->scene3d.mesh_shadow[id];
  out->resource_address=m->address;out->vertices=m->vertices;
  out->triangles=m->faces;out->valid=m->faces>0;
  return 1;
}
int twodfx_emu_get_camera(const twodfx_emu *d,int active,
                          twodfx_3d_camera *out) {
  const twodfx_3d_pose *p;
  if(!d||!out)return 0;
  p=active?&d->scene3d.camera_active:&d->scene3d.camera_shadow;
  out->x=p->x;out->y=p->y;out->z=p->z;
  out->roll=p->roll;out->yaw=p->yaw;out->pitch=p->pitch;
  return 1;
}
int twodfx_emu_get_projection(const twodfx_emu *d,int active,
                              twodfx_3d_projection_info *out) {
  const twodfx_3d_projection *p;
  if(!d||!out)return 0;
  p=active?&d->scene3d.projection_active:&d->scene3d.projection_shadow;
  out->fx=p->fx;out->fy=p->fy;out->near_z=p->near_z;out->far_z=p->far_z;
  return 1;
}
