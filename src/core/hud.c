#include "hud.h"
#include "tables.h"
#include <string.h>

/* Dashboard HUD per re/notes/renderer.md §9 — stateless redraw over the
 * pristine dashboard already present in the framebuffer. */

typedef struct {
    uint16_t ofs;
    uint8_t w, h;
    const uint8_t *cells;
} seg_rec;

static int speed_rec(const sr_assets *a, int i, seg_rec *r)
{
    if (!a->speed_dat || (size_t)(i * 2 + 2) > a->speed_size)
        return 0;
    uint16_t off = (uint16_t)(a->speed_dat[i*2] | (a->speed_dat[i*2+1] << 8));
    const uint8_t *p = a->speed_dat + 34 * 2 + off;
    if (p + 4 > a->speed_dat + a->speed_size)
        return 0;
    r->ofs = (uint16_t)(p[0] | (p[1] << 8));
    r->w = p[2];
    r->h = p[3];
    r->cells = p + 4;
    return 1;
}

static void draw_cells(sr_fb *fb, const uint8_t *pristine, uint16_t ofs,
                       int w, int h, const uint8_t *cells,
                       uint8_t c1, uint8_t c2, int lit)
{
    int x0 = ofs % 320, y0 = ofs / 320;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t v = cells[y * w + x];
            if (!v)
                continue;
            int idx = (y0 + y) * 320 + x0 + x;
            if (idx < 0 || idx >= 320 * 200)
                continue;
            if (lit)
                fb->px[idx] = v == 1 ? c1 : c2;
            else
                fb->px[idx] = pristine[idx];
        }
}

static void draw_digit(sr_fb *fb, int x, int y, int d)
{
    for (int j = 0; j < 5; j++)
        for (int i = 0; i < 4; i++) {
            uint8_t v = sr_digits[d][j * 4 + i];
            if (!v)
                continue;
            fb->px[(y + j) * 320 + x + i] = v == 1 ? 0x61 : 0x62;
        }
}

void sr_hud_draw(sr_fb *fb, const sr_assets *a, const uint8_t *pristine,
                 const sr_play *p, uint32_t tick)
{
    /* speedometer: 34 segments, displayed speed excludes autopilot delta */
    int32_t disp = p->speed - p->ap_delta;
    if (disp < 0) disp = 0;
    int sp_segs = (int)(disp / 0x141);
    if (sp_segs > 34) sp_segs = 34;
    for (int i = 0; i < 34; i++) {
        seg_rec r;
        if (!speed_rec(a, i, &r))
            break;
        draw_cells(fb, pristine, r.ofs, r.w, r.h, r.cells,
                   0x5e, 0x5f, i < sp_segs);
    }
    /* oxygen (left) / fuel (right): ceil(qty/3000), 10 segments */
    int oxy_segs = (p->oxy + 0xbb7) / 0xbb8;
    int ful_segs = (p->fuel + 0xbb7) / 0xbb8;
    if (oxy_segs > 10) oxy_segs = 10;
    if (ful_segs > 10) ful_segs = 10;
    for (int i = 0; i < 10; i++) {
        const sr_gauge_seg *s = &a->oxy[i];
        draw_cells(fb, pristine, s->screen_ofs, s->w, s->h, s->cells,
                   0x5e, 0x5f, i < oxy_segs);
        s = &a->ful[i];
        draw_cells(fb, pristine, s->screen_ofs, s->w, s->h, s->cells,
                   0x5e, 0x5f, i < ful_segs);
    }
    /* progress bar: 30 columns at x=0x2a.., slot height probed from art */
    int rows = p->road->rows;
    int steps = 0;
    if (rows > 3) {
        int32_t denom = (int32_t)(((int64_t)(rows - 3) << 16) / 30);
        if (denom > 0)
            steps = (int)(((int64_t)p->z - 0x30000) / denom);
    }
    if (steps < 0) steps = 0;
    if (steps > 29) steps = 29;
    for (int i = 0; i < steps; i++) {
        int x = 0x2a + i;
        int y = 143;
        uint8_t slot = pristine[y * 320 + x];
        int yy = y;
        while (yy >= 138 && pristine[yy * 320 + x] == slot) {
            fb->px[yy * 320 + x] = 0x60;
            yy--;
        }
    }
    /* jump-o-master light: 26x5 at (203,156) */
    {
        const uint8_t *st = sr_aplight[p->ap_light ? 1 : 0];
        for (int j = 0; j < 5; j++)
            for (int i = 0; i < 26; i++) {
                uint8_t v = st[j * 26 + i];
                if (v)
                    fb->px[(156 + j) * 320 + 203 + i] = 0x62;
            }
    }
    /* GRAV-O-METER: 4 digits at (0x60,0x9c), value (g-3)*100, leading-zero
     * suppression */
    {
        int value = ((int)p->road->gravity - 3) * 100;
        if (value < 0) value = 0;
        static const int div[4] = { 1, 10, 100, 1000 };
        for (int si = 0; si < 4; si++) {
            int d = (value / div[si]) % 10;
            draw_digit(fb, 0x60 + (3 - si) * 5, 0x9c, d);
            if (value / div[si] / 10 == 0)
                break;                       /* leading-zero stop */
        }
    }
    (void)tick;
}
