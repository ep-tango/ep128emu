/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Károly Vaczkó
 */
#include "egg_D4_graphics_test.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "twodfx_api.h"

static bool g_is_tvc;
static uint32_t g_tick;
static uint8_t g_patterns[EGG_D4_PATTERN_COUNT][TWODFX_PATTERN_BYTES];

/* One raw 4bpp bitmap, low nibble = left pixel. Intentionally odd width
 * to demonstrate packed BLIT row stride as well as odd crops. */
#define EGG_D4_BITMAP_ID    60u
#define EGG_D4_BITMAP_ADDR  0x5B0000u
#define EGG_D4_BITMAP_W     161u
#define EGG_D4_BITMAP_H     104u
#define EGG_D4_PHASE_TICKS   1000u /* 20 seconds at 50 render ticks/second */
#define EGG_D4_PHASE_COUNT   6u
#define EGG_D4_PULSE_TICKS  250u /* 5-second shrink/grow cycle */
/* Resources used only by D4 clipping diagnostic, deliberately isolated from D3. */
#define EGG_D4_CLIP_FONT_ID   61u
#define EGG_D4_CLIP_FONT_ADDR 0x5C0000u
#define EGG_D4_CLIP_STR_ADDR  0x5C0800u
#define EGG_D4_CLIP_TILESET_ID 61u
#define EGG_D4_CLIP_TILESET_ADDR 0x5C1000u
#define EGG_D4_CLIP_TILEMAP_ID  29u
#define EGG_D4_CLIP_TILEMAP_ADDR 0x5C2000u
static uint8_t g_clip_font[128u * 8u];
static uint8_t g_clip_tile[64u];
static uint8_t g_clip_map[6u * 6u];
static uint8_t g_test_bitmap[EGG_D4_BITMAP_H][(EGG_D4_BITMAP_W + 1u) / 2u];

static void build_bitmap_resource(void)
{
    for (uint16_t y = 0; y < EGG_D4_BITMAP_H; ++y) {
        for (uint16_t x = 0; x < EGG_D4_BITMAP_W; ++x) {
            uint8_t c;
            /* High-contrast geometric texture: lines, coloured tiles and a
             * diagonal pattern make wrong source phase/scale obvious. */
            if ((x % 23u) < 2u || (y % 19u) < 2u)
                c = g_is_tvc ? 15u : 4u;
            else if (((x / 16u) + (y / 13u)) % 4u == 0u)
                c = 3u;
            else if (((x + 2u * y) / 11u) % 3u == 0u)
                c = g_is_tvc ? 7u : 6u;
            else if (x < 80u)
                c = g_is_tvc ? 9u : 5u;
            else
                c = g_is_tvc ? 13u : 1u;
            uint8_t *dst = &g_test_bitmap[y][x >> 1];
            if (x & 1u) *dst = (uint8_t)((*dst & 0x0Fu) | (c << 4));
            else *dst = (uint8_t)((*dst & 0xF0u) | (c & 0x0Fu));
        }
    }
    twodfx_upload_raw(EGG_D4_BITMAP_ADDR, &g_test_bitmap[0][0],
                      sizeof(g_test_bitmap));
    twodfx_define_bitmap(EGG_D4_BITMAP_ID, EGG_D4_BITMAP_ADDR,
                         EGG_D4_BITMAP_W, EGG_D4_BITMAP_H);
}

static uint8_t pack_pixels(uint8_t left, uint8_t right)
{
    return (uint8_t)((left & 0x0Fu) | ((right & 0x0Fu) << 4));
}

static void set_pattern_pixel(uint8_t pattern,
                              uint8_t x,
                              uint8_t y,
                              uint8_t colour)
{
    uint32_t offset = (uint32_t)y * 8u + (x >> 1);
    uint8_t packed = g_patterns[pattern][offset];

    if ((x & 1u) == 0u) {
        packed = (uint8_t)((packed & 0xF0u) | (colour & 0x0Fu));
    } else {
        packed = (uint8_t)((packed & 0x0Fu) | ((colour & 0x0Fu) << 4));
    }
    g_patterns[pattern][offset] = packed;
}

static uint8_t target_colour(void)
{
    return g_is_tvc ? 8u : 1u;
}

static uint8_t frame_colour(void)
{
    return g_is_tvc ? 15u : 4u;
}

static uint8_t accent_a(void)
{
    return 3u;
}

static uint8_t accent_b(void)
{
    return 5u;
}

static uint8_t accent_c(void)
{
    return g_is_tvc ? 7u : 6u;
}

static void build_patterns(void)
{
    const uint8_t target = target_colour();

    memset(g_patterns, pack_pixels(target, target), sizeof(g_patterns));

    for (uint8_t y = 0u; y < TWODFX_PATTERN_HEIGHT; y++) {
        for (uint8_t x = 0u; x < TWODFX_PATTERN_WIDTH; x++) {
            /* Pattern 0: 1x1 checker; exactly half the pixels remain target. */
            uint8_t c0 = (((x ^ y) & 1u) != 0u) ? accent_a() : target;
            set_pattern_pixel(0u, x, y, c0);

            /* Pattern 1: four-colour woven texture, also containing target. */
            uint8_t phase = (uint8_t)((x + y * 3u) & 7u);
            uint8_t c1;
            if (phase < 2u) {
                c1 = target;
            } else if (phase < 4u) {
                c1 = accent_b();
            } else if (phase < 6u) {
                c1 = accent_c();
            } else {
                c1 = accent_a();
            }
            set_pattern_pixel(1u, x, y, c1);
            /* Comparison pattern: transparent holes, target pixels, and asymmetric
             * stripes make both nibble alignment and local phase visible. */
            set_pattern_pixel(2u, x, y,
                ((x + 3u*y) & 7u) < 3u ? 0u : ((x & 4u) ? target : accent_a()));
        }
    }

    twodfx_upload_raw(EGG_D4_PATTERN_BANK_ADDRESS,
                      g_patterns,
                      sizeof(g_patterns));
    twodfx_define_pattern(EGG_D4_PATTERN_DESCRIPTOR,
                          EGG_D4_PATTERN_BANK_ADDRESS);
}

static int16_t triangle_wave(uint16_t tick, uint16_t period, int16_t amplitude)
{
    uint16_t half = (uint16_t)(period / 2u);
    uint16_t phase = (uint16_t)(tick % period);
    uint16_t ramp = phase <= half ? phase : (uint16_t)(period - phase);
    return (int16_t)(-amplitude +
                     ((int32_t)ramp * (int32_t)(amplitude * 2)) /
                     (int32_t)half);
}

/* Four identical concave arrow polygons provide a direct comparison:
 * top-left local/opaque, top-right screen/opaque; bottom repeats with
 * transparent source pixels. The seed moves with its polygon. */
static void build_pattern_scene(void)
{
    const uint16_t sx = TWODFX_BASIC_SAFE_MIN_X;
    const uint16_t sy = TWODFX_BASIC_SAFE_MIN_Y;
    const uint16_t sw = TWODFX_BASIC_SAFE_WIDTH;
    const uint16_t sh = TWODFX_BASIC_SAFE_HEIGHT;
    const uint16_t w = (uint16_t)(((sw - 60u) / 2u) | 1u);
    const uint16_t h = (uint16_t)((sh - 60u) / 2u);
    const int8_t ox = (int8_t)triangle_wave((uint16_t)g_tick, 112u, 9);
    const int8_t oy = (int8_t)triangle_wave((uint16_t)(g_tick + 23u), 128u, 5);
    uint8_t slot = 0u;
    twodfx_reset_2dpt_all();
    twodfx_2d_set_color(TWODFX_LAYER_BACKGROUND, slot++, target_colour(), 0u);
    twodfx_2d_fill_rect(TWODFX_LAYER_BACKGROUND, slot++, sx, sy, sw, sh);
    twodfx_2d_set_xy_offset(TWODFX_LAYER_BACKGROUND, slot++, ox, oy);

    for (uint8_t i = 0u; i < 4u; ++i) {
        const uint16_t x = (uint16_t)((sx + 15u + (i & 1u) * (sw / 2u)) | 1u);
        const uint16_t y = (uint16_t)(sy + 15u + (i >> 1) * (sh / 2u));
        const uint16_t neck = (uint16_t)(w * 3u / 5u);
        const uint16_t upper = (uint16_t)(h / 4u);
        const uint16_t lower = (uint16_t)(h * 3u / 4u);
        const uint16_t vx[7] = {x, x + neck, x + neck, x + w - 1u,
                               x + neck, x + neck, x};
        const uint16_t vy[7] = {y + upper, y + upper, y, y + h / 2u,
                               y + h - 1u, y + lower, y + lower};
        twodfx_2d_set_color(TWODFX_LAYER_BACKGROUND, slot++, frame_colour(), 0u);
        for (uint8_t edge = 0u; edge < 7u; ++edge) {
            const uint8_t next = (uint8_t)((edge + 1u) % 7u);
            twodfx_2d_line(TWODFX_LAYER_BACKGROUND, slot++,
                          vx[edge], vy[edge], vx[next], vy[next], 1u);
        }
        const uint8_t flags = (uint8_t)(
            ((i & 1u) ? TWODFX_PATTERN_SCREEN_ORIGIN : 0u) |
            ((i & 2u) ? TWODFX_PATTERN_TRANSPARENT : 0u));
        twodfx_2d_pattern_bucket_fill_ex(TWODFX_LAYER_BACKGROUND, slot++,
            x + w / 3u, y + h / 2u, EGG_D4_PATTERN_DESCRIPTOR, 2u, flags);
    }
    twodfx_2d_ret(TWODFX_LAYER_BACKGROUND, slot);
}

static void build_bitmap_scene(bool stretch)
{
    const uint16_t bx = (uint16_t)(TWODFX_BASIC_SAFE_MIN_X + 29u);
    const uint16_t by = (uint16_t)(TWODFX_BASIC_SAFE_MIN_Y + 35u);
    const uint16_t dx = (uint16_t)(TWODFX_BASIC_SAFE_MIN_X +
                                  TWODFX_BASIC_SAFE_WIDTH / 2u + 22u + 1u);
    const uint16_t dy = (uint16_t)(TWODFX_BASIC_SAFE_MIN_Y + 36u + 1u);
    const uint16_t sx = 11u, sy = 9u, sw = 91u, sh = 71u;
    uint8_t slot = 0u;
    twodfx_reset_2dpt_all();

    /* A visible frame around the whole source image and the selected part. */
    twodfx_2d_set_color(TWODFX_LAYER_BACKGROUND, slot++,
                        g_is_tvc ? 15u : 4u, 0u);
    twodfx_2d_rect(TWODFX_LAYER_BACKGROUND, slot++,
                   (uint16_t)(bx - 1u), (uint16_t)(by - 1u),
                   EGG_D4_BITMAP_W + 2u, EGG_D4_BITMAP_H + 2u, 1u);
    twodfx_2d_blit(TWODFX_LAYER_BACKGROUND, slot++, bx, by,
                   EGG_D4_BITMAP_ID, TWODFX_BLIT_EXACT);
    /* Source selection in red on both Enterprise and TVC: visibly distinct
     * from the white/yellow frame around the full source bitmap. */
    twodfx_2d_set_color(TWODFX_LAYER_BACKGROUND, slot++, 3u, 0u);
    twodfx_2d_rect(TWODFX_LAYER_BACKGROUND, slot++,
                   (uint16_t)(bx + sx), (uint16_t)(by + sy), sw, sh, 1u);

    if (!stretch) {
        twodfx_2d_crop_blit(TWODFX_LAYER_BACKGROUND, slot++,
                            EGG_D4_BITMAP_ID, TWODFX_BLIT_EXACT,
                            sx, sy, sw, sh, dx, (uint16_t)(dy + 34u));
    } else {
        twodfx_2d_stretch_blit(TWODFX_LAYER_BACKGROUND, slot++,
                               EGG_D4_BITMAP_ID, TWODFX_BLIT_EXACT,
                               sx, sy, sw, sh, dx, dy, 153u, 149u);
    }
    twodfx_2d_ret(TWODFX_LAYER_BACKGROUND, slot);
}

/* 8x8 1bpp text proof for DRAW_TEXT clipping. Four glyphs are enough. */
static void prepare_clip_assets(void)
{
    static const uint8_t glyph_c[8] = {0x3c,0x42,0x40,0x40,0x40,0x42,0x3c,0};
    static const uint8_t glyph_l[8] = {0x40,0x40,0x40,0x40,0x40,0x40,0x7e,0};
    static const uint8_t glyph_i[8] = {0x3c,0x18,0x18,0x18,0x18,0x18,0x3c,0};
    static const uint8_t glyph_p[8] = {0x7c,0x42,0x42,0x7c,0x40,0x40,0x40,0};
    static const uint8_t text[5] = {'C','L','I','P',0x80u};
    memset(g_clip_font, 0, sizeof(g_clip_font));
    memcpy(&g_clip_font['C' * 8u], glyph_c, 8u);
    memcpy(&g_clip_font['L' * 8u], glyph_l, 8u);
    memcpy(&g_clip_font['I' * 8u], glyph_i, 8u);
    memcpy(&g_clip_font['P' * 8u], glyph_p, 8u);
    twodfx_upload_raw(EGG_D4_CLIP_FONT_ADDR, g_clip_font, sizeof(g_clip_font));
    twodfx_upload_raw(EGG_D4_CLIP_STR_ADDR, text, sizeof(text));
    twodfx_define_font(EGG_D4_CLIP_FONT_ID, EGG_D4_CLIP_FONT_ADDR, 1u, 1u);
    for (uint8_t y = 0u; y < 8u; y++) {
        for (uint8_t x = 0u; x < 16u; x += 2u) {
            uint8_t a = ((x + y) & 4u) ? accent_a() : accent_b();
            uint8_t b = ((x + y + 1u) & 4u) ? accent_a() : accent_b();
            g_clip_tile[(uint32_t)y * 8u + (x >> 1)] = pack_pixels(a,b);
        }
    }
    memset(g_clip_map, 0, sizeof(g_clip_map));
    twodfx_upload_raw(EGG_D4_CLIP_TILESET_ADDR, g_clip_tile, sizeof(g_clip_tile));
    twodfx_upload_raw(EGG_D4_CLIP_TILEMAP_ADDR, g_clip_map, sizeof(g_clip_map));
    twodfx_define_tileset(EGG_D4_CLIP_TILESET_ID, EGG_D4_CLIP_TILESET_ADDR);
    twodfx_define_tilemap(EGG_D4_CLIP_TILEMAP_ID, EGG_D4_CLIP_TILEMAP_ADDR,
                          6u, 6u, EGG_D4_CLIP_TILESET_ID);
}

/* All three final phases render the SAME moving scene.  The full-screen
 * phase has no SET_CLIP_RECT; the other two vary only the clipping window.
 * Keeping identical scene commands makes frame-time scope comparisons useful.
 * These max-visible extents are measured separately for Enterprise and TVC. */
static uint8_t build_clip_scene(uint8_t phase)
{
    const uint16_t visible_x = TWODFX_MAX_VISIBLE_MIN_X;
    const uint16_t visible_y = TWODFX_MAX_VISIBLE_MIN_Y;
    const uint16_t visible_w = TWODFX_MAX_VISIBLE_WIDTH;
    const uint16_t visible_h = TWODFX_MAX_VISIBLE_HEIGHT;
    const uint16_t safe_x = TWODFX_BASIC_SAFE_MIN_X;
    const uint16_t safe_y = TWODFX_BASIC_SAFE_MIN_Y;
    const uint16_t safe_w = TWODFX_BASIC_SAFE_WIDTH;
    const uint16_t safe_h = TWODFX_BASIC_SAFE_HEIGHT;

    /* The fixed viewport also defines fixed object positions. All
     * geometry stays stationary relative to it in scenes 4, 5 and 6; only
     * SET_CLIP_RECT changes, never the source geometry or the wave timing. */
    const uint16_t base_x = (uint16_t)(safe_x + safe_w / 4u + 1u);
    const uint16_t base_y = (uint16_t)(safe_y + 33u);
    const uint16_t base_w = (uint16_t)(safe_w / 2u - 1u);
    const uint16_t base_h = (uint16_t)(safe_h - 67u);
    const uint16_t center_x = (uint16_t)(base_x + base_w / 2u);
    const uint16_t center_y = (uint16_t)(base_y + base_h / 2u);
    const uint8_t outline = frame_colour();

    uint16_t clip_x = base_x, clip_y = base_y;
    uint16_t clip_w = base_w, clip_h = base_h;
    if (phase == 5u) {
        /* Four complete full-to-small-to-full cycles in this 20s phase.
         * Keep both axes centred so the clip genuinely grows/shrinks.
         * Odd widths exercise the 4bpp nibble clipping path repeatedly. */
        const uint16_t local_tick = (uint16_t)(g_tick % EGG_D4_PHASE_TICKS);
        const uint16_t wave = (uint16_t)(local_tick % EGG_D4_PULSE_TICKS);
        const uint16_t half = EGG_D4_PULSE_TICKS / 2u;
        const uint16_t shrink = (wave <= half) ? wave :
                                 (uint16_t)(EGG_D4_PULSE_TICKS - wave);
        const uint16_t max_w = (uint16_t)(visible_w - 6u);
        const uint16_t max_h = (uint16_t)(visible_h - 6u);
        const uint16_t min_w = (uint16_t)(visible_w / 5u);
        const uint16_t min_h = (uint16_t)(visible_h / 4u);
        clip_w = (uint16_t)(max_w -
            (uint32_t)(max_w - min_w) * shrink / half);
        clip_h = (uint16_t)(max_h -
            (uint32_t)(max_h - min_h) * shrink / half);
        clip_w |= 1u;
        clip_x = (uint16_t)(visible_x + (visible_w - clip_w) / 2u);
        clip_y = (uint16_t)(visible_y + (visible_h - clip_h) / 2u);
    }

    uint8_t slot = 0u;
    twodfx_reset_2dpt_all();

    /* Clear exactly the maximum physically visible area before clipping.
     * The un-clipped phase exercises all the visible screen, not just the
     * IS-BASIC / VT-BASIC inner safe rectangle. */
    twodfx_2d_set_color(TWODFX_LAYER_BACKGROUND, slot++, g_is_tvc ? 8u : 1u, 0u);
    twodfx_2d_fill_rect(TWODFX_LAYER_BACKGROUND, slot++,
                        visible_x, visible_y, visible_w, visible_h);
    if (phase != 3u) {
        twodfx_2d_set_clip_rect(TWODFX_LAYER_BACKGROUND, slot++,
                                clip_x, clip_y, clip_w, clip_h);
    }

    /* The same complex graphics workload in all three phases. */
    /* The large-area solid pass is FILL_RECT, not a full-visible-area
     * BUCKET_FILL: that redundant flood traversal alone can exceed the 20ms
     * frame budget.  Still exercise BUCKET_FILL inside a bounded 36x30 patch;
     * after its fill the pixels match the same uniform background.  All three
     * scope phases retain identical content and command timing. */
    twodfx_2d_set_color(TWODFX_LAYER_BACKGROUND, slot++, accent_c(), 0u);
    twodfx_2d_fill_rect(TWODFX_LAYER_BACKGROUND, slot++,
                        visible_x, visible_y, visible_w, visible_h);
    twodfx_2d_set_color(TWODFX_LAYER_BACKGROUND, slot++, target_colour(), 0u);
    twodfx_2d_fill_rect(TWODFX_LAYER_BACKGROUND, slot++,
                        (uint16_t)(center_x - 18u), (uint16_t)(center_y - 15u), 36u, 30u);
    twodfx_2d_set_color(TWODFX_LAYER_BACKGROUND, slot++, accent_c(), 0u);
    twodfx_2d_bucket_fill(TWODFX_LAYER_BACKGROUND, slot++, center_x, center_y);
    twodfx_2d_set_color(TWODFX_LAYER_BACKGROUND, slot++, target_colour(), 0u);
    twodfx_2d_fill_rect(TWODFX_LAYER_BACKGROUND, slot++,
                        base_x, base_y, base_w / 2u, base_h);
    /* Keep the four-colour seed inside the smallest centred clip. */
    twodfx_2d_pattern_bucket_fill(TWODFX_LAYER_BACKGROUND, slot++,
                                  (uint16_t)(center_x - 25u), center_y,
                                  EGG_D4_PATTERN_DESCRIPTOR, 1u);

    /* The XY offset uses phase-local time, so each mode repeats the same
     * translations at the same points in its 20-second scope window. */
    const uint16_t t = (uint16_t)(g_tick % EGG_D4_PHASE_TICKS);
    const int8_t ox = (int8_t)triangle_wave(t, 110u, 23);
    const int8_t oy = (int8_t)triangle_wave((uint16_t)(t + 21u), 130u, 12);
    twodfx_2d_set_xy_offset(TWODFX_LAYER_BACKGROUND, slot++, ox, oy);
    twodfx_2d_set_color(TWODFX_LAYER_BACKGROUND, slot++, accent_a(), 0u);
    twodfx_2d_fill_triangle(TWODFX_LAYER_BACKGROUND, slot++,
        (uint16_t)(visible_x + 24u),
        (uint16_t)(visible_y + visible_h - 25u),
        (uint16_t)(visible_x + visible_w - 25u),
        (uint16_t)(visible_y + visible_h - 48u),
        center_x, (uint16_t)(visible_y + 17u));
    twodfx_2d_set_color(TWODFX_LAYER_BACKGROUND, slot++, outline, 0u);
    twodfx_2d_line(TWODFX_LAYER_BACKGROUND, slot++,
                   (uint16_t)(visible_x + 9u), (uint16_t)(visible_y + 11u),
                   (uint16_t)(visible_x + visible_w - 10u),
                   (uint16_t)(visible_y + visible_h - 14u), 1u);
    twodfx_2d_ellipse(TWODFX_LAYER_BACKGROUND, slot++, center_x, center_y,
                      (uint16_t)(visible_w / 2u - 30u),
                      (uint16_t)(visible_h / 2u - 24u), 1u);
    twodfx_2d_set_color(TWODFX_LAYER_BACKGROUND, slot++, accent_b(), 0u);
    twodfx_2d_pattern_fill(TWODFX_LAYER_BACKGROUND, slot++,
        (uint16_t)(visible_x + 11u), (uint16_t)(visible_y + 19u),
        (uint16_t)(visible_w - 22u), (uint16_t)(visible_h / 3u),
        EGG_D4_PATTERN_DESCRIPTOR, 0u);
    twodfx_2d_crop_blit(TWODFX_LAYER_BACKGROUND, slot++,
                        EGG_D4_BITMAP_ID, TWODFX_BLIT_EXACT,
                        7u, 7u, 67u, 65u,
                        (uint16_t)(base_x + base_w - 38u),
                        (uint16_t)(base_y - 16u));
    twodfx_2d_stretch_blit(TWODFX_LAYER_BACKGROUND, slot++,
                           EGG_D4_BITMAP_ID, TWODFX_BLIT_EXACT,
                           17u, 9u, 67u, 65u,
                           (uint16_t)(base_x - 46u),
                           (uint16_t)(base_y + base_h - 70u),
                           107u, 91u);
    twodfx_2d_blit(TWODFX_LAYER_BACKGROUND, slot++,
                   (uint16_t)(base_x + base_w - 40u),
                   (uint16_t)(base_y + base_h - 59u),
                   EGG_D4_BITMAP_ID, TWODFX_BLIT_EXACT);
    twodfx_2d_draw_text(TWODFX_LAYER_BACKGROUND, slot++,
                        (uint16_t)(base_x + base_w - 18u),
                        (uint16_t)(base_y + 13u),
                        EGG_D4_CLIP_FONT_ID, EGG_D4_CLIP_STR_ADDR);
    twodfx_2d_tilemap(TWODFX_LAYER_BACKGROUND, slot++,
                      (uint16_t)(base_x + base_w - 29u),
                      (uint16_t)(base_y + base_h - 18u),
                      EGG_D4_CLIP_TILEMAP_ID, 0u, 0u, 6u, 6u);

    /* A zero-area clip suppresses drawing inside all three phases.
     * It may not leak even a nibble, then the full clip is restored. */
    twodfx_2d_set_clip_rect(TWODFX_LAYER_BACKGROUND, slot++,
                            clip_x, clip_y, 0u, 0u);
    twodfx_2d_set_color(TWODFX_LAYER_BACKGROUND, slot++, g_is_tvc ? 15u : 4u, 0u);
    twodfx_2d_fill_rect(TWODFX_LAYER_BACKGROUND, slot++,
                        visible_x, visible_y, visible_w, visible_h);
    twodfx_2d_set_clip_rect(TWODFX_LAYER_BACKGROUND, slot++,
                            visible_x, visible_y, visible_w, visible_h);
    twodfx_2d_set_xy_offset(TWODFX_LAYER_BACKGROUND, slot++, 0, 0);
    twodfx_2d_ret(TWODFX_LAYER_BACKGROUND, slot);

    /* Foreground has independent clip state: the viewport outline and the
     * fixed indicator remain visible even when the background clip shrinks. */
    uint8_t fg = 0u;
    twodfx_2d_set_color(TWODFX_LAYER_FOREGROUND, fg++, outline, 0u);
    twodfx_2d_rect(TWODFX_LAYER_FOREGROUND, fg++,
                   visible_x, visible_y, visible_w, visible_h, 1u);
    if (phase != 3u) {
        twodfx_2d_rect(TWODFX_LAYER_FOREGROUND, fg++,
                       clip_x, clip_y, clip_w, clip_h, 1u);
    }
    return fg; /* shared phase indicator appends and terminates foreground */
}

/* The same foreground progress strip is displayed in EVERY phase.  It is
 * outside the background's clip state and unaffected by XY translation.
 * Six 17x5 bars fit within both BASIC-safe areas. */
static void draw_phase_indicator(uint8_t phase, uint8_t slot)
{
    const uint16_t x = (uint16_t)(TWODFX_BASIC_SAFE_MIN_X + 34u);
    const uint16_t y = (uint16_t)(TWODFX_BASIC_SAFE_MIN_Y +
                                   TWODFX_BASIC_SAFE_HEIGHT - 12u);
    for (uint8_t i = 0u; i < EGG_D4_PHASE_COUNT; i++) {
        twodfx_2d_set_color(TWODFX_LAYER_FOREGROUND, slot++,
                            i == phase ? (g_is_tvc ? 15u : 4u) :
                                         (g_is_tvc ? 8u : 5u), 0u);
        twodfx_2d_fill_rect(TWODFX_LAYER_FOREGROUND, slot++,
                            (uint16_t)(x + i * 23u), y, 17u, 5u);
    }
    twodfx_2d_ret(TWODFX_LAYER_FOREGROUND, slot);
}

void egg_D4_graphics_test_start(void)
{
    twodfx_reset_sprites();
    g_is_tvc = twodfx_get_machine() == TWODFX_MACHINE_TVC;
    g_tick = 0u;
    build_patterns();
    build_bitmap_resource();
    prepare_clip_assets();
    build_pattern_scene();
    draw_phase_indicator(0u, 0u);
}

void egg_D4_graphics_test_tick(void)
{
    g_tick++;
    const uint8_t phase = (uint8_t)((g_tick / EGG_D4_PHASE_TICKS) %
                                     EGG_D4_PHASE_COUNT);
    uint8_t fg_slot = 0u;
    if (phase == 0u) build_pattern_scene();
    else if (phase <= 2u) build_bitmap_scene(phase == 2u);
    else fg_slot = build_clip_scene(phase);
    draw_phase_indicator(phase, fg_slot);
}

/* Portable per-device state codec. */
#include "demo_state_codec.h"
size_t egg_D4_graphics_test_state(uint8_t *data,int load){
 size_t offset=0;
  STATE_U(g_is_tvc,8);
  STATE_U(g_tick,32);
  for(size_t i0=0;i0<sizeof(g_patterns)/sizeof((g_patterns)[0]);++i0){
  for(size_t i1=0;i1<sizeof(g_patterns[i0])/sizeof((g_patterns[i0])[0]);++i1){
  STATE_U(g_patterns[i0][i1],8);
  }
  }
  for(size_t i0=0;i0<sizeof(g_clip_font)/sizeof((g_clip_font)[0]);++i0){
  STATE_U(g_clip_font[i0],8);
  }
  for(size_t i0=0;i0<sizeof(g_clip_tile)/sizeof((g_clip_tile)[0]);++i0){
  STATE_U(g_clip_tile[i0],8);
  }
  for(size_t i0=0;i0<sizeof(g_clip_map)/sizeof((g_clip_map)[0]);++i0){
  STATE_U(g_clip_map[i0],8);
  }
  for(size_t i0=0;i0<sizeof(g_test_bitmap)/sizeof((g_test_bitmap)[0]);++i0){
  for(size_t i1=0;i1<sizeof(g_test_bitmap[i0])/sizeof((g_test_bitmap[i0])[0]);++i1){
  STATE_U(g_test_bitmap[i0][i1],8);
  }
  }
 return offset;
}
