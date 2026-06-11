/* Audio core: Muzax OPL2 music player (EXE 0x5a39/0x58fd/0x5955/0x59f1)
 * through the Nuked OPL3 emulator + Sound Blaster digitized SFX playback.
 * Pull model: the platform calls sr_audio_render() from its audio thread. */
#ifndef SR_AUDIO_H
#define SR_AUDIO_H

#include "sr.h"
#include "assets.h"

#define SR_AUDIO_RATE 44100

typedef struct sr_audio sr_audio;

sr_audio *sr_audio_create(void);
void sr_audio_destroy(sr_audio *a);

/* Start song n from muzax.lzs (0=intro, 1=menu, 2..13 gameplay).
 * No-op if already playing n (mirrors [0xbc2] check). */
bool sr_audio_music(sr_audio *a, const sr_assets *assets, int n);
void sr_audio_music_stop(sr_audio *a);

/* Play sfx n (0..4) from sfx.snd; replaces any current sample (SB
 * single-voice semantics). raw=NULL uses the loaded sfx.snd. */
void sr_audio_sfx(sr_audio *a, const sr_assets *assets, int n);
/* Play a whole raw PCM buffer (intro.snd), time constant tc. */
void sr_audio_pcm(sr_audio *a, const uint8_t *pcm, size_t len, int tc);

void sr_audio_set_enabled(sr_audio *a, bool on);

/* Render interleaved stereo s16. Thread-safe vs the calls above through an
 * internal lock implemented by the platform shim (single-threaded use is
 * fine without). */
void sr_audio_render(sr_audio *a, int16_t *stereo, int frames);

#endif
