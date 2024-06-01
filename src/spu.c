#include "spu.h"
#include "debug.h"

#if SY_DEBUG
static char *debug_spu_str_table[] = {
                                        "Main volume left", "Main volume right", "Reverb volume left", "Reverb volume right", "Voice Key On", "Voice Key On", 
                                        "Voice Key Off", "Voice Key Off", "Pitch Modulation", "Pitch Modulation", "Noise Mode Enable", "Noise Mode Enable", "Echo On", 
                                        "Echo On", "Voice key status", "Voice key status", "garbage", "Reverb work start address", "IRQ address", "Data transfer address", 
                                        "Data transfer FIFO", "SPUCNT", "Transfer control", "SPUSTAT", "CD volume left", "CD volume right", "External input volume left",
                                        "External input volume right"
                                     };

static char *debug_spu_reg_table[] = {"Volume left", "Volume right", "Sample rate", "Start address", "ADSR lo", "ADSR hi", "ADSR volume", "Repeat address"};
#endif

u16 spu_read(struct spu_state *spu, u32 offset)
{
    u16 result = 0;
    if (offset < 0x180)
    {
        u8 voice = (offset >> 4) & 0x1f;
        //debug_log("SPU voice %u\t%-15.15s -> %u\n", voice, debug_spu_reg_table[(offset & 0xf) >> 1], result);
        result = spu->voice.regs[(offset & 0x1ff) >> 1];
    }
    else if (offset < 0x1c0)
    {
        switch (offset & 0x3f)
        {
        case 0x0:
            result = spu->cnt.main_volume_left;
            break;
        case 0x2:
            result = spu->cnt.main_volume_right;
            break;
        case 0x4:
            result = spu->cnt.reverb_volume_left;
            break;
        case 0x6:
            result = spu->cnt.reverb_volume_right;
            break;
        case 0x8:
        case 0xA:
            // BIOS reads this?
            result = 0;
            break;
        case 0xC:
        case 0xE:
            result = 0;
            break;
        case 0x10:
        case 0x12:
            // PMON
            break;
        case 0x14:
        case 0x16:
            // NON
            break;
        case 0x18:
        case 0x1A:
            // EON
            break;
        case 0x1C:
        case 0x1E:
            // ENDX
            result = 0;//spu->cnt.endx << ((offset & 0x2) << 3);
            break;
        case 0x20:
            // garbage?
            break;
        case 0x22:
            result = spu->cnt.reverb_work_start_addr;
            break;
        case 0x24:
            result = spu->cnt.irq_addr;
            break;
        case 0x26:
            result = spu->cnt.data_transfer_addr;
            break;
        case 0x28:
            result = spu->transfer_fifo[0];
            debug_log("[WARN]: Unexpected read from SPU transfer fifo\n");
            break;
        case 0x2A:
            result = spu->cnt.spucnt;
            break;
        case 0x2C:
            result = spu->cnt.transfer_control;
            break;
        case 0x2E:
            result = spu->cnt.spustat;
            break;
        case 0x30:
            result = spu->cnt.cd_volume_left;
            break;
        case 0x32:
            result = spu->cnt.cd_volume_right;
            break;
        case 0x34:
            // external audio input vol left
            break;
        case 0x36:
            // external audio input vol right
            break;
        SY_INVALID_CASE;
        }
        //debug_log("SPU CTRL\t%-20.20s -> %u\n", debug_spu_str_table[(offset & 0x3f) >> 1], result);
    }
    else
    {
        result = spu->reverb.regs[(offset & 0x3f) >> 1];
    }
    return result;
}

void spu_write(struct spu_state *spu, u32 offset, u32 value)
{
    if (offset < 0x180)
    {
        u8 voice = (offset >> 4) & 0x1f;
        //debug_log("SPU voice %u\t%-15.15s <- %u\n", voice, debug_spu_reg_table[(offset & 0xf) >> 1], value);
        spu->voice.regs[(offset & 0x1ff) >> 1] = value;
    }
    else if (offset < 0x1c0)
    {
        switch (offset & 0x3f) // from D80
        {
        case 0x0:
            spu->cnt.main_volume_left = value;
            break;
        case 0x2:
            spu->cnt.main_volume_right = value;
            break;
        case 0x4:
            spu->cnt.reverb_volume_left = value;
            break;
        case 0x6:
            spu->cnt.reverb_volume_right = value;
            break;
        case 0x8:
        case 0xa:
            // KEY ON
            // if this is a 16 bit write, determine which halfword were writing to
            value <<= ((offset & 0x2) << 3);
            
            for (u32 i = 0; i < 24; ++i)
            {
                u32 pos = 1 << i;
                if (value & pos)
                {
                    spu->voice.internal[i].state = ADSR_ATTACK;
                    spu->voice.data[i].adsr_volume = 0;
                    // TODO: according to docs, repeat gets set to start addr on key on.. but not sure this is needed?
                    //spu->voice.data[i].repeat_addr = spu->voice.data[i].start_addr;
                    spu->voice.internal[i].current_addr = spu->voice.data[i].start_addr;
                    spu->voice.internal[i].pitch_counter = 0;
                    spu->voice.internal[i].has_samples = 0;
                    spu->voice.internal[i].adsr_cycles = 0;
                    spu->cnt.endx &= ~pos;
                }
            }
            break;
        case 0xc:
        case 0xe:
            // KEY OFF
            value <<= ((offset & 0x2) << 3);
            
            for (u32 i = 0; i < 24; ++i)
            {
                u32 pos = 1 << i;
                if (value & pos)
                {
                    //debug_log("voice %u -> KEY OFF\n", i);
                    spu->voice.internal[i].adsr_cycles = 0;
                    spu->voice.internal[i].state = ADSR_RELEASE;
                }
            }
            break;
        case 0x10:
        case 0x12:
            // PMON
            value <<= ((offset & 0x2) << 3);
            spu->cnt.pmon = 0;
            for (u32 i = 0; i < 24; ++i)
            {
                if (value & (1 << i))
                {
                    spu->cnt.pmon |= (1 << i);
                }
            }
            break;
        case 0x14:
        case 0x16:
            // NON
            break;
        case 0x18:
        case 0x1a:
            // EON
            break;
        case 0x1c:
        case 0x1e:
            // ENDX
            break;
        case 0x20:
            // garbage?
            break;
        case 0x22:
            spu->cnt.reverb_work_start_addr = value;
            break;
        case 0x24:
            spu->cnt.irq_addr = value;
            break;
        case 0x26:
            spu->cnt.data_transfer_addr = value;
            spu->current_transfer_addr = value << 3;
            break;
        case 0x28:
            // TODO: were not implementing SPUSTAT.10, wonder how far that'll get us
            SY_ASSERT(spu->transfer_fifo_len < ARRAYCOUNT(spu->transfer_fifo));
            spu->transfer_fifo[spu->transfer_fifo_len++] = value; // NOTE: read behavior?
            break;
        case 0x2a:
            spu->cnt.spucnt = value;
    #if 0
            spu->cnt.spustat &= ~(0x5f);
            spu->cnt.spustat |= (spu->cnt.spucnt & 0x1f);
            spu->cnt.spustat |= (spu->cnt.spucnt << 2) & 0x40;
    #endif
            break;
        case 0x2c:
            spu->cnt.transfer_control = value;
            SY_ASSERT(((spu->cnt.transfer_control >> 1) & 0x7) == 2);
            break;
        case 0x30:
            spu->cnt.cd_volume_left = value;
            break;
        case 0x32:
            spu->cnt.cd_volume_right = value;
            break;
        case 0x34:
            // extern vol left
            break;
        case 0x36:
            // extern vol right
            break;
        case 0x38:
            // current vol left
            break;
        case 0x3a:
            // current vol right
            break;
        case 0x3c:
            // unknown
            break;
        case 0x3e:
            // unknown
            break;
        SY_INVALID_CASE;
        }
        //debug_log("SPU CTRL\t%-20.20s <- %u\n", debug_spu_str_table[(offset & 0x3f) >> 1], value);
    }
    else
    {
        spu->reverb.regs[(offset & 0x3f) >> 1] = value;
    }
    //debug_log("SPU write to: %08x <- %u\n", offset + 0x1f801c00, value);
}

void spu_tick(void *data, u32 param, s32 cycles_late)
{
    struct spu_state *spu = (struct spu_state *)data;
    // not sure if we need to emulate this, but spustat applies its changes delayed, im assuming to the next tick
    spu->cnt.spustat &= ~(0x5f);
    spu->cnt.spustat |= (spu->cnt.spucnt & 0x1f);
    spu->cnt.spustat |= (spu->cnt.spucnt << 2) & 0x40;
    // pending transfers also happen on the next tick
    if (spu->transfer_fifo_len)
    {
        memcpy((spu->dram + spu->current_transfer_addr), spu->transfer_fifo, spu->transfer_fifo_len * 2);
        spu->current_transfer_addr += spu->transfer_fifo_len * 2;
        spu->transfer_fifo_len = 0;
    }
    // each buffered sample has 2 channels
    u32 buffer_index = spu->num_buffered_frames * 2;
    s32 final_vol_left = 0;
    s32 final_vol_right = 0;

    for (u32 i = 0; i < 24; ++i)
    {
        struct voice_internal *internal = &spu->voice.internal[i];
        struct voice_regs *regs = &spu->voice.data[i];
#if 0
        if (internal->state == ADSR_RELEASE)
            continue;
#endif
        if (!internal->has_samples)
        {
            // save last samples from last block
            internal->decoded_samples[0] = internal->decoded_samples[28];
            internal->decoded_samples[1] = internal->decoded_samples[29];
            internal->decoded_samples[2] = internal->decoded_samples[30];

            u8 *data = spu->dram + (internal->current_addr << 3);

            u8 *adpcm_block = data;
            u8 shift = adpcm_block[0] & 0xf;
            if (shift > 12) {
                shift = 9;
            }
            u8 amt = 12 - shift;
            u8 filter = (adpcm_block[0] >> 4) & 0x7;
            u8 flags = adpcm_block[1];

            // loop start flag
            if (flags & 0x4) {
                regs->repeat_addr = (u16)internal->current_addr;
            }
            // store flags for when we process them later
            internal->block_flags = flags;

            s32 pos_adpcm_table[] = {0, 60, 115, 98, 122};
            s32 neg_adpcm_table[] = {0, 0, -52, -55, -60};

            s32 f0 = pos_adpcm_table[filter];
            s32 f1 = neg_adpcm_table[filter];

            for (int j = 0; j < 14; ++j)
            {
                //s32 t0 = (s32)((u32)(*(adpcm_block + 2 + i) & 0xf) << 28) >> 28;
                // first sample
                s8 a0 = *(adpcm_block + 2 + j) & 0xf;
                a0 <<= 4;
                a0 >>= 4;

                u32 t0 = a0;
                s32 s0 = (u32)(t0 << amt) + ((internal->old * f0 + internal->older * f1 + 32) / 64);
                s16 sample0 = (s16)clamp16(s0);

                internal->older = internal->old;
                internal->old = sample0;

                // second sample
                s8 a1 = (*(adpcm_block + 2 + j) >> 4) & 0xf;
                a1 <<= 4;
                a1 >>= 4;

                u32 t1 = a1;
                s32 s1 = (t1 << amt) + ((internal->old * f0 + internal->older * f1 + 32) / 64);
                s16 sample1 = (s16)clamp16(s1);

                internal->older = internal->old;
                internal->old = sample1;

                internal->decoded_samples[3 + j * 2] = sample0;
                internal->decoded_samples[3 + j * 2 + 1] = sample1;
            }
            internal->has_samples = 1;
            #if 0
            debug_log("Decoded samples for voice: %u, addr: %u\n", i, spu->voice.internal[i].current_addr);

            for (u32 j = 0; j < ARRAYCOUNT(internal->decoded_samples); ++j)
            {
                debug_log("\t%hi\n", internal->decoded_samples[j]);
            }
            #endif
        }
        // gauss table index
        u32 g = ((internal->pitch_counter >> 4) & 0xff);
        // adpcm sample index
        u32 s = ((internal->pitch_counter >> 12) & 0x1f) + 3;

        s32 interp = ((gauss_table[0x0ff - g] * internal->decoded_samples[s - 3]) >> 15);
        interp = interp + ((gauss_table[0x1ff - g] * internal->decoded_samples[s - 2]) >> 15);
        interp = interp + ((gauss_table[0x100 + g] * internal->decoded_samples[s - 1]) >> 15);
        interp = interp + ((gauss_table[0x000 + g] * internal->decoded_samples[s]) >> 15);

        if (interp > 0x7fff || interp < -0x8000)
        {
            SY_ASSERT(0);
        }

        s32 volume = (interp * regs->adsr_volume) >> 15;
        //volume = interp;

        //s16 vol_left = voice_get_volume

        s32 volL = 0;
        s32 volR = 0;

        if (regs->volume_left & 0x8000)
        {
            // sweep mode
            SY_ASSERT(0);
        }
        else
        {
            // fixed volume mode
            volL = clamp((s32)(regs->volume_left & 0x7fff), -0x4000, 0x3fff) * 2;
        }

        if (regs->volume_right & 0x8000)
        {
            // sweep mode
            SY_ASSERT(0);
        }
        else
        {
            // fixed volume mode
            volR = clamp((s32)(regs->volume_left & 0x7fff), -0x4000, 0x3fff) * 2;
        }

        final_vol_left += (volume * volL) >> 15;
        final_vol_right += (volume * volR) >> 15;

        s32 level;
        u8 mode;
        s8 shift;
        s8 stepval;
        u8 dir;

        switch (internal->state)
        {
        case ADSR_ATTACK:
        {
            dir = 0;
            mode = (regs->adsr >> 15) & 0x1;
            shift = (regs->adsr >> 10) & 0x1f;
            stepval = 7 - ((regs->adsr >> 8) & 0x3);
            internal->adsr_target = 0x7fff;
        } break;
        case ADSR_DECAY:
        {
            level = ((regs->adsr & 0xf) + 1) * 0x800;
            internal->adsr_target = level;
            mode = 1;
            dir = 1;
            stepval = -8;
            shift = (regs->adsr >> 4) & 0xf;
        } break;
        case ADSR_SUSTAIN:
        {
            shift = (regs->adsr >> 24) & 0x1f;
            dir = (regs->adsr >> 30) & 0x1;
            stepval = dir ? -8 + ((regs->adsr >> 22) & 0x3) : 7 - ((regs->adsr >> 22) & 0x3);
            mode = (regs->adsr >> 31);
        } break;
        case ADSR_RELEASE:
        {
            dir = 1;
            stepval = -8;
            shift = (regs->adsr >> 16) & 0x1f;
            mode = (regs->adsr >> 21) & 0x1;
            internal->adsr_target = 0;
        } break;
        SY_INVALID_CASE;
        }

        // TODO: this could be off by one, not sure what exactly the behavior is (eg. adsr cycles gets set to 1, should next clock step? I think not)
        if (internal->adsr_cycles > 0)
        {
            --internal->adsr_cycles;
        }
        else
        {
            s32 adsr_cycles = 1 << MAX(0, shift - 11);
            s32 adsr_step = (s32)stepval << MAX(0, 11 - shift);
            if (mode && !dir && (regs->adsr_volume > 0x6000)) {
                adsr_cycles *= 4;
            }
            if (mode && dir) {
                adsr_step = (adsr_step * regs->adsr_volume) >> 15;
            }
            internal->adsr_cycles = adsr_cycles;
            regs->adsr_volume = (s16)clamp(regs->adsr_volume + adsr_step, 0, 0x7fff);
        }
        // NOTE: adsr is adjusted in steps, must account for when a step exceeds the target
        b8 target_reached = dir ? regs->adsr_volume <= internal->adsr_target : regs->adsr_volume >= internal->adsr_target;

        if (internal->state != ADSR_SUSTAIN && target_reached)
        {
            switch (internal->state)
            {
            case ADSR_ATTACK:
                internal->state = ADSR_DECAY;
                break;
            case ADSR_DECAY:
                internal->state = ADSR_SUSTAIN;
                break;
            default:
                break;
            #if 0
            case ADSR_RELEASE:
                internal->state = ADSR_OFF;
                break;
            SY_INVALID_CASE;
            #endif
            }
            internal->adsr_cycles = 0;
        }

        // update pitch counter
        s32 step = regs->sample_rate;
        if (spu->cnt.pmon & (1 << i) && (i > 0))
        {
            s16 factor = internal->prev_amplitude;
        }
        if (step > 0x3fff) {
            step = 0x4000;
        }
        internal->pitch_counter += step;

        u32 sample_index = (internal->pitch_counter >> 12) & 0x1f;
        if (sample_index >= 28)
        {
            // pitch counter determines the 'sampling frequency', so the next block has to also follow the same frequency (which is why we subtract)
            sample_index -= 28;
            internal->pitch_counter &= ~(0x1f << 12);
            internal->pitch_counter |= (sample_index << 12);
            // NOTE: if were looping on a single block, setting this to 0 here means we re-decode the same block over and over
            internal->has_samples = 0;

            // loop end flag sets endx and jumps
            if (internal->block_flags & 0x1)
            {
                internal->current_addr = regs->repeat_addr;
                spu->cnt.endx |= (1 << i);
                if (!(internal->block_flags & 0x2)) {
                    internal->state = ADSR_RELEASE;
                    regs->adsr_volume = 0;
                    internal->adsr_cycles = 0;
                }
            }
            else {
                internal->current_addr += 2;
            }
        }
    }
#if 1
    //if (buffer_index > spu->audio_buffer_len)
    spu->audio_buffer[buffer_index] = clamp16(final_vol_left);
    spu->audio_buffer[buffer_index + 1] = clamp16(final_vol_right);

    ++spu->num_buffered_frames;

    schedule_event(spu_tick, data, 0, 768 - cycles_late, EVENT_ID_DEFAULT);
#endif
#if 0
    if (((debug_sound_buffer_index + 2) * 2) < MEGABYTES(16))
    {
        debug_sound_buffer[debug_sound_buffer_index++] = clamp16(final_vol_left);
        debug_sound_buffer[debug_sound_buffer_index++] = clamp16(final_vol_right);
    }
    else
    {
        static b8 b = 0;
        if (!b)
        {
            b = 1;
            write_wav_file(debug_sound_buffer, debug_sound_buffer_index * 2, "output.wav");
        }
    }
#endif
#if 0
    if (spu->num_buffered_samples >= 441)
    {

        play_sound(g_audio, spu->buffered_samples, spu->num_buffered_samples);
        spu->num_buffered_samples = 0;
    }
#endif
}
