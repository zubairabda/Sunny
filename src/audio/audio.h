#ifndef AUDIO_H
#define AUDIO_H

#include "common.h"

typedef struct audio_player audio_player;

#pragma pack(push, 1)
struct wav_header
{
    u8 riff[4];
    u32 chunk_size;
    u8 wave[4];
};

struct wav_format_chunk
{
    u8 fmt[4];
    u32 chunk_size;
    u16 format_tag;
    u16 num_channels;
    u32 samples_per_second;
    u32 bytes_per_second;
    u16 block_align;
    u16 bits_per_sample;
};

struct wav_data_chunk
{
    u8 data[4];
    u32 chunk_size;
};
#pragma pack(pop)

audio_player *audio_init(void);
void load_audio_source(audio_player *audio, u8 *src, u32 src_len_bytes);
void emulate_from_audio(audio_player *audio);
void debug_play_sound(audio_player *audio, u8 *source_data);

#endif
