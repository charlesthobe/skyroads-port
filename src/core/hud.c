#include "hud.h"
#include "tables.h"
#include "play.h"

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
	uint16_t off = ((uint16_t*)a->speed_dat)[i];
	const uint8_t *p = a->speed_dat + 34 * 2 + off;
	if (p + 4 > a->speed_dat + a->speed_size)
		return 0;
	r->ofs = *(uint16_t*)p;
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

static void draw_digit(sr_fb *fb, int x, int y, int digit) {
	for (int i = 0; i < 5; i++) {
		for (int j = 0; j < 4; j++) {
			uint8_t v = sr_digits[digit][i * 4 + j];
			switch (v) {
				case 0:
					fb->px[(y + i) * 320 + x + j] = 0;
					break;
				case 1:
					fb->px[(y + i) * 320 + x + j] = 0x61;
					break;
				case 2:
					fb->px[(y + i) * 320 + x + j] = 0x62;
			}
		}
	}
}

void swap_hud_area_color(sr_fb *fb, const uint8_t *pristine, int x, int y, int width, int height,
						int match_color, int new_color) {
	for (int i = 0; i < width; i++) {
		int yy = y;
		while (yy <= y + height) {
			if (pristine[yy * 320 + x] == match_color) {
				fb->px[yy * 320 + x] = new_color;
			}
			yy++;
		}
		x++;
	}
}

void sr_hud_draw(sr_fb *fb, const sr_assets *a, const uint8_t *pristine, sr_play *p, const uint32_t tick) {
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
	/* out of oxygen */
	if (p->end_state == 5) {
		switch (tick % BEEP_INTERVAL) {
			case 0:
			case 1: // Persist HUD warning for an extra adjacent tick
				swap_hud_area_color(fb, pristine, 160, 161, 7, 6, 0x63, 0x64);
		}
	}
	/* out of fuel */
	if (p->end_state == 4) {
		switch (tick % BEEP_INTERVAL) {
			case 0:
			case 1: // Persist HUD warning for an extra adjacent tick
				swap_hud_area_color(fb, pristine, 155, 169, 16, 4, 0x63, 0x64);
		}
	}
	/* progress bar: 30 columns at x=42.., a slot is 6 pixels at its thickest */
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
		swap_hud_area_color(fb, pristine, 42 + i, 140, 1, 6, 0x65, 0x60);
	}
	/* jump-o-master light: 26x5 at (203,156) */
	{
		// Apparently sr_aplight[0] just says "IDLE" which is the same as the "pristine" HUD, nonetheless we let it be used for now at least.
		const uint8_t *st = sr_aplight[p->ap_light ? 1 : 0];
		for (int i = 0; i < 5; i++)
			for (int j = 0; j < 26; j++) {
				uint8_t v = st[i * 26 + j];
				switch (v) {
					case 0:
						fb->px[(156 + i) * 320 + 203 + j] = 0;
						break;
					case 5:
						fb->px[(156 + i) * 320 + 203 + j] = 0x61;
						break;
					case 6:
						fb->px[(156 + i) * 320 + 203 + j] = 0x62;
				}
			}
	}
	// GRAV-O-METER: 1 digit at (0x65 + 5,0x9c), (value = gravity - 3), and 2 prerendered zeros from the HUD.
	// Technically can handle 2 digits although this never happens in the original game.
	{
		int value = ((int)p->road->gravity - 3);
		if (value < 0 || value > 99) {
			value = 0;
		}
		int decimal_place = 0;
		while (value > 0) {
			int digit = value % 10;
			value = value / 10;
			draw_digit(fb, 0x65 - (5 * decimal_place), 0x9c, digit);
			decimal_place++;
		}
	}
}
