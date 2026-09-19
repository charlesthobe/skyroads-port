#include "game.h"
#include "hud.h"
#include "text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ init */

static void start_intro(sr_game* g) {
  g->want_song = 0;
  g->fade = SR_FADE_IN;
  g->fade_t = 0;
  sr_fb_clear(&g->fb, 0);
  if (g->assets.intro.n_picts > 0)
    sr_blit_pict(&g->fb, &g->assets.intro.picts[0], true);
  memset(g->cur_pal, 0, sizeof g->cur_pal);
  for (int i = 0; i < g->assets.anim_pal.count && i < 256; i++)
    g->cur_pal[g->assets.anim_pal.base + i] = g->assets.anim_pal.colors[i];

  // Reset intro
  g->intro_t = 0;
  g->intro_rec = 0;
}

bool sr_game_init(sr_game* g, sr_io io, char* err, size_t errlen) {
  memset(g, 0, sizeof *g);
  if (!sr_assets_load(&g->assets, io, err, errlen))
    return false;
  if (!sr_render_init(&g->render, &g->assets))
    return false;
  sr_cfg_load(&g->cfg, &g->assets.io);
  /* boot into the intro (fn_4575): black -> ANIM palette + title pict,
   * fade in, sound, animation */
  g->state = SR_ST_INTRO;
  start_intro(g);
  return true;
}

bool sr_game_running(const sr_game* g) { return g->state != SR_ST_QUIT; }

/* ------------------------------------------------- state transitions ---- */

static void start_road(sr_game* g, int entry, int demo) {
  g->road_entry = entry;
  if (!sr_assets_load_road(&g->assets, entry, &g->road))
    return;
  sr_assets_load_world(&g->assets, demo ? 0 : (entry - 1) / 3);
  sr_render_set_world(&g->render, &g->assets);
  sr_play_init(&g->play, &g->road, demo ? g->assets.demo : NULL,
               demo ? g->assets.demo_size : 0);
  g->demo_mode = demo ? 1 : 0;
  g->paused = 0;
  g->tick = 0;
  if (!demo && g->state != SR_ST_DIED) {
    /* gameplay music: random song 2..13, (main 0x29f) */
    static bool seeded = false;
    if (!seeded) {
      srand(time(NULL));
    }
    int song = (rand() % (13 - 2 + 1)) + 2;
    g->want_song = song;
  }
  if (g->state == SR_ST_DIED) {
    g->state = SR_ST_GAME;
  }
}

static void enter_state(sr_game* g, sr_state st) {
  g->state = st;
  g->idle_ticks = 0;
  switch (st) {
  case SR_ST_INTRO:
    start_intro(g);
    break;
  case SR_ST_GAME:
  case SR_ST_DIED:
    if (g->demo_mode == 2) /* queued attract demo */
      start_road(g, 0, 1);
    else if (g->road_entry)
      start_road(g, g->road_entry, 0);
    break;
  case SR_ST_MAINMENU:
    g->want_song = 1;
    g->demo_mode = 0;
    break;
  case SR_ST_GOMENU:
    g->want_song = 1;
    g->demo_mode = 0;
    break;
  default:;
  }
}

/* fade out (36 ticks), switch, fade in (36 ticks) — fn_4b72 pacing */
static void fade_to(sr_game* g, sr_state st) {
  g->fade = SR_FADE_OUT;
  g->fade_t = 36;
  g->fade_target = st;
}

/* ------------------------------------------------------------- drawing -- */

static void draw_mainmenu(sr_game* g) {
  sr_fb_clear(&g->fb, 0);
  if (g->assets.intro.n_picts > 0)
    sr_blit_pict(&g->fb, &g->assets.intro.picts[0], true);
  int v = g->menu_sel;
  if (v >= 0 && v < g->assets.mainmenu.n_picts)
    sr_blit_pict(&g->fb, &g->assets.mainmenu.picts[v], false);
  /* original order (fn_4e36): intro.lzs FIRST cmap (title), mainmenu
   * box section at 190; later intro cmaps are logo fade palettes */
  memset(g->cur_pal, 0, sizeof g->cur_pal);
  sr_gfx_apply_section(&g->assets.intro, 0, g->cur_pal);
  sr_gfx_apply_pal(&g->assets.mainmenu, g->cur_pal);
}

/* Road entry screen position (fn_5064): base 0xF3E=(62,12), +9 rows per
 * road, +39 per world, +0xA0 right column. Ticks at 0x11F0=(112,14),
 * 7px apart, max 7 (fn_5164). */
static int gomenu_road_ofs(int rd) {
  int ofs = 0xF3E + ((rd / 3) % 5) * 39 * 320 + (rd % 3) * 9 * 320;
  if (rd >= 15)
    ofs += 0xA0;
  return ofs;
}

static void draw_gomenu(sr_game* g) {
  sr_fb_clear(&g->fb, 0);
  if (g->assets.gomenu.n_picts > 0)
    sr_blit_pict(&g->fb, &g->assets.gomenu.picts[0], true);
  /* completion tick marks (fn_5164 loop) */
  if (g->assets.gomenu.n_picts > 1) {
    for (int rd = 0; rd < 30; rd++) {
      int n = g->cfg.completions[rd];
      if (n > 7)
        n = 7;
      int ofs = 0x11F0 + ((rd % 15) % 3) * 9 * 320 +
                ((rd % 15) / 3) * 39 * 320 + (rd >= 15 ? 0xA0 : 0);
      for (int t = 0; t < n; t++) {
        sr_pict cur = g->assets.gomenu.picts[1];
        cur.screen_ofs = (uint16_t)(ofs + t * 7);
        sr_blit_pict(&g->fb, &cur, false);
      }
    }
  }
  /* selection highlight: white rectangle around the 48x9 road-name area */
  int ofs = gomenu_road_ofs(g->go_sel);
  int x0 = ofs % 320, y0 = ofs / 320;
  int x_max = x0 + 48;
  int y_max = y0 + 9;
  for (int y = y0; y < y_max; y++)
    for (int x = x0; x < x_max; x++) {
      if (x > x0 && x < x_max - 1 && y > y0 && y < y_max - 1)
        continue;
      g->fb.px[y * 320 + x] = 0x1;
    }
  memset(g->cur_pal, 0, sizeof g->cur_pal);
  sr_gfx_apply_pal(&g->assets.gomenu, g->cur_pal);
}

/* ------------------------------------------------------------ state ticks */

static bool any_pressed(const sr_input* in) {
  for (int i = 0; i < SR_KEY_COUNT; i++)
    if (in->pressed[i])
      return true;
  return false;
}

/* Intro (fn_4575): fade-in 36, +24 ticks -> INTRO.SND, +37 ticks -> ANIM
 * at 2 ticks/frame (18 fps), then to the menu. Any key skips. */
static void tick_intro(sr_game* g, const sr_input* in) {
  if (any_pressed(in)) {
    fade_to(g, SR_ST_MAINMENU);
    return;
  }
  g->intro_t++;
  if (g->intro_t == 24)
    g->want_intro_snd = 1;
  if (g->intro_t < 24 + 37)
    return;
  /* anim playback */
  uint32_t at = g->intro_t - (24 + 37);
  int frame = (int)(at / 2);
  while (g->intro_rec < g->assets.n_anim &&
         g->assets.anim[g->intro_rec].frame <= (uint16_t)frame) {
    sr_blit_pict(&g->fb, &g->assets.anim[g->intro_rec].pict, false);
    g->intro_rec++;
  }
  if (any_pressed(in))
    g->idle_ticks = 0;
  else if (++g->idle_ticks > 36u * 10) { /* attract demo after 10 s */
    g->idle_ticks = 0;
    g->demo_mode = 2;
    fade_to(g, SR_ST_GAME);
  }
}

static void tick_mainmenu(sr_game* g, const sr_input* in) {
  if (in->pressed[SR_KEY_DOWN] && g->menu_sel < 2)
    g->menu_sel++;
  if (in->pressed[SR_KEY_UP] && g->menu_sel > 0)
    g->menu_sel--;
  if (in->pressed[SR_KEY_ESC])
    g->state = SR_ST_QUIT;
  if (in->pressed[SR_KEY_ENTER] || in->pressed[SR_KEY_JUMP]) {
    if (g->menu_sel == 0)
      fade_to(g, SR_ST_GOMENU);
    else if (g->menu_sel == 1)
      fade_to(g, SR_ST_SETMENU);
    else
      fade_to(g, SR_ST_HELP);
  }
  draw_mainmenu(g);
}

static void tick_gomenu(sr_game* g, const sr_input* in) {
  /* fn_5164: up/down +-1 clamp, left/right -+15 */
  static int hold_counter = 0;
  if (any_pressed(in)) {
    hold_counter = 0;
  }
  if (in->held[SR_KEY_UP] || in->held[SR_KEY_DOWN]) {
    if (hold_counter < 18) {
      hold_counter++;
    } else if (in->held[SR_KEY_LEFT] || in->held[SR_KEY_RIGHT]) {
      if (in->held[SR_KEY_UP])
        g->go_sel = 0;
      if (in->held[SR_KEY_DOWN])
        g->go_sel = 29;
    } else {
      if (in->held[SR_KEY_UP] && g->go_sel > 0)
        g->go_sel--;
      if (in->held[SR_KEY_DOWN] && g->go_sel < 29)
        g->go_sel++;
    }
  } else {
    hold_counter = 0;
  }
  if (in->pressed[SR_KEY_UP] && g->go_sel > 0)
    g->go_sel--;
  if (in->pressed[SR_KEY_DOWN] && g->go_sel < 29)
    g->go_sel++;
  if (in->pressed[SR_KEY_LEFT] && g->go_sel >= 15)
    g->go_sel -= 15;
  if (in->pressed[SR_KEY_RIGHT] && g->go_sel < 15)
    g->go_sel += 15;
  if (in->pressed[SR_KEY_ESC])
    fade_to(g, SR_ST_MAINMENU);
  if (in->pressed[SR_KEY_ENTER] || in->pressed[SR_KEY_JUMP]) {
    g->road_entry = g->go_sel + 1; /* entry 0 = demo road */
    g->demo_mode = 0;
    fade_to(g, SR_ST_GAME);
  }
  draw_gomenu(g);
}

/* Settings (fn_4c0e): 0-2 control radio, 3-4 sound on/off. */
static void tick_setmenu(sr_game* g, const sr_input* in) {
  int* sel = &g->menu_sel2;
  static int hold_counter = 0;
  if (any_pressed(in)) {
    hold_counter = 0;
  }
  if (in->held[SR_KEY_LEFT] || in->held[SR_KEY_RIGHT]) {
    if (hold_counter < 18) {
      hold_counter++;
    } else {
      if (in->held[SR_KEY_LEFT] && *sel > 0)
        (*sel)--;
      if (in->held[SR_KEY_RIGHT] && *sel < 4)
        (*sel)++;
    }
  } else {
    hold_counter = 0;
  }
  if (in->pressed[SR_KEY_LEFT] && *sel > 0)
    (*sel)--;
  if (in->pressed[SR_KEY_RIGHT] && *sel < 4)
    (*sel)++;
  if (in->pressed[SR_KEY_UP]) {
    if (*sel == 3)
      *sel = 0;
    else if (*sel == 4)
      *sel = 1;
  }
  if (in->pressed[SR_KEY_DOWN]) {
    if (*sel == 0)
      *sel = 3;
    else if (*sel < 3)
      *sel = 4;
  }
  if (in->pressed[SR_KEY_ENTER] || in->pressed[SR_KEY_JUMP]) {
    if (*sel <= 2)
      g->cfg.control = (uint16_t)*sel;
    else
      g->cfg.sound_off = (uint16_t)(*sel == 4);
    sr_cfg_save(&g->cfg, &g->assets.io);
  }
  if (in->pressed[SR_KEY_ESC])
    fade_to(g, SR_ST_MAINMENU);

  sr_fb_clear(&g->fb, 0);
  if (g->assets.setmenu.n_picts > 0)
    sr_blit_pict(&g->fb, &g->assets.setmenu.picts[0], true);
  switch (g->cfg.control) {
  case 0:
    sr_blit_pict(&g->fb, &g->assets.setmenu.picts[6], false);
    break;
  case 1:
    sr_blit_pict(&g->fb, &g->assets.setmenu.picts[7], false);
    break;
  case 2:
    sr_blit_pict(&g->fb, &g->assets.setmenu.picts[8], false);
  }
  if (g->cfg.sound_off) {
    sr_blit_pict(&g->fb, &g->assets.setmenu.picts[10], false);
  } else {
    sr_blit_pict(&g->fb, &g->assets.setmenu.picts[9], false);
  }
  if (*sel < g->assets.setmenu.n_picts)
    sr_blit_pict(&g->fb, &g->assets.setmenu.picts[*sel + 1], false);
  memset(g->cur_pal, 0, sizeof g->cur_pal);
  sr_gfx_apply_pal(&g->assets.setmenu, g->cur_pal);
}

static void tick_help(sr_game* g, const sr_input* in) {
  if (in->pressed[SR_KEY_ENTER] || in->pressed[SR_KEY_JUMP]) {
    if (++g->help_page >= g->assets.helpmenu.n_picts) {
      g->help_page = 0;
      fade_to(g, SR_ST_MAINMENU);
    }
  }
  if (in->pressed[SR_KEY_ESC]) {
    g->help_page = 0;
    fade_to(g, SR_ST_MAINMENU);
  }
  sr_fb_clear(&g->fb, 0);
  if (g->help_page < g->assets.helpmenu.n_picts)
    sr_blit_pict(&g->fb, &g->assets.helpmenu.picts[g->help_page], true);
  memset(g->cur_pal, 0, sizeof g->cur_pal);
  if (g->assets.helpmenu.n_sections > 0) {
    int s = g->help_page;
    if (s >= g->assets.helpmenu.n_sections)
      s = g->assets.helpmenu.n_sections - 1;
    const sr_pal_section* sec = &g->assets.helpmenu.sections[s];
    for (int i = 0; i < sec->count && sec->base + i < 256; i++)
      g->cur_pal[sec->base + i] = sec->colors[i];
  }
}

static void tick_game(sr_game* g, const sr_input* in) {
  if (g->demo_mode && any_pressed(in)) { /* attract: any key exits */
    fade_to(g, SR_ST_MAINMENU);
    return;
  }

  /* pause (P): freeze; ESC quits to menu, other key resumes (fn_1f2c) */
  if (g->paused) {
    if (in->pressed[SR_KEY_ESC]) {
      g->paused = 0;
      fade_to(g, SR_ST_GOMENU);
    } else if (any_pressed(in)) {
      g->paused = 0;
    }
    return;
  }
  if (!g->demo_mode && in->pressed[SR_KEY_PAUSE]) {
    g->paused = 1;
    return;
  }
  if (in->pressed[SR_KEY_ESC]) { /* result 7 */
    fade_to(g, g->demo_mode ? SR_ST_MAINMENU : SR_ST_GOMENU);
    return;
  }

  g->tick++;
  sr_play_input(&g->play, in);
  int res = sr_play_tick(&g->play);
  if (g->play.pending_sfx) {
    g->sfx_request = g->play.pending_sfx;
    g->play.pending_sfx = 0;
  }

  sr_render_frame(&g->render, &g->fb, &g->assets, &g->play, g->tick,
                  g->play.on_sticky);
  sr_hud_draw(&g->fb, &g->assets, g->render.pristine, &g->play, g->tick);
  memcpy(g->cur_pal, g->assets.game_pal, sizeof g->cur_pal);

  if (res == SR_RES_RUNNING)
    return;

  if (res == SR_RES_COMPLETE) {
    g->roadend_final = false;
    if (!g->demo_mode) {
      int rd = g->road_entry - 1;
      if (rd >= 0 && rd < 30 && g->cfg.completions[rd] < 0xffff)
        g->cfg.completions[rd]++;
      sr_cfg_save(&g->cfg, &g->assets.io);
      int done = 0;
      for (int i = 0; i < 30; i++)
        if (g->cfg.completions[i])
          done++;
      g->roadend_final = (rd == 29 && done == 30);
      if (g->go_sel < 29)
        g->go_sel++;
      /* fn_2b21: text over the final frame */
    }
    sr_text(&g->fb, g->roadend_final ? 0x84 : 0x68, 0x50,
            g->roadend_final ? "The End" : "Road Completed", 0x63);
    g->state = SR_ST_ROADEND;
    return;
  }
  /* deaths (1..5): replay the same road immediately (main 0x3b4) */
  fade_to(g, SR_ST_DIED);
}

static void tick_roadend(sr_game* g) {
  static int counter = 0;
  if (counter < 36) {
    counter++;
    return;
  }
  counter = 0;
  if (g->demo_mode) {
    fade_to(g, SR_ST_INTRO);
    return;
  }
  fade_to(g, g->roadend_final ? SR_ST_MAINMENU : SR_ST_GOMENU);
}

/* ------------------------------------------------------------- main tick */

static void game_tick_inner(sr_game* g, const sr_input* in);
static void apply_fade(sr_game* g);

void sr_game_tick(sr_game* g, const sr_input* in) {
  game_tick_inner(g, in);
  apply_fade(g);
}

static void game_tick_inner(sr_game* g, const sr_input* in) {
  /* fade controller: the original blocks in fn_4b72 while fading */
  if (g->fade == SR_FADE_OUT) {
    if (--g->fade_t <= 0) {
      enter_state(g, g->fade_target);
      g->fade = SR_FADE_IN;
      g->fade_t = 0;
      /* render the new state once so the fade-in has pixels */
      sr_input none = {0};
      switch (g->state) {
      case SR_ST_MAINMENU:
        draw_mainmenu(g);
        break;
      case SR_ST_GOMENU:
        draw_gomenu(g);
        break;
      case SR_ST_SETMENU:
        tick_setmenu(g, &none);
        break;
      case SR_ST_HELP:
        tick_help(g, &none);
        break;
      case SR_ST_GAME:
        tick_game(g, &none);
      default:;
      }
    }
    return;
  }
  if (g->fade == SR_FADE_IN) {
    if (++g->fade_t >= 36)
      g->fade = SR_FADE_NONE;
    if (g->state != SR_ST_GAME && g->state != SR_ST_INTRO)
      return; /* menus frozen during fades */
  }

  switch (g->state) {
  case SR_ST_INTRO:
    tick_intro(g, in);
    break;
  case SR_ST_MAINMENU:
    tick_mainmenu(g, in);
    break;
  case SR_ST_GOMENU:
    tick_gomenu(g, in);
    break;
  case SR_ST_HELP:
    tick_help(g, in);
    break;
  case SR_ST_GAME:
    tick_game(g, in);
    break;
  case SR_ST_SETMENU:
    tick_setmenu(g, in);
    break;
  case SR_ST_ROADEND:
    tick_roadend(g);
    break;
  case SR_ST_QUIT:
  default:;
  }
}

/* fn_4b72/fn_4315: linear DAC scale, t = 100*step/36 percent */
static void apply_fade(sr_game* g) {
  int t;
  switch (g->fade) {
  case SR_FADE_IN:
    t = g->fade_t;
    break;
  case SR_FADE_OUT:
    t = g->fade_t;
    break;
  default:
    t = 36;
    break;
  }
  int pct = t * 100 / 36;
  for (int i = 0; i < 256; i++) {
    g->out_pal[i].r = (uint8_t)(g->cur_pal[i].r * pct / 100);
    g->out_pal[i].g = (uint8_t)(g->cur_pal[i].g * pct / 100);
    g->out_pal[i].b = (uint8_t)(g->cur_pal[i].b * pct / 100);
  }
}
