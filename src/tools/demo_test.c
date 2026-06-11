/* Physics validation: play DEMO.REC through the reimplemented physics on
 * the demo road (roads.lzs entry 0). The recording is position-indexed
 * input for the original engine — if our physics matches, the run
 * completes the road; any deviation strands or kills the ship early. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/assets.h"
#include "../core/play.h"

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

static const char *resname(int r)
{
    switch (r) {
    case SR_RES_COMPLETE: return "COMPLETE";
    case SR_RES_WALL: return "WALL CRASH";
    case SR_RES_BURNED: return "BURNED";
    case SR_RES_FELL: return "FELL OFF";
    case SR_RES_NO_FUEL: return "OUT OF FUEL";
    case SR_RES_NO_OXYGEN: return "OUT OF OXYGEN";
    default: return "?";
    }
}

int main(int argc, char **argv)
{
    g_dir = argc > 1 ? argv[1] : ".";
    sr_assets *a = calloc(1, sizeof *a);
    char err[256];
    if (!sr_assets_load(a, (sr_io){ io_read_file, NULL }, err, sizeof err)) {
        fprintf(stderr, "load: %s\n", err);
        return 1;
    }
    sr_road road;
    if (!sr_assets_load_road(a, 0, &road)) {
        fprintf(stderr, "road 0 load failed\n");
        return 1;
    }
    printf("demo road: %d rows, gravity %d, fuel %d rows, oxygen %d s\n",
           road.rows, road.gravity, road.word2, road.word3);

    sr_play *p = calloc(1, sizeof *p);
    sr_play_init(p, &road, a->demo, a->demo_size);

    int detail_lo = argc > 2 ? atoi(argv[2]) : -1;
    int detail_hi = argc > 3 ? atoi(argv[3]) : -1;
    FILE *csv = fopen("/tmp/sr_truth/mysim.csv", "w");
    int res = SR_RES_RUNNING;
    int t = 0;
    for (; t < 36 * 240 && res == SR_RES_RUNNING; t++) {
        sr_input dummy = { 0 };
        sr_play_input(p, &dummy);                 /* demo mode */
        int row = (int)(p->z >> 16);
        if (row >= detail_lo && row <= detail_hi)
            printf("t=%5d row=%3u.%02u x=%05x y=%04x yv=%6d xv=%4d "
                   "st=%2d ac=%2d jk=%d J=%d hole=%d gnd=%d sp=%04x\n",
                   t, (unsigned)(p->z >> 16),
                   (unsigned)(((p->z & 0xffff) * 100) >> 16),
                   p->x, p->y, p->yvel, p->xvel, p->steer, p->accel,
                   p->jumpkey, p->jumping, p->over_hole, p->on_ground,
                   (unsigned)p->speed);
        res = sr_play_tick(p);
        if (csv)
            fprintf(csv, "%d,%.4f,%d,%04x\n", t, p->z / 65536.0,
                    (int)p->speed, p->x);
        if (detail_lo < 0 && t % 180 == 0)
            printf("t=%5d  row=%3u.%02u  col x=%05x y=%04x speed=%04x "
                   "fuel=%5d oxy=%5d\n",
                   t, (unsigned)(p->z >> 16),
                   (unsigned)(((p->z & 0xffff) * 100) >> 16),
                   p->x, p->y, (unsigned)p->speed, p->fuel, p->oxy);
    }
    if (csv) fclose(csv);
    printf("\nresult after %d ticks (%.1fs): %s at row %u/%d\n",
           t, t / 36.0, resname(res), (unsigned)(p->z >> 16), road.rows);
    return res == SR_RES_COMPLETE ? 0 : 2;
}
