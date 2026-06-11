#include "audio.h"
#include "lzs.h"
#include "../thirdparty/opl3.h"
#include <stdlib.h>
#include <string.h>

/* ---- driver tables (EXE data segment, re/notes/audio_misc.md §2.4-2.6) -- */

static const uint8_t reg_group[11] = {       /* ds:0xc1c */
    0x20,0x40,0x60,0x80,0xe0, 0x20,0x40,0x60,0x80,0xe0, 0xc0
};
static const uint8_t op1_off[11] = {         /* ds:0xc27 */
    0x00,0x01,0x02,0x08,0x09,0x0a, 0x10,0x14,0x12,0x15,0x11
};
static const uint8_t op2_off[11] = {         /* ds:0xc32 */
    0x03,0x04,0x05,0x0b,0x0c,0x0d, 0x13,0xff,0xff,0xff,0xff
};
static const uint8_t c0_idx[11] = {          /* ds:0xc3d */
    0x00,0x01,0x02,0x03,0x04,0x05, 0x06,0x07,0xff,0x08,0xff
};
static const uint8_t vol_tab[31] = {         /* ds:0xc48 */
    0x3f,0x14,0x10,0x0e,0x0c,0x0a,0x09,0x08,0x07,0x06,0x06,0x05,0x05,
    0x04,0x04,0x04,0x04,0x04,0x03,0x03,0x03,0x03,0x02,0x02,0x02,0x01,
    0x01,0x01,0x01,0x00,0x00
};
static const uint8_t fnum_lo[12] = {         /* ds:0xc77 */
    0xac,0xb6,0xc1,0xcd,0xd9,0xe6,0xf3,0x02,0x11,0x22,0x33,0x45
};
static const uint8_t fnum_hi[12] = {         /* ds:0xc83 */
    0,0,0,0,0,0,0,1,1,1,1,1
};
static const uint8_t perc_patch[4][11] = {   /* ds:0xc90, ch 7..10 */
    {0x0c,0x00,0xf8,0xb5,0x00, 0x00,0x00,0xd6,0x4f,0x00, 0x01},
    {0x04,0x00,0xf7,0xb5,0x00, 0x00,0x00,0xd6,0x4f,0x00, 0x01},
    {0x01,0x00,0xf5,0xb5,0x00, 0x00,0x00,0xd6,0x4f,0x00, 0x01},
    {0x01,0x00,0xf7,0xb5,0x00, 0x4e,0x00,0x10,0x00,0x00, 0x01},
};

struct sr_audio {
    opl3_chip chip;
    bool enabled;

    /* music state ([0x31a0..0x31b2]) */
    uint8_t song[0x3e80];
    const uint8_t *instr;      /* instrument base */
    const uint8_t *ev, *loop;  /* event ptr / loop point */
    const uint8_t *song_end;
    uint8_t delay;             /* [0xc8f] */
    uint8_t rhythm;            /* [0x31a6] mirror of reg 0xBD */
    uint8_t chan_instr[11];    /* [0x31a7+] */
    int cur_song;              /* [0xbc2] */
    bool playing;

    int tick_acc;              /* samples until next 180Hz tick */

    /* sfx voice */
    const uint8_t *sfx_data;
    uint8_t *sfx_copy;
    size_t sfx_len;
    uint32_t sfx_pos_fp;       /* 16.16 position */
    uint32_t sfx_step_fp;
};

static void oplw(sr_audio *a, uint8_t reg, uint8_t val)
{
    OPL3_WriteRegBuffered(&a->chip, reg, val);
}

/* ---- driver (0x5889/0x58b1/0x58fd/0x5955/0x59b3/0x59f1) ---------------- */

static void key_off(sr_audio *a, int ch)
{
    if (ch < 6) {
        oplw(a, (uint8_t)(0xb0 + ch), 0x00);
    } else {
        a->rhythm &= (uint8_t)~(0x10 >> (ch - 6));
        oplw(a, 0xbd, a->rhythm);
    }
}

static void program_instr(sr_audio *a, int ch, int n, const uint8_t *rec11)
{
    key_off(a, ch);
    a->chan_instr[ch] = (uint8_t)n;
    for (int b = 0; b < 5; b++)
        oplw(a, (uint8_t)(op1_off[ch] + reg_group[b]), rec11[b]);
    if (op2_off[ch] != 0xff)
        for (int b = 5; b < 10; b++)
            oplw(a, (uint8_t)(op2_off[ch] + reg_group[b]), rec11[b]);
    if (c0_idx[ch] != 0xff)
        oplw(a, (uint8_t)(0xc0 + c0_idx[ch]), rec11[10]);
}

static void opl_reset(sr_audio *a)
{
    a->rhythm = 0xe0;
    for (uint8_t r = 0x40; r <= 0x55; r++)
        oplw(a, r, 0x3f);
    for (int ch = 7; ch >= 0; ch--)
        key_off(a, ch);
}

static void opl_init(sr_audio *a)
{
    opl_reset(a);
    oplw(a, 0x01, 0x20);                     /* wave select enable */
    oplw(a, 0x08, 0x00);
    oplw(a, 0xbd, 0xe0);                     /* AM/VIB depth + rhythm mode */
    for (int ch = 7; ch <= 10; ch++)
        program_instr(a, ch, 0, perc_patch[ch - 7]);
    /* fixed percussion pitches (0x58df) */
    oplw(a, 0xa8, 0xac); oplw(a, 0xb8, 0x0c);
    oplw(a, 0xa7, 0x02); oplw(a, 0xb7, 0x0d);
}

static void note_on(sr_audio *a, int ch, int note)
{
    key_off(a, ch);
    if (ch <= 6) {
        int phys = c0_idx[ch];
        int block = note / 12 + 2;
        int semi = note % 12;
        oplw(a, (uint8_t)(0xa0 + phys), fnum_lo[semi]);
        uint8_t b = (uint8_t)(fnum_hi[semi] | (block << 2));
        if (0xb0 + phys < 0xb6)
            b |= 0x20;                       /* key-on for melodic 0..5 */
        oplw(a, (uint8_t)(0xb0 + phys), b);
        if (ch < 6)
            return;
    }
    a->rhythm |= (uint8_t)(0x10 >> (ch - 6));
    oplw(a, 0xbd, a->rhythm);
}

static void set_volume(sr_audio *a, int ch, int vol)
{
    if (vol > 30) vol = 30;
    const uint8_t *rec = a->instr + a->chan_instr[ch] * 16;
    int single = (op2_off[ch] == 0xff);
    int op = single ? op1_off[ch] : op2_off[ch];
    int tlb = single ? 1 : 6;                /* record byte holding 0x40 reg */
    uint8_t v = rec[tlb];
    uint8_t att = (uint8_t)((v & 0x3f) + vol_tab[vol]);
    if (att > 0x3f) att = 0x3f;
    oplw(a, (uint8_t)(0x40 + op), (uint8_t)((v & 0xc0) | att));
    if (!single && (rec[10] & 1)) {          /* additive: modulator too */
        v = rec[1];
        att = (uint8_t)((v & 0x3f) + vol_tab[vol]);
        if (att > 0x3f) att = 0x3f;
        oplw(a, (uint8_t)(0x40 + op1_off[ch]), (uint8_t)((v & 0xc0) | att));
    }
}

/* ---- event stream (0x5a39) --------------------------------------------- */

static void music_tick(sr_audio *a)
{
    if (!a->playing)
        return;
    if (a->delay) {
        a->delay--;
        return;
    }
    while (a->ev + 1 < a->song_end) {
        uint16_t w = (uint16_t)(a->ev[0] | (a->ev[1] << 8));
        a->ev += 2;
        int op = w & 7;
        int ch = (w >> 4) & 0xf;
        int param = w >> 8;
        switch (op) {
        case 0: a->delay = (uint8_t)param; return;
        case 1:
            if (ch < 11)
                program_instr(a, ch, param, a->instr + param * 16);
            break;
        case 2: if (ch < 11) note_on(a, ch, param); break;
        case 3: if (ch < 11) key_off(a, ch); break;
        case 4: if (ch < 11) set_volume(a, ch, param); break;
        case 5: a->ev = a->loop; break;
        case 6: a->loop = a->ev; break;
        case 7: break;                       /* sync flag, unused */
        }
    }
}

/* ---- public ------------------------------------------------------------- */

sr_audio *sr_audio_create(void)
{
    sr_audio *a = calloc(1, sizeof *a);
    OPL3_Reset(&a->chip, SR_AUDIO_RATE);
    a->enabled = true;
    a->cur_song = -1;
    opl_init(a);
    return a;
}

void sr_audio_destroy(sr_audio *a)
{
    free(a->sfx_copy);
    free(a);
}

void sr_audio_set_enabled(sr_audio *a, bool on)
{
    a->enabled = on;
    if (!on) {
        opl_reset(a);
        a->playing = false;
        a->cur_song = -1;
        a->sfx_data = NULL;
    }
}

bool sr_audio_music(sr_audio *a, const sr_assets *assets, int n)
{
    if (a->cur_song == n)
        return true;
    opl_reset(a);
    a->playing = false;
    if (!a->enabled)
        return false;

    size_t size;
    uint8_t *data = assets->io.read_file("muzax.lzs", &size);
    if (!data)
        return false;
    if ((size_t)(n * 6 + 6) > size) {
        free(data);
        return false;
    }
    uint16_t off   = (uint16_t)(data[n*6]   | (data[n*6+1] << 8));
    uint16_t ninst = (uint16_t)(data[n*6+2] | (data[n*6+3] << 8));
    uint16_t raw   = (uint16_t)(data[n*6+4] | (data[n*6+5] << 8));
    if (off == 0 || raw == 0 || raw > sizeof a->song) {
        free(data);
        return false;
    }
    lzs_stream s;
    lzs_init(&s, data, size, off);
    lzs_decompress(&s, a->song, raw);
    free(data);

    a->instr = a->song;
    a->ev = a->loop = a->song + ninst * 16;
    a->song_end = a->song + raw;
    a->delay = 0;
    a->playing = true;
    a->cur_song = n;
    return true;
}

void sr_audio_music_stop(sr_audio *a)
{
    opl_reset(a);
    a->playing = false;
    a->cur_song = -1;
}

void sr_audio_pcm(sr_audio *a, const uint8_t *pcm, size_t len, int tc)
{
    if (!a->enabled || !pcm || !len)
        return;
    free(a->sfx_copy);
    a->sfx_copy = malloc(len);
    memcpy(a->sfx_copy, pcm, len);
    a->sfx_data = a->sfx_copy;
    a->sfx_len = len;
    a->sfx_pos_fp = 0;
    uint32_t rate = 1000000u / (256u - (uint32_t)(tc & 0xff));
    a->sfx_step_fp = (uint32_t)(((uint64_t)rate << 16) / SR_AUDIO_RATE);
}

void sr_audio_sfx(sr_audio *a, const sr_assets *assets, int n)
{
    if (!a->enabled)
        return;
    size_t size;
    uint8_t *d = assets->io.read_file("sfx.snd", &size);
    if (!d)
        return;
    int count = (d[0] | (d[1] << 8)) / 2 - 1;
    if (n < 0 || n >= count) {
        free(d);
        return;
    }
    uint16_t off  = (uint16_t)(d[n*2]     | (d[n*2+1] << 8));
    uint16_t next = (uint16_t)(d[n*2+2]   | (d[n*2+3] << 8));
    if (next > size || off >= next) {
        free(d);
        return;
    }
    sr_audio_pcm(a, d + off + 1, (size_t)(next - off - 1), d[off]);
    free(d);
}

void sr_audio_render(sr_audio *a, int16_t *stereo, int frames)
{
    const int per_tick = SR_AUDIO_RATE * 6628 / 1193182;  /* 180.02 Hz */
    for (int i = 0; i < frames; i++) {
        if (a->tick_acc <= 0) {
            music_tick(a);
            a->tick_acc += per_tick;
        }
        a->tick_acc--;
        int16_t buf[2];
        OPL3_GenerateResampled(&a->chip, buf);
        int32_t l = buf[0] * 2, r = buf[1] * 2;
        if (a->sfx_data) {
            uint32_t pos = a->sfx_pos_fp >> 16;
            if (pos >= a->sfx_len) {
                a->sfx_data = NULL;
            } else {
                int32_t s = ((int32_t)a->sfx_data[pos] - 128) << 7;
                l += s;
                r += s;
                a->sfx_pos_fp += a->sfx_step_fp;
            }
        }
        if (l > 32767) l = 32767; if (l < -32768) l = -32768;
        if (r > 32767) r = 32767; if (r < -32768) r = -32768;
        stereo[i*2] = (int16_t)l;
        stereo[i*2+1] = (int16_t)r;
    }
}
