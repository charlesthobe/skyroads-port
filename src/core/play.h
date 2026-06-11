/* Gameplay physics/logic — a faithful reimplementation of SKYROADS.EXE
 * fn_1f2c and its callees. Every constant and step order comes from
 * re/notes/gameloop.md (disassembly-verified). Original DS-relative
 * variable names are kept in comments for cross-reference. */
#ifndef SR_PLAY_H
#define SR_PLAY_H

#include "sr.h"
#include "assets.h"

enum {
    SR_RES_RUNNING   = -1,
    SR_RES_COMPLETE  = 0,
    SR_RES_WALL      = 1,
    SR_RES_BURNED    = 2,
    SR_RES_FELL      = 3,
    SR_RES_NO_FUEL   = 4,
    SR_RES_NO_OXYGEN = 5,
    SR_RES_QUIT      = 7
};

typedef struct {
    const sr_road *road;
    const uint8_t *demo;       /* DEMO.REC buffer or NULL */
    size_t demo_size;

    /* ship state (original fixed-point units) */
    uint16_t x;                /* [0xaf2c] 0x80/px, start 0x8000 */
    uint16_t y;                /* [0xaf3c] deck = 0x2800 */
    uint32_t z;                /* [0x9628/2a] hi word = row */
    int32_t  speed;            /* [0x54b8/ba] 0..0x2aaa */
    int16_t  yvel;             /* [0x9342] */
    int16_t  xvel;             /* [0x4576] */
    int16_t  side_push;        /* [0x54a2] */
    int16_t  grav_accel;       /* [0x54b6] = -(g*0x1680/0x190) */
    uint16_t last_new_y;       /* fn_1f2c local [bp-0x1c], persists ticks */

    int16_t  fuel, oxy;        /* [0x54a0]/[0xb14c] 0..0x7530 */

    /* inputs (set per frame) */
    int16_t steer, accel;      /* [0x9600]/[0x933c] -1/0/1 */
    int16_t jumpkey;           /* [0x5488] */

    /* flags/counters */
    int16_t end_state;         /* [0x457c] */
    int16_t expl_ctr;          /* [0x4578] */
    int16_t end_frames;        /* [0x4566] */
    int16_t jumping;           /* [bp-0x8] */
    uint16_t jump_start_y;     /* [bp-0xa] */
    int16_t on_ground;         /* [bp-0xc] */
    int16_t on_ice;            /* [bp-0xe] */
    int16_t on_sticky;         /* [bp-0x10] */
    int16_t over_hole;         /* [bp-0x12] */
    uint16_t tile_code;        /* [bp-0x14] */
    int16_t edge_dist;         /* [bp-0x18] */
    int16_t ap_done;           /* [bp-0x6] */
    int32_t ap_delta;          /* [0xaf3e/40] */
    int16_t ap_light;          /* [0x4568] */
    int16_t autopilot_on;      /* [0x457e] = 1 */
    uint32_t tick;             /* [0x160c] mirror for animations */
    int16_t fly_ticks;         /* completion fly-away countdown */
    int16_t pending_sfx;       /* fn_03c2 calls surfaced to the shell */
    uint32_t sfx_tick;         /* [0xaf48] */
} sr_play;

void sr_play_init(sr_play *p, const sr_road *road,
                  const uint8_t *demo, size_t demo_size);
/* One 36Hz physics tick (§5). Returns SR_RES_*. */
int sr_play_tick(sr_play *p);
/* Input mapping per fn_074c keyboard path; demo mode reads DEMO.REC. */
void sr_play_input(sr_play *p, const sr_input *in);

/* Collision queries (also used by the renderer for shadows). */
uint16_t sr_tile_at(const sr_play *p, uint32_t z, uint16_t x);  /* fn_04c0 */
int sr_solid(const sr_play *p, uint32_t z, uint16_t x, uint16_t y); /* fn_1685 */
int sr_in_tunnel(const sr_play *p, uint32_t z, uint16_t x, uint16_t y); /* fn_0533 */

extern const uint16_t sr_blocktop[6];     /* ds:0xde */
extern const uint16_t sr_tun_inner[38];   /* ds:0x46 */
extern const uint16_t sr_tun_outer[38];   /* ds:0x92 */

#endif
