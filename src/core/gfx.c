#include "gfx.h"
#include <string.h>

void sr_fb_clear(sr_fb *fb, uint8_t color)
{
    memset(fb->px, color, sizeof fb->px);
}

void sr_blit_pict(sr_fb *fb, const sr_pict *p, bool opaque)
{
    int x0 = p->screen_ofs % SR_SCREEN_W;
    int y0 = p->screen_ofs / SR_SCREEN_W;
    for (int y = 0; y < p->h; y++) {
        int dy = y0 + y;
        if (dy < 0 || dy >= SR_SCREEN_H) continue;
        const uint8_t *src = p->pixels + (size_t)y * p->w;
        uint8_t *dst = fb->px + dy * SR_SCREEN_W + x0;
        if (opaque) {
            int w = p->w;
            if (x0 + w > SR_SCREEN_W) w = SR_SCREEN_W - x0;
            memcpy(dst, src, (size_t)w);
        } else {
            for (int x = 0; x < p->w && x0 + x < SR_SCREEN_W; x++)
                if (src[x])
                    dst[x] = src[x];
        }
    }
}

void sr_blit_at(sr_fb *fb, const uint8_t *pixels, int w, int h, int x, int y)
{
    for (int j = 0; j < h; j++) {
        int dy = y + j;
        if (dy < 0 || dy >= SR_SCREEN_H) continue;
        for (int i = 0; i < w; i++) {
            int dx = x + i;
            if (dx < 0 || dx >= SR_SCREEN_W) continue;
            uint8_t c = pixels[(size_t)j * w + i];
            if (c)
                fb->px[dy * SR_SCREEN_W + dx] = c;
        }
    }
}

void sr_blit_strip(sr_fb *fb, const uint8_t *strip, int strip_w,
                   int src_y, int w, int h, int x, int y)
{
    sr_blit_at(fb, strip + (size_t)src_y * strip_w, w, h, x, y);
}

void sr_palette_rgba(const sr_rgb6 *pal, uint32_t *out256)
{
    for (int i = 0; i < 256; i++) {
        uint32_t r = (uint32_t)((pal[i].r << 2) | (pal[i].r >> 4));
        uint32_t g = (uint32_t)((pal[i].g << 2) | (pal[i].g >> 4));
        uint32_t b = (uint32_t)((pal[i].b << 2) | (pal[i].b >> 4));
        out256[i] = 0xff000000u | (r << 16) | (g << 8) | b;
    }
}
