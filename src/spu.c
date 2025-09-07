#include "spu.h"
#include "audio/audio.h"
#include "cdrom.h"
#include "event.h"
#include "debug.h"

static s16 gauss_table[512] = 
{
    -0x001, -0x001, -0x001, -0x001, -0x001, -0x001, -0x001, -0x001,
    -0x001, -0x001, -0x001, -0x001, -0x001, -0x001, -0x001, -0x001,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0001,
    0x0001, 0x0001, 0x0001, 0x0002, 0x0002, 0x0002, 0x0003, 0x0003,
    0x0003, 0x0004, 0x0004, 0x0005, 0x0005, 0x0006, 0x0007, 0x0007,
    0x0008, 0x0009, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E,
    0x000F, 0x0010, 0x0011, 0x0012, 0x0013, 0x0015, 0x0016, 0x0018,
    0x0019, 0x001B, 0x001C, 0x001E, 0x0020, 0x0021, 0x0023, 0x0025,
    0x0027, 0x0029, 0x002C, 0x002E, 0x0030, 0x0033, 0x0035, 0x0038,
    0x003A, 0x003D, 0x0040, 0x0043, 0x0046, 0x0049, 0x004D, 0x0050,
    0x0054, 0x0057, 0x005B, 0x005F, 0x0063, 0x0067, 0x006B, 0x006F,
    0x0074, 0x0078, 0x007D, 0x0082, 0x0087, 0x008C, 0x0091, 0x0096,
    0x009C, 0x00A1, 0x00A7, 0x00AD, 0x00B3, 0x00BA, 0x00C0, 0x00C7,
    0x00CD, 0x00D4, 0x00DB, 0x00E3, 0x00EA, 0x00F2, 0x00FA, 0x0101,
    0x010A, 0x0112, 0x011B, 0x0123, 0x012C, 0x0135, 0x013F, 0x0148,
    0x0152, 0x015C, 0x0166, 0x0171, 0x017B, 0x0186, 0x0191, 0x019C,
    0x01A8, 0x01B4, 0x01C0, 0x01CC, 0x01D9, 0x01E5, 0x01F2, 0x0200,
    0x020D, 0x021B, 0x0229, 0x0237, 0x0246, 0x0255, 0x0264, 0x0273,
    0x0283, 0x0293, 0x02A3, 0x02B4, 0x02C4, 0x02D6, 0x02E7, 0x02F9,
    0x030B, 0x031D, 0x0330, 0x0343, 0x0356, 0x036A, 0x037E, 0x0392,
    0x03A7, 0x03BC, 0x03D1, 0x03E7, 0x03FC, 0x0413, 0x042A, 0x0441,
    0x0458, 0x0470, 0x0488, 0x04A0, 0x04B9, 0x04D2, 0x04EC, 0x0506,
    0x0520, 0x053B, 0x0556, 0x0572, 0x058E, 0x05AA, 0x05C7, 0x05E4,
    0x0601, 0x061F, 0x063E, 0x065C, 0x067C, 0x069B, 0x06BB, 0x06DC,
    0x06FD, 0x071E, 0x0740, 0x0762, 0x0784, 0x07A7, 0x07CB, 0x07EF,
    0x0813, 0x0838, 0x085D, 0x0883, 0x08A9, 0x08D0, 0x08F7, 0x091E,
    0x0946, 0x096F, 0x0998, 0x09C1, 0x09EB, 0x0A16, 0x0A40, 0x0A6C,
    0x0A98, 0x0AC4, 0x0AF1, 0x0B1E, 0x0B4C, 0x0B7A, 0x0BA9, 0x0BD8,
    0x0C07, 0x0C38, 0x0C68, 0x0C99, 0x0CCB, 0x0CFD, 0x0D30, 0x0D63,
    0x0D97, 0x0DCB, 0x0E00, 0x0E35, 0x0E6B, 0x0EA1, 0x0ED7, 0x0F0F,
    0x0F46, 0x0F7F, 0x0FB7, 0x0FF1, 0x102A, 0x1065, 0x109F, 0x10DB,
    0x1116, 0x1153, 0x118F, 0x11CD, 0x120B, 0x1249, 0x1288, 0x12C7,
    0x1307, 0x1347, 0x1388, 0x13C9, 0x140B, 0x144D, 0x1490, 0x14D4,
    0x1517, 0x155C, 0x15A0, 0x15E6, 0x162C, 0x1672, 0x16B9, 0x1700,
    0x1747, 0x1790, 0x17D8, 0x1821, 0x186B, 0x18B5, 0x1900, 0x194B,
    0x1996, 0x19E2, 0x1A2E, 0x1A7B, 0x1AC8, 0x1B16, 0x1B64, 0x1BB3,
    0x1C02, 0x1C51, 0x1CA1, 0x1CF1, 0x1D42, 0x1D93, 0x1DE5, 0x1E37,
    0x1E89, 0x1EDC, 0x1F2F, 0x1F82, 0x1FD6, 0x202A, 0x207F, 0x20D4,
    0x2129, 0x217F, 0x21D5, 0x222C, 0x2282, 0x22DA, 0x2331, 0x2389,
    0x23E1, 0x2439, 0x2492, 0x24EB, 0x2545, 0x259E, 0x25F8, 0x2653,
    0x26AD, 0x2708, 0x2763, 0x27BE, 0x281A, 0x2876, 0x28D2, 0x292E,
    0x298B, 0x29E7, 0x2A44, 0x2AA1, 0x2AFF, 0x2B5C, 0x2BBA, 0x2C18,
    0x2C76, 0x2CD4, 0x2D33, 0x2D91, 0x2DF0, 0x2E4F, 0x2EAE, 0x2F0D,
    0x2F6C, 0x2FCC, 0x302B, 0x308B, 0x30EA, 0x314A, 0x31AA, 0x3209,
    0x3269, 0x32C9, 0x3329, 0x3389, 0x33E9, 0x3449, 0x34A9, 0x3509,
    0x3569, 0x35C9, 0x3629, 0x3689, 0x36E8, 0x3748, 0x37A8, 0x3807,
    0x3867, 0x38C6, 0x3926, 0x3985, 0x39E4, 0x3A43, 0x3AA2, 0x3B00,
    0x3B5F, 0x3BBD, 0x3C1B, 0x3C79, 0x3CD7, 0x3D35, 0x3D92, 0x3DEF,
    0x3E4C, 0x3EA9, 0x3F05, 0x3F62, 0x3FBD, 0x4019, 0x4074, 0x40D0,
    0x412A, 0x4185, 0x41DF, 0x4239, 0x4292, 0x42EB, 0x4344, 0x439C,
    0x43F4, 0x444C, 0x44A3, 0x44FA, 0x4550, 0x45A6, 0x45FC, 0x4651,
    0x46A6, 0x46FA, 0x474E, 0x47A1, 0x47F4, 0x4846, 0x4898, 0x48E9,
    0x493A, 0x498A, 0x49D9, 0x4A29, 0x4A77, 0x4AC5, 0x4B13, 0x4B5F,
    0x4BAC, 0x4BF7, 0x4C42, 0x4C8D, 0x4CD7, 0x4D20, 0x4D68, 0x4DB0,
    0x4DF7, 0x4E3E, 0x4E84, 0x4EC9, 0x4F0E, 0x4F52, 0x4F95, 0x4FD7,
    0x5019, 0x505A, 0x509A, 0x50DA, 0x5118, 0x5156, 0x5194, 0x51D0,
    0x520C, 0x5247, 0x5281, 0x52BA, 0x52F3, 0x532A, 0x5361, 0x5397,
    0x53CC, 0x5401, 0x5434, 0x5467, 0x5499, 0x54CA, 0x54FA, 0x5529,
    0x5558, 0x5585, 0x55B2, 0x55DE, 0x5609, 0x5632, 0x565B, 0x5684,
    0x56AB, 0x56D1, 0x56F6, 0x571B, 0x573E, 0x5761, 0x5782, 0x57A3,
    0x57C3, 0x57E2, 0x57FF, 0x581C, 0x5838, 0x5853, 0x586D, 0x5886,
    0x589E, 0x58B5, 0x58CB, 0x58E0, 0x58F4, 0x5907, 0x5919, 0x592A,
    0x593A, 0x5949, 0x5958, 0x5965, 0x5971, 0x597C, 0x5986, 0x598F,
    0x5997, 0x599E, 0x59A4, 0x59A9, 0x59AD, 0x59B0, 0x59B2, 0x59B3
};

static s16 fir_coefficients[39] = 
{
    -0x001, 0x0000, 0x0002, 0x0000, -0x00A, 0x0000, 0x0023, 0x0000,
    -0x067, 0x0000, 0x010A, 0x0000, -0x268, 0x0000, 0x0534, 0x0000,
    -0xB90, 0x0000, 0x2806, 0x4000, 0x2806, 0x0000, -0xB90, 0x0000,
    0x0534, 0x0000, -0x268, 0x0000, 0x010A, 0x0000, -0x067, 0x0000,
    0x0023, 0x0000, -0x00A, 0x0000, 0x0002, 0x0000, -0x001,
};

enum
{
    LOOP_END = 0x1,
    LOOP_REPEAT = 0x2,
    LOOP_START = 0x4
};

enum
{
    MODE_LINEAR = 0,
    MODE_EXPONENTIAL = 1
};

enum
{
    DIR_INCREASE = 0,
    DIR_DECREASE = 1
};

struct spu_state g_spu;

void spu_reset(void)
{
    memset(&g_spu, 0, sizeof(struct spu_state));
    g_spu.regs.control.endx = 0x00ffffff;
    schedule_event(spu_tick, 0, 768);
}

u32 spu_read(u32 offset)
{
    if (offset > 0x200)
        return 0;
    u8 *data = ((u8 *)&g_spu.regs) + offset;
    return U32FromPtr(data);
}

static int spu_handle_write(u32 offset, u32 value)
{
    switch (offset)
    {
    case 0x188:
    case 0x18a:
        // KON
        value <<= ((offset & 0x2) << 3);
        
        for (u32 i = 0; i < 24; ++i)
        {
            u32 pos = 1 << i;
            if (value & pos)
            {
                g_spu.voice_data[i].stage = ADSR_ATTACK;
                g_spu.regs.voices[i].adsr_volume = 0;
                g_spu.regs.voices[i].repeat_addr = g_spu.regs.voices[i].start_addr;
                g_spu.voice_data[i].current_addr = g_spu.regs.voices[i].start_addr;
                g_spu.voice_data[i].pitch_counter = 0;
                g_spu.voice_data[i].has_samples = false;
                g_spu.voice_data[i].adsr_cycles = 0;
            }
        }
        g_spu.regs.control.endx &= ~(value & 0x00ffffff);
        break;
    case 0x18c:
    case 0x18e:
        // KOFF
        value <<= ((offset & 0x2) << 3);
        
        for (u32 i = 0; i < 24; ++i)
        {
            u32 pos = 1 << i;
            if (value & pos)
            {
                g_spu.voice_data[i].adsr_cycles = 0;
                g_spu.voice_data[i].stage = ADSR_RELEASE;
            }
        }
        g_spu.regs.control.endx |= (value & 0x00ffffff);
        break;
    case 0x1a2:
        g_spu.regs.control.reverb_work_start_addr = value;
        g_spu.current_reverb_addr = value << 3;
        break;
    case 0x1a6:
        g_spu.regs.control.data_transfer_addr = value;
        g_spu.current_transfer_addr = value << 3;
        break;
    case 0x1a8:
        SY_ASSERT(g_spu.transfer_fifo_len < ARRAYCOUNT(g_spu.transfer_fifo));
        g_spu.transfer_fifo[g_spu.transfer_fifo_len++] = value; // read behavior?
        break;
    default:
        return 0;
    }
    return 1;
}

void spu_write32(u32 offset, u32 value)
{
    if (spu_handle_write(offset, value))
        return;
    if (offset > 0x200)
        return;

    u8 *data = ((u8 *)&g_spu.regs) + offset;
    U32FromPtr(data) = value;
}

void spu_write16(u32 offset, u32 value)
{
    if (spu_handle_write(offset, value))
        return;
    if (offset > 0x200)
        return;

    u8 *data = ((u8 *)&g_spu.regs) + offset;
    U16FromPtr(data) = value;
}

static void update_adsr(struct spu_voice *regs, struct voice_internal *state)
{
    u8 mode = 0; // 0 - linear, 1 - exponential
    s8 shift = 0;
    u32 stepval = 0;
    //s32 step = 0;
    u8 dir = 0; // 0 - increase, 1 - decrease

    switch (state->stage)
    {
    case ADSR_ATTACK:
    {
        //dir = 0;
        mode = (regs->adsr >> 15) & 0x1;
        shift = (regs->adsr >> 10) & 0x1f;
        stepval = (regs->adsr >> 8) & 0x3;
        //step = 7 - step;
        state->adsr_target = 0x7fff;
        break;
    }
    case ADSR_DECAY:
    {
        s32 level = ((regs->adsr & 0xf) + 1) * 0x800;
        state->adsr_target = level;
        mode = 1;
        dir = 1;
        //step = -8;
        shift = (regs->adsr >> 4) & 0xf;
        break;
    }
    case ADSR_SUSTAIN:
    {
        shift = (regs->adsr >> 24) & 0x1f;
        dir = (regs->adsr >> 30) & 0x1;
        stepval = (regs->adsr >> 22) & 0x3;
        //step = dir ? -8 + stepval : 7 - stepval;
        mode = (regs->adsr >> 31);
        break;
    }
    case ADSR_RELEASE:
    {
        dir = 1;
        //step = -8;
        shift = (regs->adsr >> 16) & 0x1f;
        mode = (regs->adsr >> 21) & 0x1;
        state->adsr_target = 0;
        break;
    }
    default:
        break;
    }

    s32 adsr_step = 7 - stepval;
    if (dir)
        adsr_step = ~adsr_step;

    if (state->adsr_cycles > 0)
    {
        --state->adsr_cycles;
    }

    if (!state->adsr_cycles)
    {
        s32 adsr_cycles = 1 << MAX(0, shift - 11);
        adsr_step = adsr_step << MAX(0, 11 - shift);
        if (mode == MODE_EXPONENTIAL && dir == DIR_INCREASE && (regs->adsr_volume > 0x6000))
        {
            adsr_cycles *= 4;
        }
        if (mode == MODE_EXPONENTIAL && dir == DIR_DECREASE)
        {
            adsr_step = (adsr_step * regs->adsr_volume) >> 15;
        }
        state->adsr_cycles = adsr_cycles;
        regs->adsr_volume = (s16)clamp(regs->adsr_volume + adsr_step, 0, 0x7fff);
    }
    
    b8 target_reached = dir ? regs->adsr_volume <= state->adsr_target : regs->adsr_volume >= state->adsr_target;

    if (state->stage != ADSR_SUSTAIN && target_reached) // TODO: remove
    {
        switch (state->stage)
        {
        case ADSR_ATTACK:
            state->stage = ADSR_DECAY;
            break;
        case ADSR_DECAY:
            state->stage = ADSR_SUSTAIN;
            break;
        case ADSR_RELEASE:
            state->stage = ADSR_OFF;
            break;
        default:
            break;
        }
        state->adsr_cycles = 0;
    }
}

static inline s16 *reverb_load(u32 base, u32 addr, u32 offset)
{
    u32 len = 0x80000 - base;
    u32 relative = (addr + offset - base) % len;
    u32 result = (base + relative) & 0x7fffe;
    SY_ASSERT(result >= base && result <= 0x7fffe);
    return (s16 *)(g_spu.dram + result);
}

static inline s32 apply_volume(s32 level, s16 volume)
{
    return ((level * (s32)volume) >> 15);
}

static void apply_fir_filter(struct fir_filter *filter, s32 left, s32 right, spu_sample *output)
{
    // TODO: check this
    // input new sample
    filter->buffer[filter->index].left = left;
    filter->buffer[filter->index].right = right;

    // shift elements
    ++filter->index;
    if (filter->index > 38)
        filter->index = 0;

    int index = filter->index;

    s32 sum_l = 0;
    s32 sum_r = 0;

    for (int i = 0; i < 39; ++i)
    {
        --index;
        if (index < 0)
            index = 38;
        sum_l += (filter->buffer[index].left * fir_coefficients[i]) >> 15;
        sum_r += (filter->buffer[index].right * fir_coefficients[i]) >> 15;
    }

    //output->left = clamp16(sum_l);
    //output->right = clamp16(sum_r);
    SY_ASSERT(sum_l >= -0x8000 && sum_l <= 0x7fff); // TODO: remove
    SY_ASSERT(sum_r >= -0x8000 && sum_r <= 0x7fff);
    output->left = sum_l;
    output->right = sum_r;
}

static void output_reverb(s32 left_in, s32 right_in, s32 *left_out, s32 *right_out)
{
    struct spu_reverb *reverb = &g_spu.regs.reverb;

    s32 Lin = apply_volume(left_in, reverb->vLIN);
    s32 Rin = apply_volume(right_in, reverb->vRIN);
    
    u32 mBASE = g_spu.regs.control.reverb_work_start_addr << 3;
    u32 BufferAddress = g_spu.current_reverb_addr;

    u32 LSAME = (u32)reverb->mLSAME << 3;
    u32 RSAME = (u32)reverb->mRSAME << 3;

    s16 *mLSAME = reverb_load(mBASE, BufferAddress, LSAME);
    s16 *mRSAME = reverb_load(mBASE, BufferAddress, RSAME);
    s16 *dLSAME = reverb_load(mBASE, BufferAddress, (u32)reverb->dLSAME << 3);
    s16 *dRSAME = reverb_load(mBASE, BufferAddress, (u32)reverb->dRSAME << 3);

    s16 delayL = *reverb_load(mBASE, BufferAddress, LSAME - 2);
    s16 delayR = *reverb_load(mBASE, BufferAddress, RSAME - 2);

    // same side reflection
    *mLSAME = (s16)clamp16(apply_volume((Lin + apply_volume(dLSAME[0], reverb->vWALL) - delayL), reverb->vIIR) + delayL);
    *mRSAME = (s16)clamp16(apply_volume((Rin + apply_volume(dRSAME[0], reverb->vWALL) - delayR), reverb->vIIR) + delayR);

    u32 LDIFF = (u32)reverb->mLDIFF << 3;
    u32 RDIFF = (u32)reverb->mRDIFF << 3;

    s16 *mLDIFF = reverb_load(mBASE, BufferAddress, (u32)reverb->mLDIFF << 3);
    s16 *mRDIFF = reverb_load(mBASE, BufferAddress, (u32)reverb->mRDIFF << 3);
    s16 *dLDIFF = reverb_load(mBASE, BufferAddress, (u32)reverb->dLDIFF << 3);
    s16 *dRDIFF = reverb_load(mBASE, BufferAddress, (u32)reverb->dRDIFF << 3);

    s16 delayLD = *reverb_load(mBASE, BufferAddress, LDIFF - 2);
    s16 delayRD = *reverb_load(mBASE, BufferAddress, RDIFF - 2);

    // different side reflection
    *mLDIFF = (s16)clamp16(apply_volume((Lin + apply_volume(dRDIFF[0], reverb->vWALL) - delayLD), reverb->vIIR) + delayLD);
    *mRDIFF = (s16)clamp16(apply_volume((Rin + apply_volume(dLDIFF[0], reverb->vWALL) - delayRD), reverb->vIIR) + delayRD);

    s16 vCOMB1 = reverb->vCOMB1;
    s16 vCOMB2 = reverb->vCOMB2;
    s16 vCOMB3 = reverb->vCOMB3;
    s16 vCOMB4 = reverb->vCOMB4;

    s16 *mLCOMB1 = reverb_load(mBASE, BufferAddress, (u32)reverb->mLCOMB1 << 3);
    s16 *mLCOMB2 = reverb_load(mBASE, BufferAddress, (u32)reverb->mLCOMB2 << 3);
    s16 *mLCOMB3 = reverb_load(mBASE, BufferAddress, (u32)reverb->mLCOMB3 << 3);
    s16 *mLCOMB4 = reverb_load(mBASE, BufferAddress, (u32)reverb->mLCOMB4 << 3);

    s16 *mRCOMB1 = reverb_load(mBASE, BufferAddress, (u32)reverb->mRCOMB1 << 3);
    s16 *mRCOMB2 = reverb_load(mBASE, BufferAddress, (u32)reverb->mRCOMB2 << 3);
    s16 *mRCOMB3 = reverb_load(mBASE, BufferAddress, (u32)reverb->mRCOMB3 << 3);
    s16 *mRCOMB4 = reverb_load(mBASE, BufferAddress, (u32)reverb->mRCOMB4 << 3);

    // early echo
    s32 Lout = clamp16(apply_volume(vCOMB1, mLCOMB1[0]) + apply_volume(vCOMB2, mLCOMB2[0]) + apply_volume(vCOMB3, mLCOMB3[0]) + apply_volume(vCOMB4, mLCOMB4[0]));
    s32 Rout = clamp16(apply_volume(vCOMB1, mRCOMB1[0]) + apply_volume(vCOMB2, mRCOMB2[0]) + apply_volume(vCOMB3, mRCOMB3[0]) + apply_volume(vCOMB4, mRCOMB4[0]));
#if 1
    // late reverb APF1
    u32 LAPF1 = (u32)reverb->mLAPF1 << 3;
    u32 RAPF1 = (u32)reverb->mRAPF1 << 3;
    u32 dAPF1 = (u32)reverb->dAPF1 << 3;

    s16 *dLAPF1 = reverb_load(mBASE, BufferAddress, LAPF1 - dAPF1);
    s16 *dRAPF1 = reverb_load(mBASE, BufferAddress, RAPF1 - dAPF1);
    s16 *mLAPF1 = reverb_load(mBASE, BufferAddress, LAPF1);
    s16 *mRAPF1 = reverb_load(mBASE, BufferAddress, RAPF1);
    
    Lout = clamp16(Lout - apply_volume(reverb->vAPF1, dLAPF1[0]));
    *mLAPF1 = Lout;
    Lout = clamp16(apply_volume(Lout, reverb->vAPF1) + dLAPF1[0]);

    Rout = clamp16(Rout - apply_volume(reverb->vAPF1, dRAPF1[0]));
    *mRAPF1 = Rout;
    Rout = clamp16(apply_volume(Rout, reverb->vAPF1) + dRAPF1[0]);

    // late reverb APF2
    u32 LAPF2 = (u32)reverb->mLAPF2 << 3;
    u32 RAPF2 = (u32)reverb->mRAPF2 << 3;
    u32 dAPF2 = (u32)reverb->dAPF2 << 3;

    s16 *dLAPF2 = reverb_load(mBASE, BufferAddress, LAPF2 - dAPF2);
    s16 *dRAPF2 = reverb_load(mBASE, BufferAddress, RAPF2 - dAPF2);
    s16 *mLAPF2 = reverb_load(mBASE, BufferAddress, LAPF2);
    s16 *mRAPF2 = reverb_load(mBASE, BufferAddress, RAPF2);
    
    Lout = clamp16(Lout - apply_volume(reverb->vAPF2, dLAPF2[0]));
    *mLAPF2 = Lout;
    Lout = clamp16(apply_volume(Lout, reverb->vAPF2) + dLAPF2[0]);

    Rout = clamp16(Rout - apply_volume(reverb->vAPF2, dRAPF2[0]));
    *mRAPF2 = Rout;
    Rout = clamp16(apply_volume(Rout, reverb->vAPF2) + dRAPF2[0]);
#endif
    Lout = apply_volume(Lout, g_spu.regs.control.reverb_volume_left);
    Rout = apply_volume(Rout, g_spu.regs.control.reverb_volume_right);

    g_spu.current_reverb_addr = MAX(mBASE, (BufferAddress + 2) & 0x7fffe);

    *left_out = Lout;
    *right_out = Rout;
}

static inline s16 read_cd_buffer(void)
{
    s16 result = g_spu.cd_buffer[g_spu.cd_buffer_index++];
    return result;
}

void spu_tick(u32 param, s32 ticks_late)
{
    // not sure if we need to emulate this, but spustat applies its changes delayed, im assuming to the next tick
    g_spu.regs.control.spustat &= ~(0x5f);
    g_spu.regs.control.spustat |= (g_spu.regs.control.spustat & 0x1f);
    g_spu.regs.control.spustat |= (g_spu.regs.control.spustat << 2) & 0x40;
    // pending transfers also happen on the next tick
    if (g_spu.transfer_fifo_len)
    {
        u32 size = g_spu.transfer_fifo_len * 2;
        memcpy((g_spu.dram + g_spu.current_transfer_addr), g_spu.transfer_fifo, size);
        g_spu.current_transfer_addr += size;
        g_spu.transfer_fifo_len = 0;
    }

    s32 mixed_left = 0;
    s32 mixed_right = 0;

    s32 reverb_input_left = 0;
    s32 reverb_input_right = 0;
    
    for (u32 i = 0; i < 24; ++i)
    {
        struct voice_internal *state = &g_spu.voice_data[i];
        struct spu_voice *regs = &g_spu.regs.voices[i];

        if (state->stage == ADSR_OFF)
            continue;

        if (!state->has_samples)
        {
            // save last samples from last block
            state->decoded_samples[0] = state->decoded_samples[28];
            state->decoded_samples[1] = state->decoded_samples[29];
            state->decoded_samples[2] = state->decoded_samples[30];

            u8 *data = g_spu.dram + (state->current_addr << 3);

            u8 *adpcm_block = data;
            u8 shift = adpcm_block[0] & 0xf;
            if (shift > 12)
                shift = 9;

            u8 amt = 12 - shift;
            u8 filter = (adpcm_block[0] >> 4) & 0x7;
            u8 flags = adpcm_block[1];

            if (flags & LOOP_START)
                regs->repeat_addr = (u16)state->current_addr;

            // store flags for when we process them later
            state->block_flags = flags;

            s32 pos_adpcm_table[] = {0, 60, 115, 98, 122};
            s32 neg_adpcm_table[] = {0, 0, -52, -55, -60};

            s32 f0 = pos_adpcm_table[filter];
            s32 f1 = neg_adpcm_table[filter];

            s16 older = state->decoded_samples[1];
            s16 old = state->decoded_samples[2];
            for (int j = 0; j < 14; ++j)
            {
                // first sample   
                s8 a0 = adpcm_block[2 + j] & 0xf;
                a0 <<= 4;
                a0 >>= 4;

                u32 t0 = a0;
                s32 s0 = (u32)(t0 << amt) + ((old * f0 + older * f1 + 32) / 64);
                s16 sample0 = (s16)clamp16(s0);

                older = old;
                old = sample0;

                // second sample
                s8 a1 = (*(adpcm_block + 2 + j) >> 4) & 0xf;
                a1 <<= 4;
                a1 >>= 4;

                u32 t1 = a1;
                s32 s1 = (t1 << amt) + ((old * f0 + older * f1 + 32) / 64);
                s16 sample1 = (s16)clamp16(s1);

                older = old;
                old = sample1;

                state->decoded_samples[3 + j * 2] = sample0;
                state->decoded_samples[3 + j * 2 + 1] = sample1;
            }
            state->has_samples = true;
        }
        // gauss table index
        u32 g = ((state->pitch_counter >> 4) & 0xff);
        // adpcm sample index
        u32 s = ((state->pitch_counter >> 12) & 0x1f) + 3;

        s32 interp      = ((gauss_table[0x0ff - g] * state->decoded_samples[s - 3]) >> 15);
        interp = interp + ((gauss_table[0x1ff - g] * state->decoded_samples[s - 2]) >> 15);
        interp = interp + ((gauss_table[0x100 + g] * state->decoded_samples[s - 1]) >> 15);
        interp = interp + ((gauss_table[0x000 + g] * state->decoded_samples[s    ]) >> 15);

        // update pitch counter
        s32 step = regs->sample_rate;
        if (g_spu.regs.control.pmon & (1 << i) && (i > 0))
        {
            s16 factor = g_spu.voice_data[i - 1].amplitude;
            factor += 0x8000;
            step = sign_extend16_32(step);
            step = (step * factor) >> 15;
            step &= 0xffff;
            SY_ASSERT(0); // TODO: threads of fate hits this
        }
        if (step > 0x3fff)
        {
            step = 0x4000;
        }
        state->pitch_counter += step;

        u32 sample_index = (state->pitch_counter >> 12) & 0x1f;
        if (sample_index >= 28)
        {
            // pitch counter determines the 'sampling frequency', so the next block has to also follow the same frequency (which is why we subtract)
            sample_index -= 28;
            state->pitch_counter &= ~(0x1f << 12);
            state->pitch_counter |= (sample_index << 12);
            // NOTE: if were looping on a single block, setting this to 0 here means we re-decode the same block over and over
            state->has_samples = false;

            // loop end flag sets endx and jumps
            if (state->block_flags & LOOP_END)
            {
                state->current_addr = regs->repeat_addr;
                g_spu.regs.control.endx |= (1 << i);
                if (!(state->block_flags & LOOP_REPEAT))
                {
                    state->stage = ADSR_RELEASE;
                    regs->adsr_volume = 0;
                    state->adsr_cycles = 0;
                }
            }
            else
            {
                state->current_addr += 2;
            }
        }

        update_adsr(regs, state);

        // apply adsr volume
        s32 level = (interp * regs->adsr_volume) >> 15;

        state->amplitude = level; // OUTx

        // apply voice volume
        s16 volL = regs->volume_left;
        s16 volR = regs->volume_right;

        // TODO: sweep
        if (volL & 0x8000)
            SY_ASSERT(0);
        else
            volL <<= 1;

        if (volR & 0x8000)
            SY_ASSERT(0);
        else
            volR <<= 1;

        s32 voice_output_left = apply_volume(level, volL);
        s32 voice_output_right = apply_volume(level, volR);

        mixed_left += voice_output_left;
        mixed_right += voice_output_right;

        if (g_spu.regs.control.reverb_mode_enable & (1 << i))
        {
            reverb_input_left += voice_output_left;
            reverb_input_right += voice_output_right;
        }
    }

    if (g_spu.regs.control.spucnt & 0x1)
    {
        if (g_spu.cd_buffer_index < g_spu.cd_buffer_length)
        {
            SY_ASSERT((g_spu.cd_buffer_length & 0x1) == 0);
            s16 raw_left = read_cd_buffer();
            s16 raw_right = read_cd_buffer();
            //g_spu.cd_buffer_length -= 2;
            s32 left = clamp16(((raw_left * g_cdrom.vol_LL) >> 7) + ((raw_right * g_cdrom.vol_RL) >> 7));
            s32 right = clamp16(((raw_right * g_cdrom.vol_RR) >> 7) + ((raw_left * g_cdrom.vol_LR) >> 7));

            s32 cd_vol_left = apply_volume(left, g_spu.regs.control.cd_volume_left);
            s32 cd_vol_right = apply_volume(right, g_spu.regs.control.cd_volume_right);

            mixed_left += cd_vol_left;
            mixed_right += cd_vol_right;

            if (g_spu.regs.control.spucnt & 0x4)
            {
                reverb_input_left += cd_vol_left;
                reverb_input_right += cd_vol_right;
            }
        }
    }

    if ((g_spu.regs.control.spucnt >> 7) & 0x1)
    {
        s32 reverb_out_right;
        s32 reverb_out_left;
        // NOTE: we are pushing and processing both inputs at once for now
        spu_sample downsample;
        apply_fir_filter(&g_spu.input_filter, reverb_input_left, reverb_input_right, &downsample);
        reverb_input_left = downsample.left;
        reverb_input_right = downsample.right;

        spu_sample upsample;
        if (g_spu.reverb_index)
        {
            // process right and output?
            output_reverb(reverb_input_left, reverb_input_right, &reverb_out_left, &reverb_out_right);
            apply_fir_filter(&g_spu.output_filter, reverb_out_left, reverb_out_right, &upsample);
        }
        else
        {
            // process left (push zeros for upsample)
            apply_fir_filter(&g_spu.output_filter, 0, 0, &upsample);
        }

        upsample.left <<= 1;
        upsample.right <<= 1;

        // TODO: remove
        SY_ASSERT(upsample.left >= -0x8000 && upsample.left <= 0x7fff);
        SY_ASSERT(upsample.right >= -0x8000 && upsample.right <= 0x7fff);

        reverb_out_left = upsample.left;
        reverb_out_right = upsample.right;

        g_spu.reverb_index = !g_spu.reverb_index;
        
        mixed_left += reverb_out_left;
        mixed_right += reverb_out_right;
    }

    mixed_left = clamp16(mixed_left);
    mixed_right = clamp16(mixed_right);

    // apply master volume
    s16 main_vol_left = g_spu.regs.control.main_volume_left;
    s16 main_vol_right = g_spu.regs.control.main_volume_right;

    // TODO: sweep
    if (main_vol_left & 0x8000)
        SY_ASSERT(0);
    else
        main_vol_left <<= 1;
    
    if (main_vol_right & 0x8000)
        SY_ASSERT(0);
    else
        main_vol_right <<= 1;

    
    mixed_left = apply_volume(mixed_left, main_vol_left);
    mixed_right = apply_volume(mixed_right, main_vol_right);

    audio_buffer_write(mixed_left, mixed_right);

    schedule_event(spu_tick, 0, 768 - ticks_late);
}
