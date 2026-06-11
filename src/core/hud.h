#ifndef SR_HUD_H
#define SR_HUD_H

#include "sr.h"
#include "assets.h"
#include "gfx.h"
#include "play.h"

void sr_hud_draw(sr_fb *fb, const sr_assets *a, const uint8_t *pristine,
                 const sr_play *p, uint32_t tick);

#endif
