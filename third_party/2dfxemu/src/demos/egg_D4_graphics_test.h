#ifndef EGG_D4_GRAPHICS_TEST_H
#define EGG_D4_GRAPHICS_TEST_H

#include "twodfx_api.h"

/* D4h / OUT 248,212: six-scene 2D graphics demonstration.
 * Each scene lasts 1,000 accepted updates (20 seconds at 50 updates/s).
 * The foreground strip marks the active scene, then the sequence repeats.
 *
 * 1. Four concave arrow polygons filled by PATTERN_BUCKET_FILL:
 *    top-left local/opaque, top-right screen/opaque;
 *    bottom-left local/transparent, bottom-right screen/transparent.
 *    Shapes move together; each seed stays fixed relative to its polygon.
 * 2. CROP_BLIT: selected source area copied at its original size.
 * 3. STRETCH_BLIT: selected source area scaled to a larger destination.
 * 4. Mixed full-visible-area scene, without clipping.
 * 5. The same scene restricted to a fixed clipping window.
 * 6. The same scene with a window that shrinks and expands every five seconds.
 */

#define EGG_D4_PATTERN_BANK_ADDRESS 0x5A0000u
#define EGG_D4_PATTERN_DESCRIPTOR   1u
#define EGG_D4_PATTERN_COUNT        3u

void egg_D4_graphics_test_start(void);
void egg_D4_graphics_test_tick(void);

#endif /* EGG_D4_GRAPHICS_TEST_H */
