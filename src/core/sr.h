/* SkyRoads portable port — common definitions.
 *
 * Reverse engineered from SKYROADS.EXE (Bluemoon Interactive, 1993).
 * readme.txt: "This is abandonware. Feel free to modify, reverse engineer,
 * and change the game exactly as you want."
 *
 * The core is platform-clean C11: no I/O other than through sr_io, no
 * floating point in game logic (the original is pure 16-bit integer math).
 */
#ifndef SR_H
#define SR_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define SR_SCREEN_W 320
#define SR_SCREEN_H 200
#define SR_VIEW_H   138   /* 3D viewport height; dashboard below */

/* Original timing: PIT divisor 6628 -> 180.018 Hz IRQ; game tick on 2 of 10
 * phases = 36.0036 Hz. Expressed as exact rational ticks per second. */
#define SR_TICK_NUM 1193182u   /* PIT base Hz */
#define SR_TICK_DEN (6628u * 5u)

enum {
    SR_KEY_UP, SR_KEY_DOWN, SR_KEY_LEFT, SR_KEY_RIGHT,
    SR_KEY_HOME, SR_KEY_PGUP, SR_KEY_END, SR_KEY_PGDN,
    SR_KEY_ESC, SR_KEY_JUMP, SR_KEY_PAUSE, SR_KEY_ENTER,
    SR_KEY_COUNT
};

typedef struct {
    uint8_t held[SR_KEY_COUNT];   /* bit0: currently held */
    /* edge-triggered presses appended by platform between ticks */
    uint8_t pressed[SR_KEY_COUNT];
} sr_input;

typedef struct {
    uint8_t r, g, b;              /* 6-bit VGA DAC values 0..63 */
} sr_rgb6;

typedef struct sr_game sr_game;

/* Platform-supplied file access (so web/iOS bundles can redirect). */
typedef struct {
    /* Read whole file into a malloc'd buffer; returns NULL if missing. */
    void *(*read_file)(const char *name, size_t *out_size);
    /* Persist a small file (skyroads.cfg); may be NULL (no persistence). */
    bool (*write_file)(const char *name, const void *data, size_t size);
} sr_io;

#endif
