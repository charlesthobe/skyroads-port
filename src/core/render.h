/* Road viewport renderer — faithful reimplementation of fn_2d03/fn_0be3.
 * The pseudo-3D is fully pre-baked in TREKDAT.LZS span tables; this module
 * replays them exactly (full repaint per frame — pixel-identical to the
 * original's incremental update). Spec: re/notes/renderer.md */
#ifndef SR_RENDER_H
#define SR_RENDER_H

#include "sr.h"
#include "assets.h"
#include "gfx.h"
#include "play.h"

typedef struct {
    uint8_t *exp[8];          /* expanded phase objects (fn_3a7a) */
    uint8_t pristine[SR_SCREEN_W * SR_SCREEN_H];  /* backdrop + dashboard */
} sr_render;

bool sr_render_init(sr_render *r, const sr_assets *a);
/* Rebuild pristine screen for the current world backdrop + dashboard. */
void sr_render_set_world(sr_render *r, const sr_assets *a);

/* Draw one gameplay frame into fb (viewport + ship + shadow).
 * Mirrors fn_0be3's computations from play state. */
void sr_render_frame(sr_render *r, sr_fb *fb, const sr_assets *a,
                     const sr_play *p, uint32_t tick, int on_sticky);

void sr_render_free(sr_render *r);

#endif
