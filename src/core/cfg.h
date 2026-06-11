/* skyroads.cfg — 66 bytes (re/notes/audio_misc.md §5):
 *   u16 checksum = sum over i=1..32 of (word_i XOR i)
 *   u16 control device (0 kbd / 1 joystick / 2 mouse)
 *   u16 sound off
 *   30 x u16 per-road completion counters
 * Bad checksum -> all zeroed (like the original). */
#ifndef SR_CFG_H
#define SR_CFG_H

#include "sr.h"

typedef struct {
    uint16_t control;
    uint16_t sound_off;
    uint16_t completions[30];
} sr_cfg;

void sr_cfg_load(sr_cfg *c, const sr_io *io);
void sr_cfg_save(const sr_cfg *c, const sr_io *io);

#endif
