#include "play.h"
#include <string.h>

/* Data tables from the EXE data segment (verified re/image.bin). */
const uint16_t sr_blocktop[6] = { 0x2800, 0x3200, 0x3200, 0x3200, 0x3c00, 0x3c00 };
const uint16_t sr_tun_inner[38] = {
	0x10,0x10,0x10,0x10,0x0f,0x0e,0x0d,0x0b,0x08,0x07,0x06,0x05,0x03,
	0x03,0x03,0x03,0x03,0x03,0x02,0x01,0x00,0x00,0x00,0x00,0x00,0x00,
	0x01,0x02,0x03,0x03,0x03,0x03,0x03,0x03,0x05,0x06,0x07,0x08
};
const uint16_t sr_tun_outer[38] = {
	0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,
	0x20,0x20,0x20,0x20,0x1f,0x1f,0x1f,0x1f,0x1f,0x1e,0x1e,0x1e,0x1d,
	0x1d,0x1d,0x1c,0x1b,0x1a,0x19,0x18,0x16,0x14,0x12,0x11,0x0e
};

void sr_play_init(sr_play *p, const sr_road *road,
				 const uint8_t *demo, size_t demo_size)
{
	memset(p, 0, sizeof *p);
	p->road = road;
	p->demo = demo;
	p->demo_size = demo_size;
	p->x = 0x8000;						/* center of column 3 */
	p->y = 0x2800;
	p->last_new_y = 0x2800;
	p->z = (uint32_t)3 << 16;			/* spawn at row 3 */
	p->fuel = 0x7530;
	p->oxy = 0x7530;
	p->autopilot_on = 1;
	/* grav_accel = -(g * 0x1680 / 0x190) (fn_1f2c 0x1f4d) */
	p->grav_accel = (int16_t)-(((int32_t)road->gravity * 0x1680) / 0x190);
}

/* ---- fn_04c0: tile lookup -------------------------------------------- */
uint16_t sr_tile_at(const sr_play *p, uint32_t z, uint16_t x)
{
	uint16_t col = (uint16_t)(x / 0x80 - 0x5f);
	if (col >= 0x142)
		return 0;
	uint32_t row = z >> 16;
	if (row >= 500)
		return 0;						/* buffer is zero past the road */
	return p->road->cells[row * 7 + col / 0x2e];
}

/* ---- fn_1584: block profile solidity --------------------------------- */
static int solid_block(uint16_t tile, int16_t t, uint16_t y)
{
	if ((uint16_t)t > 0x25)
		return 0;
	/* EXE 0x159b: (y + 0xDE00) / 0x80, UNSIGNED — y below 0x2200 wraps to a
	 * huge row index (=> not inside tunnels, but >= inner for bores) */
	uint16_t di = (uint16_t)(y + 0xde00) / 0x80;
	switch (tile & 0xf00) {
		case 0x100:	/* tunnel */
			return di >= sr_tun_inner[t] && di < sr_tun_outer[t];
		case 0x200:	/* half block */
			return y < 0x3200;
		case 0x300:	/* half block with bore */
			return y < 0x3200 && di >= sr_tun_inner[t];
		case 0x400:	/* full block */
			return y < 0x3c00;
		case 0x500:	/* full block with bore */
			return y < 0x3c00 && di >= sr_tun_inner[t];
		default:
			return 0;
	}
}

/* ---- fn_1685: point solidity (ship half-width 0x700) ------------------ */
int sr_solid(const sr_play *p, uint32_t z, uint16_t x, uint16_t y)
{
	uint16_t right = sr_tile_at(p, z, (uint16_t)(x + 0x700));
	uint16_t left = sr_tile_at(p, z, (uint16_t)(x - 0x700));
	if ((right | left) & 0xf) {				 /* road deck slab */
		if (y < 0x2800 && (uint16_t)(y + 0x600) > 0x2480)
			return 1;
	}
	if ((uint16_t)(y + 0x680) <= 0x2800)
		return 0;
	if (!(left & 0xf00) && !(right & 0xf00))
		return 0;
	uint16_t center = sr_tile_at(p, z, x);
	int16_t t = (int16_t)(0x17 - ((x / 0x80 - 0x31) % 0x2e));
	int16_t xoff = -0x1700;
	if (t <= 0) {
		t = (int16_t)(1 - t);
		xoff = 0x1700;
	}
	if (solid_block(center, t, y))
		return 1;
	if (solid_block(sr_tile_at(p, z, (uint16_t)(x + xoff)), (int16_t)(0x2f - t), y))
		return 1;
	return 0;
}

/* ---- fn_0533: inside tunnel mouth (finish gate / render flag) ---------- */
int sr_in_tunnel(const sr_play *p, uint32_t z, uint16_t x, uint16_t y)
{
	uint16_t tile = sr_tile_at(p, z, x);
	uint16_t bt = tile & 0xf00;
	if (bt != 0x100 && bt != 0x300 && bt != 0x500)
		return 0;
	int16_t t = (int16_t)(0x17 - ((x / 0x80 - 0x31) % 0x2e));
	if (t <= 0)
		t = (int16_t)(1 - t);
	if ((uint16_t)t > 0x25)
		return 0;
	uint16_t di = (uint16_t)(y + 0xde00) / 0x80;	/* unsigned, see 0x159b */
	return di < (sr_tun_outer[t] + sr_tun_inner[t]) / 2;
}

/* ---- fn_17be: swept move with per-axis refinement ---------------------- */
static void swept_move(sr_play *p, uint32_t nz, uint16_t nx, uint16_t ny)
{
	if (nz == p->z && nx == p->x && ny == p->y)
		return;
	int32_t dz = (int32_t)(nz - p->z);
	int16_t dx = (int16_t)(nx - p->x);
	int16_t dy = (int16_t)(ny - p->y);

	/* coarse 5-step sweep */
	int si = 1;
	for (; si <= 5; si++) {
		uint32_t tz = p->z + (uint32_t)((int64_t)dz * si / 5);
		uint16_t tx = (uint16_t)(p->x + dx * si / 5);
		uint16_t ty = (uint16_t)(p->y + dy * si / 5);
		if (sr_solid(p, tz, tx, ty))
			break;
	}
	int safe = si - 1;
	p->z += (uint32_t)((int64_t)dz * safe / 5);
	p->x = (uint16_t)(p->x + dx * safe / 5);
	p->y = (uint16_t)(p->y + dy * safe / 5);
	if (si > 5)
		return;							 /* full move applied */

	/* z refine: step 0x1000 then /16 down to 1 */
	for (int32_t step = 0x1000; step > 0; step /= 0x10) {
		while (1) {
			int32_t remaining = (int32_t)(nz - p->z);
			if (dz >= 0 ? (remaining < step) : (remaining > -step))
				break;
			uint32_t tz = p->z + (uint32_t)(dz >= 0 ? step : -step);
			if (sr_solid(p, tz, p->x, p->y))
				break;
			p->z = tz;
		}
	}
	/* x refine: step 0x7d then /5 */
	for (int16_t step = 0x7d; step > 0; step /= 5) {
		while (1) {
			int16_t remaining = (int16_t)(nx - p->x);
			if (dx >= 0 ? (remaining < step) : (remaining > -step))
				break;
			uint16_t tx = (uint16_t)(p->x + (dx >= 0 ? step : -step));
			if (sr_solid(p, p->z, tx, p->y))
				break;
			p->x = tx;
		}
	}
	/* y refine */
	for (int16_t step = 0x7d; step > 0; step /= 5) {
		while (1) {
			int16_t remaining = (int16_t)(ny - p->y);
			if (dy >= 0 ? (remaining < step) : (remaining > -step))
				break;
			uint16_t ty = (uint16_t)(p->y + (dy >= 0 ? step : -step));
			if (sr_solid(p, p->z, p->x, ty))
				break;
			p->y = ty;
		}
	}
}

/* ---- input (fn_074c) --------------------------------------------------- */
void sr_play_input(sr_play *p, const sr_input *in)
{
	if (p->demo) {
		uint32_t idx = p->z / 0x666;
		uint8_t b = idx < p->demo_size ? p->demo[idx] : 0;
		p->accel = (int16_t)((b & 3) - 1);
		p->steer = (int16_t)(((b >> 2) & 3) - 1);
		p->jumpkey = (b >> 4) & 1;
		return;
	}
	int right = in->held[SR_KEY_RIGHT] | in->held[SR_KEY_PGUP] | in->held[SR_KEY_PGDN];
	int left = in->held[SR_KEY_LEFT] | in->held[SR_KEY_HOME] | in->held[SR_KEY_END];
	int up	= in->held[SR_KEY_UP] | in->held[SR_KEY_HOME] | in->held[SR_KEY_PGUP];
	int down = in->held[SR_KEY_DOWN] | in->held[SR_KEY_END] | in->held[SR_KEY_PGDN];
	p->steer = (int16_t)(right - left);
	p->accel = (int16_t)(up - down);
	p->jumpkey = in->held[SR_KEY_JUMP];
}

/* ---- autopilot (fn_1bb5 / fn_1c20 / fn_1d4d) --------------------------- */
static int ap_bad_cell(const sr_play *p, uint32_t z, uint16_t x)
{
	uint16_t code = sr_tile_at(p, z, x);
	uint16_t type = code & 0xf00;
	if (type == 0x100)
		return 0;							/* tunnel roof is fine */
	if (type)
		code >>= 4;						 /* block-top surface */
	uint16_t surf = code & 0xf;
	if (surf == 0)
		return type == 0;					/* hole only if no block
												(fn_1bb5 0x1bfd: zero-surface
												block tops are landable) */
	return surf == 0xc;					 /* burning */
}

static int ap_sim_lands(const sr_play *p, int32_t speed, int16_t xvel)
{
	uint32_t z = p->z;
	int32_t x = p->x;
	int32_t y = p->y;
	int16_t yvel = p->yvel;
	uint32_t prev_z = z;
	int32_t prev_x = x;
	int guard = 0;
	while (y > 0x2800 && guard++ < 4096) {
		prev_z = z; prev_x = x;
		yvel = (int16_t)(yvel + p->grav_accel);
		z += (uint32_t)speed;
		x += p->side_push + (int32_t)xvel * (speed + 0x618) / 0x200;
		if (x < 0x2f80 || x > 0xd080)
			return 0;
		y += yvel;
		speed += p->accel * 0x4b;
		if (speed < 0) speed = 0;
		if (speed > 0x2aaa) speed = 0x2aaa;
	}
	return !ap_bad_cell(p, z, (uint16_t)x) &&
			!ap_bad_cell(p, prev_z, (uint16_t)prev_x);
}

static void autopilot(sr_play *p)
{
	if (ap_sim_lands(p, p->speed, p->xvel)) {
		p->ap_delta = 0;
		return;
	}
	int32_t speed0 = p->speed;
	for (int di = 1; di <= 6; di++) {
		int16_t xv = (int16_t)(p->xvel + (int32_t)p->xvel * di / 10);
		if (ap_sim_lands(p, p->speed, xv)) { p->xvel = xv; goto fixed; }
		xv = (int16_t)(p->xvel - (int32_t)p->xvel * di / 10);
		if (ap_sim_lands(p, p->speed, xv)) { p->xvel = xv; goto fixed; }
		int32_t sp = p->speed + p->speed * di / 10;
		if (sp < 0x2aaa && ap_sim_lands(p, sp, p->xvel)) { p->speed = sp; goto fixed; }
		sp = p->speed - p->speed * di / 10;
		if (ap_sim_lands(p, sp, p->xvel)) { p->speed = sp; goto fixed; }
	}
	p->speed = speed0;
	p->ap_delta = 0;
	return;
fixed:
	p->ap_delta = p->speed - speed0;
	p->ap_light = 1;
}

/* ---- sfx helper (fn_03c2 surface) -------------------------------------- */
static void sfx(sr_play *p, int n)
{
	p->pending_sfx = n + 1;	 /* 0 = none */
	p->sfx_tick = p->tick;
}

static int sfx_busy(const sr_play *p)			/* fn_0476, SB path */
{
	return p->tick < p->sfx_tick + 8;
}

/* ---- surface effects (fn_1a9c) ----------------------------------------- */
static void surface_effects(sr_play *p, uint16_t code)
{
	switch (code & 0xf) {
		case 0x2:									/* sticky (0x1b0d) */
			if (p->expl_ctr == 0)
				p->speed -= 0x12f;
			break;
		case 0x9:									/* supplies (0x1ad6) */
			if (p->end_state == 0) {
				if (p->fuel < 0x6978 || p->oxy < 0x6978)
					sfx(p, 4);
				p->fuel = 0x7530;
				p->oxy = 0x7530;
			}
			break;
		case 0xa:									/* boost (0x1b25) */
			if (p->expl_ctr == 0)
				p->speed += 0x12f;
			break;
		case 0xc:									/* burning (0x1aab) */
			if (p->end_state == 0)
				p->end_state = 2;
			if (p->expl_ctr == 0) {
				p->expl_ctr = 1;
				sfx(p, 0);
			}
			break;
		default:
			break;
	}
	if (p->speed < 0) p->speed = 0;
	if (p->speed > 0x2aaa) p->speed = 0x2aaa;
}

/* ---- the 36 Hz physics step (fn_1f2c 0x22a3..0x2add) ------------------- */
int sr_play_tick(sr_play *p)
{
	const int rows = p->road->rows;
	p->tick++;

	/* completion fly-away (fn_0e58): 72 ticks of pure z advance */
	if (p->fly_ticks > 0) {
		p->z += (uint32_t)p->speed;
		if (--p->fly_ticks == 0)
			return SR_RES_COMPLETE;
		return SR_RES_RUNNING;
	}

	/* exit checks (§4) */
	if (!(p->expl_ctr != 0 && p->expl_ctr <= 0x2a)) {
		if (p->end_state == 1) return SR_RES_WALL;
		if (p->end_state == 2) return SR_RES_BURNED;
		if (p->end_state == 3 && p->expl_ctr != 0) return SR_RES_FELL;
		if (p->end_frames >= 0x6c) return p->end_state;
	}

	/* 3. tile under ship */
	p->tile_code = sr_tile_at(p, p->z, p->x);
	p->over_hole = (p->tile_code == 0);

	/* 4. surface resolution (only on ground) */
	if (p->on_ground) {
		uint16_t code = p->tile_code;
		if (p->y > 0x2800) {
			uint16_t bt = (code >> 8) & 0xf;
			if (bt < 6 && p->y == sr_blocktop[bt])
				code >>= 4;
			else
				code = 0;
		}
		surface_effects(p, code);
		p->on_ice = ((code & 0xf) == 8);
		p->on_sticky = ((code & 0xf) == 2);
	} else {
		p->on_sticky = 0;					/* ice persists while airborne */
	}

	/* 5. finish check */
	if (p->z >= (((uint32_t)(rows - 1) << 16) + 0x8000) &&
		sr_in_tunnel(p, p->z, p->x, p->y) && p->end_state == 0) {
		p->y = 0;							/* fn_0e58 fly-away */
		p->fly_ticks = 0x48;
		return SR_RES_RUNNING;
	}

	/* 6. landing aftermath / bounce — evaluated against the PREVIOUS tick's
	 * requested y (original keeps new_y in a frame-local across iterations;
	 * the bounce is one tick after the clamp, before gravity reapplies) */
	if (p->y != p->last_new_y) {
		if (p->side_push != 0 && p->edge_dist < 2) {
			p->yvel = 0;
		} else {
			int16_t thresh = (int16_t)((0x104 * (int32_t)p->road->gravity) / 8);
			if ((p->yvel <= -thresh || p->yvel >= thresh) && p->expl_ctr == 0) {
				if (p->end_state == 0 && p->yvel < 0 && !sfx_busy(p))
					sfx(p, 1);
				p->yvel = (int16_t)(-((int32_t)p->yvel * 5) / 10);
			} else {
				p->yvel = 0;
			}
		}
	}

	/* 7. speed from input */
	if (p->end_state == 0) {
		p->speed += p->accel * 0x4b;
		if (p->speed < 0) p->speed = 0;
		if (p->speed > 0x2aaa) p->speed = 0x2aaa;

		/* 8. steering */
		if (!p->on_ice) {
			if (!p->jumping && !p->over_hole) {
				p->xvel = (int16_t)(p->steer * 0x1d);
			} else if (p->xvel == 0 && p->yvel > 0 &&
						(uint16_t)(p->y - p->jump_start_y) < 0xf00) {
				p->xvel = (int16_t)(p->steer * 0x1d);
			}
		}

		/* 9. jump */
		if (!p->jumping && !p->over_hole && p->jumpkey &&
			p->road->gravity < 0x14) {
			p->yvel = 0x480;
			p->jumping = 1;
			p->jump_start_y = p->y;
		}
	}

	/* 10. autopilot */
	if (p->autopilot_on && p->jumping && !p->ap_done && p->y >= 0x3700) {
		autopilot(p);
		p->ap_done = 1;
	}

	/* 11. gravity */
	if (p->expl_ctr == 0) {
		if (p->y >= 0x2800)
			p->yvel = (int16_t)(p->yvel + p->grav_accel);
		else if (p->yvel > -0x6a)
			p->yvel = -0x6a;
	} else {
		if (p->yvel < 0) p->yvel = 0;
		if (p->yvel < 0x47) p->yvel = (int16_t)(p->yvel + 0x27);
		else p->yvel = 0x47;
	}

	/* 12. integrate */
	uint32_t new_z = p->z + (uint32_t)p->speed;
	/* lateral term: speed + 0x618 normally; sticky floors LOSE the bonus
	 * (EXE 0x2634: jnz -> 0, fallthrough -> 0x618) */
	int32_t eff = p->speed + (p->on_sticky ? 0 : 0x618);
	uint16_t new_x = (uint16_t)(p->x + (int16_t)((int32_t)p->xvel * eff / 0x200)
								+ p->side_push);
	if ((p->x < 0x2f80 && new_x > 0xd080) || (p->x > 0xd080 && new_x < 0x2f80))
		new_x = p->x;
	uint16_t new_y = (uint16_t)(p->y + p->yvel);

	/* 13. collision resolve */
	swept_move(p, new_z, new_x, new_y);
	p->last_new_y = new_y;

	/* 14. blocked-in-z corner nudge */
	if (p->z != new_z && p->x == new_x) {
		if (sr_solid(p, new_z, p->x, p->y)) {
			if (!sr_solid(p, new_z, (uint16_t)(p->x - 0x3a0), p->y)) {
				p->x -= 0x3a0;
				new_z = p->z;
				sfx(p, 2);
			} else if (!sr_solid(p, new_z, (uint16_t)(p->x + 0x3a0), p->y)) {
				p->x += 0x3a0;
				new_z = p->z;
				sfx(p, 2);
			}
		}
	}

	/* 15. wall hit (0x279f-0x281a): speed zeroed on BOTH paths; the
	 * explosion fires whenever not already exploding, but end_state only
	 * upgrades from 0 */
	if (p->z != new_z) {
		if (p->speed >= 0xe38) {
			if (p->expl_ctr == 0) {
				p->expl_ctr = 1;
				sfx(p, 0);
				if (p->end_state == 0)
					p->end_state = 1;
			}
		} else {
			if (p->z > new_z - (uint32_t)p->speed)
				sfx(p, 2);
		}
		p->speed = 0;
	}

	/* 16. blocked-in-x */
	if (p->x != new_x) {
		p->xvel = 0;
		if ((p->side_push > 0 && new_x > p->x) ||
			(p->side_push < 0 && new_x < p->x))
			p->side_push = 0;
		p->speed -= 0x97;
		if (p->speed < 0) p->speed = 0;
		if (p->speed > 0x2aaa) p->speed = 0x2aaa;
	}

	/* 17. landing */
	p->on_ground = 0;
	if (p->y != new_y && p->yvel < 0) {
		p->ap_light = 0;
		p->ap_done = 0;
		p->jumping = 0;
		p->on_ground = 1;
		/* revert autopilot speed correction (0x28ea..0x294d) */
		p->speed -= p->ap_delta;
		if (p->speed < 0) p->speed = 0;
		if (p->speed > 0x2aaa) p->speed = 0x2aaa;
		p->ap_delta = 0;

		/* edge-balance scan */
		int16_t balance = 0;
		p->edge_dist = 0x7fff;
		for (int i = 1; i <= 0xe; i++) {
			if (!sr_solid(p, p->z, (uint16_t)(p->x + i * 0x80),
						 (uint16_t)(p->y - 1))) {
				balance++;
				p->edge_dist = (int16_t)i;
				break;
			}
		}
		for (int i = 1; i <= 0xe; i++) {
			if (!sr_solid(p, p->z, (uint16_t)(p->x - i * 0x80),
						 (uint16_t)(p->y - 1))) {
				balance--;
				p->edge_dist = (int16_t)i;
				break;
			}
		}
		if (balance != 0)
			p->side_push = (int16_t)(p->side_push + balance * 0x11);
		else
			p->side_push = 0;
	}

	/* 18. y wrap guard */
	if (p->y > 0x7fff)
		p->y = 0;

	/* 19. fuel & oxygen + 20. death checks (0x2a19-0x2ad8): drains and
	 * death tests only while alive; the three death causes each OVERWRITE
	 * (last match wins); end_frames does NOT count the tick that sets
	 * end_state */
	if (p->end_state == 0) {
		uint16_t oxy_div = (uint16_t)(0x24 * p->road->word3);
		if (oxy_div) {
			p->oxy = (int16_t)(p->oxy - 0x7530 / oxy_div);
			if (p->oxy < 0) p->oxy = 0;
		}
		if (p->road->word2) {
			int32_t q = 0x7530 / p->road->word2;
			p->fuel = (int16_t)(p->fuel - (int16_t)((q * p->speed) >> 16));
			if (p->fuel < 0) p->fuel = 0;
		}
		if (p->y < 0x2800)
			p->end_state = 3;
		if (p->fuel == 0)
			p->end_state = 4;
		if (p->oxy == 0)
			p->end_state = 5;
	} else {
		p->end_frames++;
	}
	if (p->expl_ctr != 0)
		p->expl_ctr++;

	return SR_RES_RUNNING;
}
