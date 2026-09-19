#include "assets.h"
#include "lzs.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- helpers ---------------------------------------------------------- */

static void *xmalloc(size_t n)
{
	void *p = malloc(n ? n : 1);
	return p;
}

static bool read_whole(sr_assets *a, const char *name, uint8_t **buf, size_t *size)
{
	*buf = a->io.read_file(name, size);
	return *buf != NULL;
}

/* ---- graphics LZS (CMAP/PICT sections) -------------------------------- */

/* Load a chunked graphics file. bases[] gives the palette base for each
 * consecutive CMAP section (last entry repeats); CMAP colors are written
 * into pal[base..], pict pixels are rebased by +base (0 stays 0) — exactly
 * what the original does at load time (EXE 0x3f75 / 0x4036). */
static bool load_gfx(sr_assets *a, const char *name, sr_rgb6 *pal,
					 const int *bases, int n_bases, sr_gfxfile *out)
{
	uint8_t *data;
	size_t size;
	if (!read_whole(a, name, &data, &size))
		return false;

	lzs_stream s;
	lzs_init(&s, data, size, 0);

	sr_pict *picts = NULL;
	int n = 0, cap = 0, section = -1;
	int base = 0;

	while (s.next_pos < size) {
		uint8_t tag[4];
		lzs_raw(&s, tag, 4);
		if (!memcmp(tag, "CMAP", 4)) {
			section++;
			base = bases[section < n_bases ? section : n_bases - 1];
			int count = lzs_byte(&s);
			sr_pal_section *sec = NULL;
			if (out->n_sections < (int)(sizeof out->sections / sizeof *out->sections))
				sec = &out->sections[out->n_sections++];
			if (sec) { sec->base = base; sec->count = count; }
			for (int i = 0; i < count; i++) {
				sr_rgb6 c;
				c.r = lzs_byte(&s); c.g = lzs_byte(&s); c.b = lzs_byte(&s);
				if (base + i < 256)
					pal[base + i] = c;
				if (sec && i < 256)
					sec->colors[i] = c;
			}
			for (int i = 0; i < count * 2; i++)
				lzs_advance(&s);					/* EGA palette: skip */
		} else if (!memcmp(tag, "PICT", 4)) {
			sr_pict p;
			p.screen_ofs = lzs_u16(&s);
			p.h = lzs_u16(&s);
			p.w = lzs_u16(&s);
			p.pixels = xmalloc((size_t)p.w * p.h);
			lzs_decompress(&s, p.pixels, (size_t)p.w * p.h);
			for (size_t i = 0; i < (size_t)p.w * p.h; i++)
				if (p.pixels[i])
					p.pixels[i] = (uint8_t)(p.pixels[i] + base);
			if (n == cap) {
				cap = cap ? cap * 2 : 4;
				picts = realloc(picts, (size_t)cap * sizeof(*picts));
			}
			picts[n++] = p;
		} else {
			break; /* trailing garbage / unknown container */
		}
	}
	free(data);
	out->picts = picts;
	out->n_picts = n;
	return n > 0;
}

void sr_gfx_apply_section(const sr_gfxfile *g, int idx, sr_rgb6 *pal)
{
	if (idx < 0 || idx >= g->n_sections)
		return;
	const sr_pal_section *sec = &g->sections[idx];
	for (int i = 0; i < sec->count && sec->base + i < 256; i++)
		pal[sec->base + i] = sec->colors[i];
}

void sr_gfx_apply_pal(const sr_gfxfile *g, sr_rgb6 *pal)
{
	for (int s = 0; s < g->n_sections; s++)
		sr_gfx_apply_section(g, s, pal);
}

void sr_assets_free_gfx(sr_gfxfile *g)
{
	for (int i = 0; i < g->n_picts; i++)
		free(g->picts[i].pixels);
	free(g->picts);
	g->picts = NULL;
	g->n_picts = 0;
}

/* ---- roads ------------------------------------------------------------ */

bool sr_assets_load_road(sr_assets *a, int entry, sr_road *road)
{
	uint8_t *data;
	size_t size;
	if (!read_whole(a, "roads.lzs", &data, &size))
		return false;

	uint16_t first = *(uint16_t*)data;
	int count = first / 4;
	if (entry < 0 || entry >= count) {
		free(data);
		return false;
	}
	uint16_t off = ((uint16_t*)data)[entry*2];
	uint16_t raw = ((uint16_t*)data)[entry*2+1];

	lzs_stream s;
	lzs_init(&s, data, size, off);
	road->gravity = lzs_u16(&s);
	road->word2	= lzs_u16(&s);
	road->word3	= lzs_u16(&s);
	for (int i = 0; i < 72; i++) {
		road->palette[i].r = lzs_byte(&s);
		road->palette[i].g = lzs_byte(&s);
		road->palette[i].b = lzs_byte(&s);
		a->game_pal[i] = road->palette[i];
	}
	road->rows = raw / 14;
	/* original clears a 500-row buffer; mirror that so reads past the end
	 * of short roads see zeroes */
	road->cells = xmalloc(500 * 7 * sizeof(uint16_t));
	memset(road->cells, 0, 500 * 7 * sizeof(uint16_t));
	lzs_decompress(&s, (uint8_t *)road->cells, raw);
	free(data);
	return true;
}

bool sr_assets_load_world(sr_assets *a, int world)
{
	char name[16];
	snprintf(name, sizeof name, "world%d.lzs", world);
	sr_assets_free_gfx(&a->world);
	static const int base[] = { 142 };
	return load_gfx(a, name, a->game_pal, base, 1, &a->world);
}

/* ---- gauges (ful_disp.dat / oxy_disp.dat) ------------------------------ */

static bool load_gauge(sr_assets *a, const char *name, sr_gauge_seg *segs)
{
	uint8_t *d;
	size_t size;
	if (!read_whole(a, name, &d, &size))
		return false;
	for (int i = 0; i < 10; i++) {
		uint16_t off = ((uint16_t*)d)[i];
		const uint8_t *r = d + 20 + off;
		segs[i].screen_ofs = *(uint16_t*)r;
		segs[i].w = r[2];
		segs[i].h = r[3];
		size_t n = (size_t)segs[i].w * segs[i].h;
		if (n > sizeof segs[i].cells) n = sizeof segs[i].cells;
		memcpy(segs[i].cells, r + 4, n);
	}
	free(d);
	return true;
}

/* ---- trekdat ----------------------------------------------------------- */

static bool load_trekdat(sr_assets *a)
{
	uint8_t *d;
	size_t size;
	if (!read_whole(a, "trekdat.lzs", &d, &size))
		return false;

	lzs_stream s;
	lzs_init(&s, d, size, 0);
	int cap = 0;
	a->n_trek = 0;
	a->trek = NULL;
	/* records until stream exhausted; mirror EXE 0xBB loop */
	while (s.next_pos + 4 <= size + 1) {
		uint32_t raw = lzs_u16(&s);
		uint32_t comp = lzs_u16(&s);
		if (raw == 0 || comp > raw)
			break;
		sr_trek_obj o;
		o.raw_size = raw;
		o.data_ofs = raw - comp;
		o.data = xmalloc(raw);
		memset(o.data, 0, raw);
		o.data[0] = (uint8_t)(o.data_ofs & 0xff);
		o.data[1] = (uint8_t)(o.data_ofs >> 8);
		lzs_decompress(&s, o.data + o.data_ofs, comp);
		if (a->n_trek == cap) {
			cap = cap ? cap * 2 : 16;
			a->trek = realloc(a->trek, (size_t)cap * sizeof(*a->trek));
		}
		a->trek[a->n_trek++] = o;
		if (s.next_pos >= size)
			break;
	}
	free(d);
	return a->n_trek > 0;
}

/* ---- anim.lzs (intro animation) ---------------------------------------- */

static bool load_anim(sr_assets *a)
{
	uint8_t *d;
	size_t size;
	if (!read_whole(a, "anim.lzs", &d, &size))
		return false;
	lzs_stream s;
	lzs_init(&s, d, size, 0);
	lzs_u16(&s);							 /* 'AN' */
	lzs_u16(&s);							 /* 'IM' */
	a->anim_frames = lzs_u16(&s);
	/* CMAP object at base 0 (fn_3fc6) */
	{
		uint8_t tag[4];
		lzs_raw(&s, tag, 4);
		int count = lzs_byte(&s);
		a->anim_pal.base = 0;
		a->anim_pal.count = count;
		for (int i = 0; i < count && i < 256; i++) {
			a->anim_pal.colors[i].r = lzs_byte(&s);
			a->anim_pal.colors[i].g = lzs_byte(&s);
			a->anim_pal.colors[i].b = lzs_byte(&s);
		}
		for (int i = 0; i < count * 2; i++)
			lzs_advance(&s);
	}
	int cap = 0;
	a->n_anim = 0;
	for (int f = 0; f < a->anim_frames; f++) {
		int count = lzs_u16(&s);
		for (int i = 0; i < count && a->n_anim < 300; i++) {
			sr_anim_rec r;
			r.frame = (uint16_t)f;
			/* PICT (fn_4068 wire format) */
			lzs_u16(&s);						/* tag half 1 */
			lzs_u16(&s);						/* tag half 2 */
			r.pict.screen_ofs = lzs_u16(&s);
			r.pict.h = lzs_u16(&s);
			r.pict.w = lzs_u16(&s);
			r.pict.pixels = xmalloc((size_t)r.pict.w * r.pict.h);
			lzs_decompress(&s, r.pict.pixels, (size_t)r.pict.w * r.pict.h);
			if (a->n_anim == cap) {
				cap = cap ? cap * 2 : 64;
				a->anim = realloc(a->anim, (size_t)cap * sizeof(*a->anim));
			}
			a->anim[a->n_anim++] = r;
		}
	}
	free(d);
	return a->n_anim > 0;
}

/* ---- top level --------------------------------------------------------- */

bool sr_assets_load(sr_assets *a, sr_io io, char *err, size_t errlen)
{
	memset(a, 0, sizeof *a);
	a->io = io;

#define TRY(x, what) do { if (!(x)) { snprintf(err, errlen, "%s", what); return false; } } while (0)

	static const int b_main[] = { 190 };
	static const int b_set[] = { 200, 250 };
	static const int b_help[] = { 200 };
	static const int b_go[]	= { 0, 240 };
	static const int b_intro[] = { 0 };
	static const int b_cars[] = { 72 };
	static const int b_dash[] = { 92 };

	TRY(load_gfx(a, "mainmenu.lzs", a->menu_pal, b_main, 1, &a->mainmenu), "mainmenu.lzs");
	TRY(load_gfx(a, "setmenu.lzs", a->menu_pal, b_set, 2, &a->setmenu), "setmenu.lzs");
	TRY(load_gfx(a, "helpmenu.lzs", a->menu_pal, b_help, 1, &a->helpmenu), "helpmenu.lzs");
	TRY(load_gfx(a, "gomenu.lzs",	a->menu_pal, b_go,	2, &a->gomenu),	"gomenu.lzs");
	TRY(load_gfx(a, "intro.lzs",	a->menu_pal, b_intro,1, &a->intro),	"intro.lzs");
	TRY(load_gfx(a, "cars.lzs",	 a->game_pal, b_cars, 1, &a->cars),	 "cars.lzs");
	TRY(load_gfx(a, "dashbrd.lzs", a->game_pal, b_dash, 1, &a->dashbrd), "dashbrd.lzs");
	TRY(load_trekdat(a), "trekdat.lzs");
	TRY(load_gauge(a, "oxy_disp.dat", a->oxy), "oxy_disp.dat");
	TRY(load_gauge(a, "ful_disp.dat", a->ful), "ful_disp.dat");
	TRY(read_whole(a, "speed.dat", &a->speed_dat, &a->speed_size), "speed.dat");
	TRY(read_whole(a, "demo.rec", &a->demo, &a->demo_size), "demo.rec");
	TRY(load_anim(a), "anim.lzs");
	TRY(read_whole(a, "intro.snd", &a->intro_snd, &a->intro_snd_size), "intro.snd");
#undef TRY
	return true;
}
