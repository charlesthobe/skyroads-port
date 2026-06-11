/* 8x8 text renderer (fn_450a equivalent: 8px advance, transparent
 * background, set bits = color). The original uses the BIOS ROM font;
 * we ship a public-domain lookalike (font8x8, Marcel Sondaar/Daniel
 * Hepper). */
#ifndef SR_TEXT_H
#define SR_TEXT_H

#include "sr.h"
#include "gfx.h"

void sr_text(sr_fb *fb, int x, int y, const char *s, uint8_t color);
int  sr_text_width(const char *s);

#endif
