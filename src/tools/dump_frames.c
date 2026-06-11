/* Headless harness: boot the core, simulate input, dump PPM screenshots.
 * Usage: sr_dump <data_dir> <out_dir> */
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
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        void *buf = malloc((size_t)size);
        if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
            fclose(f); free(buf); return NULL;
        }
        fclose(f);
        *out_size = (size_t)size;
        return buf;
    }
    return NULL;
}

static void write_ppm(const sr_game *g, const char *path)
{
    uint32_t pal[256];
    sr_palette_rgba(g->out_pal, pal);
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", SR_SCREEN_W, SR_SCREEN_H);
    for (int i = 0; i < SR_SCREEN_W * SR_SCREEN_H; i++) {
        uint32_t c = pal[g->fb.px[i]];
        fputc((c >> 16) & 0xff, f);
        fputc((c >> 8) & 0xff, f);
        fputc(c & 0xff, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    g_dir = argc > 1 ? argv[1] : ".";
    const char *out = argc > 2 ? argv[2] : ".";
    char err[256], path[1300];
    sr_game *g = malloc(sizeof *g);
    if (!sr_game_init(g, (sr_io){ io_read_file, NULL }, err, sizeof err)) {
        fprintf(stderr, "load failed: %s\n", err);
        return 1;
    }
    sr_input in = { 0 };

    for (int i = 0; i < 40; i++)                /* boot fade-in */
        sr_game_tick(g, &in);
    snprintf(path, sizeof path, "%s/frame_mainmenu.ppm", out);
    write_ppm(g, path);

    in.pressed[SR_KEY_ENTER] = 1;
    sr_game_tick(g, &in);                       /* enter gomenu */
    memset(in.pressed, 0, sizeof in.pressed);
    for (int i = 0; i < 80; i++)                /* fade out + in */
        sr_game_tick(g, &in);
    snprintf(path, sizeof path, "%s/frame_gomenu.ppm", out);
    write_ppm(g, path);

    in.pressed[SR_KEY_ENTER] = 1;               /* start road 1 */
    sr_game_tick(g, &in);
    memset(in.pressed, 0, sizeof in.pressed);
    for (int i = 0; i < 80 + 36; i++)
        sr_game_tick(g, &in);
    snprintf(path, sizeof path, "%s/frame_game.ppm", out);
    write_ppm(g, path);

    printf("ok: road rows=%d gravity=%d w2=%d w3=%d trek_objs=%d\n",
           g->road.rows, g->road.gravity, g->road.word2, g->road.word3,
           g->assets.n_trek);
    return 0;
}
