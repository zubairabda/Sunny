#ifndef AUDIO_H
#define AUDIO_H

#include "common.h"

typedef struct audio_player audio_player;

audio_player *audio_init(void);
void audio_shutdown(audio_player *audio);
void emulate_from_audio(audio_player *audio);
void play_sound(audio_player *audio, s16 *data, u32 num_frames);

#endif /* AUDIO_H */
