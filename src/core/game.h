#ifndef SR_GAME_H
#define SR_GAME_H

#include "sr.h"
#include "assets.h"
#include "gfx.h"
#include "play.h"
#include "render.h"
#include "cfg.h"

typedef enum {
    SR_ST_INTRO,
    SR_ST_MAINMENU,
    SR_ST_GOMENU,
    SR_ST_SETMENU,
    SR_ST_HELP,
    SR_ST_GAME,
    SR_ST_ROADEND,            /* "Road Completed" / "The End" */
    SR_ST_QUIT
} sr_state;

typedef enum { SR_FADE_NONE, SR_FADE_IN, SR_FADE_OUT } sr_fade;

struct sr_game {
    sr_assets assets;
    sr_fb fb;
    sr_rgb6 cur_pal[256];     /* palette before fade scaling */
    sr_rgb6 out_pal[256];     /* presented palette (fade applied) */
    sr_state state;

    sr_cfg cfg;

    int menu_sel;             /* main menu: 0 start, 1 controls, 2 help */
    int menu_sel2;            /* settings selection 0..4 */
    int go_sel;               /* gomenu: road index 0..29 */
    int help_page;

    /* fade controller (fn_4b72: 36 steps, linear) */
    sr_fade fade;
    int fade_t;               /* 0..36 */
    sr_state fade_target;

    /* gameplay */
    sr_road road;
    int road_entry;
    uint32_t tick;
    sr_play play;
    sr_render render;
    int paused;
    int demo_mode;            /* attract demo running */
    int roadend_final;        /* show "The End" */
    uint32_t idle_ticks;      /* main menu attract timer */

    /* intro sequencing */
    uint32_t intro_t;
    int intro_rec;            /* next anim record */
    int intro_frame;
    int want_intro_snd;       /* 1 = start INTRO.SND playback */

    /* audio requests consumed by the platform shell */
    int want_song;            /* -1 none; matches original song choices */
    int sfx_request;          /* 0 none, else sfx index+1 */
    uint32_t rng;             /* music shuffle state */
};

bool sr_game_init(sr_game *g, sr_io io, char *err, size_t errlen);
/* Advance exactly one 36Hz tick. */
void sr_game_tick(sr_game *g, const sr_input *in);
bool sr_game_running(const sr_game *g);

#endif
