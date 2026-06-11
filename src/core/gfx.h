/* 320x200 8bpp framebuffer ops mirroring the original's mode 13h drawing. */
#ifndef SR_GFX_H
#define SR_GFX_H

#include "sr.h"
#include "assets.h"

typedef struct {
    uint8_t px[SR_SCREEN_W * SR_SCREEN_H];
} sr_fb;

void sr_fb_clear(sr_fb *fb, uint8_t color);
/* Blit a PICT at its embedded screen offset; color 0 transparent unless
 * opaque is set (full-screen backgrounds are drawn opaque). */
void sr_blit_pict(sr_fb *fb, const sr_pict *p, bool opaque);
/* Blit at an explicit position (sprites). */
void sr_blit_at(sr_fb *fb, const uint8_t *pixels, int w, int h, int x, int y);
/* Sub-rectangle of a strip (cars sheet) at position, transparent-0. */
void sr_blit_strip(sr_fb *fb, const uint8_t *strip, int strip_w,
                   int src_y, int w, int h, int x, int y);

/* Expand 6-bit palette to 8-bit RGBA (platform present). */
void sr_palette_rgba(const sr_rgb6 *pal, uint32_t *out256);

#endif
