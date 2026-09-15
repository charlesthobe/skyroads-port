/* Original data file loaders. The port reads the UNMODIFIED retail files;
 * formats per docs/FORMATS.md. */
#ifndef SR_ASSETS_H
#define SR_ASSETS_H

#include "sr.h"

/* One PICT from a graphics LZS, pixels already rebased (+palette base,
 * 0 stays 0) exactly like the in-game representation. */
typedef struct {
	uint16_t screen_ofs;	/* y*320+x destination */
	uint16_t w, h;
	uint8_t *pixels;		/* w*h */
} sr_pict;

typedef struct {
	int base, count;
	sr_rgb6 colors[256];
} sr_pal_section;

typedef struct {
	sr_pict *picts;
	int n_picts;
	sr_pal_section sections[8];
	int n_sections;
} sr_gfxfile;

/* Re-apply a file's CMAP sections to a palette buffer (the original loads
 * each screen's palette when the screen opens; order matters). */
void sr_gfx_apply_pal(const sr_gfxfile *g, sr_rgb6 *pal);
/* Apply a single CMAP section (files whose later sections belong to other
 * sequences, e.g. intro fade palettes). */
void sr_gfx_apply_section(const sr_gfxfile *g, int idx, sr_rgb6 *pal);

/* Road grid: 7 u16 codes per row. */
typedef struct {
	uint16_t gravity;	 /* dash shows (gravity-3)*100 */
	uint16_t word2;		/* fuel-related (see notes) */
	uint16_t word3;		/* oxygen-related */
	sr_rgb6 palette[72];
	uint16_t *cells;		/* rows*7 */
	int rows;
} sr_road;

typedef struct {
	uint8_t *data;		 /* raw_size bytes; LZS payload at tail */
	uint32_t raw_size;
	uint32_t data_ofs;	 /* raw_size - lzs_size (stored at head u16 too) */
} sr_trek_obj;

typedef struct {
	uint16_t screen_ofs;
	uint8_t w, h;
	uint8_t cells[64];	 /* w*h values 0..2 */
} sr_gauge_seg;

typedef struct {
	uint16_t frame;			/* anim frame number this delta belongs to */
	sr_pict pict;
} sr_anim_rec;

typedef struct sr_assets {
	sr_io io;

	/* graphics LZS files, loaded with their in-game palette bases */
	sr_gfxfile mainmenu, setmenu, helpmenu, gomenu, intro, cars, dashbrd;
	sr_gfxfile world;		 /* current world backdrop */
	sr_rgb6 game_pal[256];	 /* ds:0x41d0 equivalent */
	sr_rgb6 menu_pal[256];	 /* ds:0x5182 equivalent */

	sr_trek_obj *trek;		 /* trekdat.lzs objects */
	int n_trek;

	sr_gauge_seg oxy[10], ful[10];

	sr_anim_rec *anim;		 /* anim.lzs delta picts */
	int n_anim;
	int anim_frames;
	sr_pal_section anim_pal;	/* ANIM's CMAP (base 0) */

	uint8_t *intro_snd;		/* raw PCM, played at TC 0x5A */
	size_t intro_snd_size;

	uint8_t *speed_dat;		/* raw for now */
	size_t speed_size;
	uint8_t *demo;			 /* demo.rec, 6398 bytes */
	size_t demo_size;
} sr_assets;

/* Load everything needed at boot (menus, trekdat, gauges, demo).
 * Returns false (and fills err) on missing/corrupt files. */
bool sr_assets_load(sr_assets *a, sr_io io, char *err, size_t errlen);

/* Per-road loads (mirror the original's load order/palette bases). */
bool sr_assets_load_road(sr_assets *a, int entry, sr_road *road);
bool sr_assets_load_world(sr_assets *a, int world);

void sr_assets_free_gfx(sr_gfxfile *g);

#endif
