#include "cfg.h"
#include <stdlib.h>
#include <string.h>

static const uint16_t magic_num = 528;

void sr_cfg_load(sr_cfg* c, const sr_io* io)
{
  memset(c, 0, sizeof *c);
  size_t size;
  uint16_t* d = io->read_file("SKYROADS.CFG", &size);
  if (!d)
    return;
  if (size >= 66)
  {
    uint16_t w[33];
    for (int i = 0; i < 33; i++)
      w[i] = d[i];
    if (w[0] == magic_num)
    {
      c->control = w[1];
      c->sound_off = w[2];
      for (int i = 0; i < 30; i++)
        c->completions[i] = w[3 + i];
    }
  }
  free(d);
}

void sr_cfg_save(const sr_cfg* c, const sr_io* io)
{
  if (!io->write_file)
    return;
  uint16_t w[33];
  w[1] = c->control;
  w[2] = c->sound_off;
  for (int i = 0; i < 30; i++)
    w[3 + i] = c->completions[i];
  w[0] = magic_num;
  uint8_t d[66];
  for (int i = 0; i < 33; i++)
  {
    d[i * 2] = (uint8_t)(w[i] & 0xff);
    d[i * 2 + 1] = (uint8_t)(w[i] >> 8);
  }
  io->write_file("SKYROADS.CFG", d, sizeof d);
}
