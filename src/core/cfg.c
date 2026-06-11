#include "cfg.h"
#include <stdlib.h>
#include <string.h>

static uint16_t checksum(const uint16_t *words)   /* words[1..32] */
{
    uint16_t sum = 0;
    for (int i = 1; i <= 32; i++)
        sum = (uint16_t)(sum + (words[i] ^ i));
    return sum;
}

void sr_cfg_load(sr_cfg *c, const sr_io *io)
{
    memset(c, 0, sizeof *c);
    size_t size;
    uint8_t *d = io->read_file("skyroads.cfg", &size);
    if (!d)
        return;
    if (size >= 66) {
        uint16_t w[33];
        for (int i = 0; i < 33; i++)
            w[i] = (uint16_t)(d[i*2] | (d[i*2+1] << 8));
        if (checksum(w) == w[0]) {
            c->control = w[1];
            c->sound_off = w[2];
            for (int i = 0; i < 30; i++)
                c->completions[i] = w[3 + i];
        }
    }
    free(d);
}

void sr_cfg_save(const sr_cfg *c, const sr_io *io)
{
    if (!io->write_file)
        return;
    uint16_t w[33];
    w[1] = c->control;
    w[2] = c->sound_off;
    for (int i = 0; i < 30; i++)
        w[3 + i] = c->completions[i];
    w[0] = checksum(w);
    uint8_t d[66];
    for (int i = 0; i < 33; i++) {
        d[i*2] = (uint8_t)(w[i] & 0xff);
        d[i*2+1] = (uint8_t)(w[i] >> 8);
    }
    io->write_file("skyroads.cfg", d, sizeof d);
}
