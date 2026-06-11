/* Render frames at several z positions/states to exercise all 8 phases,
 * block shapes, tunnels, jumps. Writes PPMs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/game.h"

static const char *g_dir;
static void *io_read_file(const char *name, size_t *out_size)
{
    char path[1200], upper[64];
    size_t n = strlen(name);
    for (size_t i = 0; i <= n; i++) {
        char c = name[i];
        upper[i] = (char)((c >= 'a' && c <= 'z') ? c - 32 : c);
    }
    for (int attempt = 0; attempt < 2; attempt++) {
        snprintf(path, sizeof path, "%s/%s", g_dir, attempt ? upper : name);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET);
        void *buf = malloc((size_t)size);
        if (fread(buf, 1, (size_t)size, f) != (size_t)size) { fclose(f); free(buf); return NULL; }
        fclose(f); *out_size = (size_t)size; return buf;
    }
    return NULL;
}

static void write_ppm(sr_game *g, const char *path)
{
    uint32_t pal[256];
    sr_palette_rgba(g->out_pal, pal);
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n320 200\n255\n");
    for (int i = 0; i < 320*200; i++) {
        uint32_t c = pal[g->fb.px[i]];
        fputc((c>>16)&0xff,f); fputc((c>>8)&0xff,f); fputc(c&0xff,f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    g_dir = argc > 1 ? argv[1] : ".";
    const char *out = argc > 2 ? argv[2] : ".";
    int entry = argc > 3 ? atoi(argv[3]) : 1;
    char err[256], path[1300];
    sr_game *g = malloc(sizeof *g);
    if (!sr_game_init(g, (sr_io){ io_read_file, NULL }, err, sizeof err)) {
        fprintf(stderr, "load failed: %s\n", err); return 1;
    }
    if (entry == 0) {
        /* intro mode: just run from boot and dump frames */
        sr_input in0 = { 0 };
        for (int t = 0; t < 36 * 16; t++) {
            sr_game_tick(g, &in0);
            if (t % 72 == 0) {
                snprintf(path, sizeof path, "%s/intro_%04d.ppm", out, t);
                write_ppm(g, path);
            }
        }
        printf("intro done; state=%d\n", (int)g->state);
        return 0;
    }
    g->road_entry = entry;
    g->go_sel = entry - 1;
    if (!sr_assets_load_road(&g->assets, entry, &g->road)) return 1;
    sr_assets_load_world(&g->assets, (entry - 1) / 3);
    sr_render_set_world(&g->render, &g->assets);
    sr_play_init(&g->play, &g->road, NULL, 0);
    g->state = SR_ST_GAME;
    g->fade = SR_FADE_NONE;

    sr_input in = { 0 };
    in.held[SR_KEY_UP] = 1;                  /* accelerate */
    for (int t = 0; t < 36 * 12; t++) {
        if (t == 36*6) in.held[SR_KEY_JUMP] = 1;
        if (t == 36*6 + 18) in.held[SR_KEY_JUMP] = 0;
        sr_game_tick(g, &in);
        if (t % 60 == 0 || t == 36*6 + 8) {
            snprintf(path, sizeof path, "%s/rt_%04d.ppm", out, t);
            write_ppm(g, path);
        }
        if (g->state != SR_ST_GAME) {
            printf("left game state at t=%d (row %u)\n", t, (unsigned)(g->play.z>>16));
            break;
        }
    }
    snprintf(path, sizeof path, "%s/rt_final.ppm", out);
    write_ppm(g, path);
    printf("done row=%u speed=%x\n", (unsigned)(g->play.z >> 16), (unsigned)g->play.speed);
    return 0;
}
