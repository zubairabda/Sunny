#ifndef SPU_H
#define SPU_H

#include "common.h"

struct voice_regs
{
    s16 volume_left;    // 0x0
    s16 volume_right;   // 0x2
    u16 sample_rate;    // 0x4
    u16 start_addr;     // 0x6
    u32 adsr;           // 0x8
    s16 adsr_volume;    // 0xC
    u16 repeat_addr;    // 0xE
};

enum adsr_stage
{
    ADSR_OFF = 0,
    ADSR_ATTACK,
    ADSR_DECAY,
    ADSR_SUSTAIN,
    ADSR_RELEASE
};

struct voice_state
{
    u32 current_addr;
    enum adsr_stage stage;
    u32 pitch_counter;
    s32 adsr_cycles;
    u16 adsr_target;
    //s16 older;
    //s16 old;
    s16 amplitude;
    b8 has_samples;
    u8 block_flags;
    
    s16 decoded_samples[31]; // 3 saved samples from previous block + 28 samples per block
};

typedef union
{
    struct
    {
        u16 cd_audio_enable : 1;
        u16 external_audio_enable : 1;
        u16 cd_audio_reverb : 1;
        u16 external_audio_reverb : 1;
        u16 transfer_mode : 2;
        u16 irq_enable : 1;
        u16 reverb_master_enable : 1;
        u16 noise_frequency_step : 2;
        u16 noise_frequency_shift : 4;
        u16 mute_spu : 1;
        u16 spu_enable : 1;
    };
    u16 value;
} SPUCNT;

struct spu_control
{
    u16 main_volume_left; // D80
    u16 main_volume_right;
    u16 reverb_volume_left; // D84
    u16 reverb_volume_right;
    // starts ADSR envelope
    u32 keyon;  // D88
    u32 keyoff;
    u32 pmon; // D90
    u32 noise_mode_enable;
    u32 reverb_mode_enable;
    u32 endx;
    u16 unk0; // DA0
    u16 reverb_work_start_addr;
    u16 irq_addr;
    u16 data_transfer_addr;
    u16 tx_fifo; // ?
    u16 spucnt; // DAA
    u16 transfer_control;
    u16 spustat; // DAE
    u16 cd_volume_left; // DB0
    u16 cd_volume_right;
    u16 extern_volume_left;
    u16 extern_volume_right;
    u16 current_main_volume_left;
    u16 current_main_volume_right;
};

union reverb_regs
{
    struct
    {
        u16 dAPF1;
        u16 dAPF2;
        s16 vIIR;
        s16 vCOMB1;
        s16 vCOMB2;
        s16 vCOMB3;
        s16 vCOMB4;
        s16 vWALL;
        s16 vAPF1;
        s16 vAPF2;
        u16 mLSAME;
        u16 mRSAME;
        u16 mLCOMB1;
        u16 mRCOMB1;
        u16 mLCOMB2;
        u16 mRCOMB2;
        u16 dLSAME;
        u16 dRSAME;
        u16 mLDIFF;
        u16 mRDIFF;
        u16 mLCOMB3;
        u16 mRCOMB3;
        u16 mLCOMB4;
        u16 mRCOMB4;
        u16 dLDIFF;
        u16 dRDIFF;
        u16 mLAPF1;
        u16 mRAPF1;
        u16 mLAPF2;
        u16 mRAPF2;
        s16 vLIN;
        s16 vRIN;
    };
    u16 regs[32];
};

struct spu_state
{
    u32 sector_sample_index;
    union reverb_regs reverb;

    struct spu_voice
    {
        union
        {
            struct voice_regs data[24];
            u16 regs[(sizeof(struct voice_regs) * 24) >> 1];
        };
        struct voice_state states[24];
    } voice;

    struct spu_control cnt;
    u8 transfer_fifo_len;
    u16 transfer_fifo[32];
    u32 current_transfer_addr;
    //u32 frames_buffered;
    //s16 *audio_buffer;
    u8 *dram;
};

extern struct spu_state g_spu;

void spu_reset(void);

u16 spu_read(u32 offset);
void spu_write(u32 offset, u32 value);
void spu_tick(u32 param);

#endif /* SPU_H */
