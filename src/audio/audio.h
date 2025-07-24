#ifndef AUDIO_H
#define AUDIO_H

#include "common.h"

b8 audio_init(void);
void audio_shutdown(void);

b32 emulate_from_audio(void);
void audio_buffer_write(s16 left, s16 right);
void play_sound(s16 *data, u32 num_frames);

void audio_set_volume(f32 volume);
f32 audio_get_volume(void);

void audio_set_mute(b8 muted);
b8 audio_is_muted(void);

#endif /* AUDIO_H */
