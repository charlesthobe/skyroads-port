#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/audio.h"

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

int main(int argc, char **argv)
{
    g_dir = argc > 1 ? argv[1] : ".";
    int song = argc > 2 ? atoi(argv[2]) : 1;
    sr_assets *as = calloc(1, sizeof *as);
    char err[256];
    if (!sr_assets_load(as, (sr_io){ io_read_file, NULL }, err, sizeof err)) {
        fprintf(stderr, "%s\n", err); return 1;
    }
    sr_audio *a = sr_audio_create();
    if (!sr_audio_music(a, as, song)) { fprintf(stderr, "song load failed\n"); return 1; }

    int seconds = 8, frames = SR_AUDIO_RATE * seconds;
    int16_t *buf = malloc((size_t)frames * 4);
    sr_audio_render(a, buf, frames);

    double rms = 0; int16_t peak = 0;
    for (int i = 0; i < frames * 2; i++) {
        rms += (double)buf[i] * buf[i];
        if (abs(buf[i]) > peak) peak = (int16_t)abs(buf[i]);
    }
    rms = __builtin_sqrt(rms / (frames * 2));
    printf("song %d: rms=%.1f peak=%d\n", song, rms, peak);

    FILE *f = fopen("/tmp/srdump/song.wav", "wb");
    uint32_t dlen = (uint32_t)frames * 4, flen = dlen + 36;
    fwrite("RIFF", 1, 4, f); fwrite(&flen, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    uint32_t fmtlen = 16; uint16_t pcm = 1, ch = 2, bits = 16;
    uint32_t rate = SR_AUDIO_RATE, brate = rate * 4; uint16_t align = 4;
    fwrite(&fmtlen,4,1,f); fwrite(&pcm,2,1,f); fwrite(&ch,2,1,f);
    fwrite(&rate,4,1,f); fwrite(&brate,4,1,f); fwrite(&align,2,1,f); fwrite(&bits,2,1,f);
    fwrite("data",1,4,f); fwrite(&dlen,4,1,f); fwrite(buf,1,dlen,f);
    fclose(f);
    printf("wrote /tmp/srdump/song.wav\n");
    return rms > 100 ? 0 : 2;
}
