#ifndef TWODFX_EGG_H
#define TWODFX_EGG_H

/*
 * 2dfx firmware egg registration, lifecycle and interactive input.
 *
 * Graphics, resources and renderer operations belong to twodfx_api.h. This
 * header only describes how compile-time firmware extensions are registered,
 * activated and supplied with standalone idle/unclaimed F9h input bytes.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Number of firmware egg command slots; valid egg IDs are 0..31. */
#define TWODFX_EGG_COUNT 32u

/** Machine-availability flags for one registry entry. */
#define TWODFX_EGG_MACHINE_ENTERPRISE 0x01u
#define TWODFX_EGG_MACHINE_TVC        0x02u
#define TWODFX_EGG_MACHINE_BOTH \
    (TWODFX_EGG_MACHINE_ENTERPRISE | TWODFX_EGG_MACHINE_TVC)

typedef void (*twodfx_egg_start_fn)(void);

typedef void (*twodfx_egg_tick_fn)(void);

/** One compile-time egg registration entry; registry array index is the ID. */
typedef struct {
    /** OR combination of TWODFX_EGG_MACHINE_ENTERPRISE/TVC. */
    uint8_t machine_flags;

    /** Required initialization callback; NULL means the slot is unavailable. */
    twodfx_egg_start_fn start;

    /** Optional scene-update callback; may be NULL for a static egg. */
    twodfx_egg_tick_fn tick;
} twodfx_egg_definition_t;

/**
 * Compile-time 32-entry egg registry defined in twodfx_egg_registry.c.
 *
 * Selecting any entry immediately begins a fresh pending input stream. At the
 * deferred safe point, a registered, machine-compatible entry deactivates the
 * previous egg, calls start once and then schedules tick normally. Selecting
 * an unavailable or incompatible slot leaves no egg active.
 *
 * Before every valid start callback, the manager automatically restores 50 Hz
 * updates, normal engine mode, transparent index 0, hidden built-in logo,
 * disabled raster interrupts, cleared overrun/collision state, no sprites,
 * empty background/foreground 2DPTs and empty resource descriptors. Resource
 * RAM bytes are retained intentionally, but cannot be referenced through stale
 * descriptors or scene objects.
 */
extern const twodfx_egg_definition_t
    twodfx_egg_registry[TWODFX_EGG_COUNT];

bool twodfx_egg_read_f9(uint8_t *value);

/**
 * Returns the sticky egg-input overflow indication.
 *
 * @return
 *     true if at least one incoming byte was discarded because the FIFO was
 *     full; false otherwise.
 *
 * When the finite FIFO is full, already queued bytes are preserved and the
 * newest arriving byte is discarded. The indication remains set until it is
 * explicitly cleared or egg input state is reset by a new selection/stop.
 */
bool twodfx_egg_f9_overflowed(void);

/** Clears the sticky egg-input FIFO overflow indication. */
void twodfx_egg_clear_f9_overflow(void);

#ifdef __cplusplus
}
#endif

#endif /* TWODFX_EGG_H */
