#ifndef AUDIO_H
#define AUDIO_H

#include "common.h"

typedef struct audio_player audio_player;

audio_player *audio_init(void);
void load_audio_source(audio_player *audio, u8 *src, u32 src_len_bytes);
void emulate_from_audio(audio_player *audio);
void debug_play_sound(audio_player *audio, u8 *source_data);

#endif
