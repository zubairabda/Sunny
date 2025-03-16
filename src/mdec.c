#include "mdec.h"
#include "dma.h"
#include "memory.h"

typedef struct
{
    u32 parameters_left : 16;
    u32 current_block : 3;
    u32 : 4;
    u32 depth_only : 1;
    u32 signed_output : 1;
    u32 output_depth : 2;
    u32 data_out_req : 1;
    u32 data_in_req : 1;
    u32 busy : 1;
    u32 data_in_fifo_full : 1;
    u32 data_out_fifo_empty : 1;
} mdec_status;

static mdec_status stat;
static u32 parameters_left;
static u32 parameter_fifo[65536];
static u32 parameter_fifo_count;
static u32 command;

static b8 transfer_pending;

static u32 data_fifo[65536];
static u32 data_fifo_size;
static u32 data_fifo_index; // used for io reads

static u8 iq_table[128]; // luminance + color
static s16 scale_table[64];

void mdec_reset(u32 flags)
{
    if (flags & MDEC_RESET)
    {
        stat.busy = 0;
        stat.data_out_fifo_empty = 1;
        parameter_fifo_count = 0;
        parameters_left = 0;
        data_fifo_index = data_fifo_size = 0;
    }

    if (flags & MDEC_ENABLE_DATAOUT)
    {
        stat.data_out_req = 1; // NOTE: should only be set when data is available?
    }

    if (flags & MDEC_ENABLE_DATAIN)
    {
        stat.data_in_req = 1;
    }
}

u32 mdec_getstat(void)
{
    u32 result;
    memcpy(&result, &stat, sizeof(u32));
    return result;
}

enum
{
    MDEC_DEPTH_4,
    MDEC_DEPTH_8,
    MDEC_DEPTH_24,
    MDEC_DEPTH_15
};

static u32 decode_index;

u16 sign_extend10_16(u16 value)
{
    s16 temp = (s16)value;
    return (temp << 6) >> 6;
}

u16 rl_clamp11(s16 value)
{
    if (value > 0x3ff)
        return 0x3ff;
    if (value < -0x400)
        return -0x400;
    return value;
}

static u8 zigzag[64] = 
{
    0, 1, 5, 6, 14, 15, 27, 28,
    2, 4, 7, 13, 16, 26, 29, 42,
    3, 8, 12, 17, 25, 30, 41, 43,
    9, 11, 18, 24, 31, 40, 44, 53,
    10, 19, 23, 32, 39, 45, 52, 54,
    20, 22, 33, 38, 46, 51, 55, 60,
    21, 34, 37, 47, 50, 56, 59, 61,
    35, 36, 48, 49, 57, 58, 62, 63
};

static u8 zagzig[64] = 
{
    0, 1, 8, 16, 9, 2, 3, 10,
    17, 24, 32, 25, 18, 11, 4, 5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

static f32 scalefactor[8] =
{
    1.000000000, 1.387039845, 1.306562965, 1.175875602,
    1.000000000, 0.785694958, 0.541196100, 0.275899379
};

static f32 scalezag[64] = 
{
    0.125,                0.173379980625,       0.173379980625,       0.163320370625,       
    0.24048494145220303,  0.163320370625,       0.14698445025,        0.22653186155704255,
    0.22653186155704255,  0.14698445025,        0.125,                0.20387328909217023,
    0.21338834768869888,  0.20387328909217023,  0.125,                0.09821186975,
    0.173379980625,       0.192044439127535,    0.192044439127535,    0.173379980625,
    0.09821186975,        0.0676495125,         0.13622377659520019,  0.163320370625,
    0.17283542892235781,  0.163320370625,       0.13622377659520019,  0.0676495125,
    0.034487422375,       0.09383256933232556,  0.1283199917387538,   0.14698445025,
    0.14698445025,        0.1283199917387538,   0.09383256933232556,  0.034487422375,
    0.04783542898546954,  0.08838834763280455,  0.11548494146582684,  0.125,
    0.11548494146582684,  0.08838834763280455,  0.04783542898546954,  0.04505998883348734,
    0.07954741123594403,  0.09821186975,        0.09821186975,        0.07954741123594403,
    0.04505998883348734,  0.0405529185466314,   0.0676495125,         0.07716457087832772,
    0.0676495125,         0.0405529185466314,   0.034487422375,       0.05315188088240797,
    0.05315188088240797,  0.034487422375,       0.027096593874453886, 0.036611652331901244,
    0.027096593874453886, 0.018664458488402737, 0.018664458488402737, 0.009515058416573205
};

static void idct_core(u16 *blk)
{
    u16 temp[64] = {0};
    u16 *src = blk;
    u16 *dst = temp;
    for (u8 pass = 0; pass < 2; ++pass)
    {
        for (u8 x = 0; x < 8; ++x)
        {
            for (u8 y = 0; y < 8; ++y)
            {
                s32 sum = 0;
                for (u8 z = 0; z < 8; ++z)
                {
                    sum += src[y + z * 8] * (scale_table[x + z * 8] / 8);
                }
                dst[x + y * 8] = (sum + 0xfff) / 0x2000;
            }
        }
        u16 *swap = src;
        src = dst;
        dst = swap;
    }
}

static void rl_decode_block(u16 *blk, u8 *iq) 
{
    u16 *data = (u16 *)parameter_fifo;
    memset(blk, 0, sizeof(u16) * 64);

    for (;;)
    {
        u16 dct = data[decode_index];
        decode_index += 2;
        if (dct == 0xfe00) // padding
            continue;
        
        u8 q = (dct >> 10) & 0x3f;
        u16 dc = dct & 0x3ff;
        u16 val = sign_extend10_16(dc) * iq[0];
        u8 k = 0;
        while (k < 64)
        {
            if (q == 0)
            {
                val = sign_extend10_16(dc) * 2;
            }
            val = rl_clamp11(val);
            if (q > 0)
                blk[zagzig[k]] = val;
            else if (q == 0)
                blk[k] = val;
            u16 rle = data[decode_index];
            decode_index += 2;
            if (rle == 0xfe00) // end code
                break;
            k = k + ((rle >> 10) & 0x3f) + 1;
            val = (sign_extend10_16(rle & 0x3ff) * iq[k] * q + 4) / 8;
        }
        idct_core(blk);
        break;
    }
}

static u8 clamp8(s16 value)
{
    if (value > 127)
        return 127;
    if (value < -128)
        return -128;
    return value;
}

static void yuv_to_rgb15(u16 *dst, u16 *crblk, u16 *cbblk, u16 *yblk, u8 xx, u8 yy)
{
    for (u8 y = 0; y < 8; ++y)
    {
        for (u8 x = 0; x < 8; ++x)
        {
            u16 r = *(crblk + ((x + xx) / 2) + ((y + yy) / 2) * 8);
            u16 b = *(cbblk + ((x + xx) / 2) + ((y + yy) / 2) * 8);
            u16 g = (-0.3437f * b) + (-0.7143f * r);
            r = (f32)r * 1.402f;
            b = (f32)b * 1.772f;
            u16 Y = *(yblk + x + y * 8);
            r = clamp8(r + Y);
            g = clamp8(g + Y);
            b = clamp8(b + Y);
            u16 red = (r >> 3) & 0x1f;
            u16 green = (g >> 3) & 0x1f;
            u16 blue = (b >> 3) & 0x1f;
            u16 result = r | (g << 5) | (b << 10);
            dst[(x + xx) + (y + yy) * 16] = result;
        }
    }
}

void mdec_on_dma(void)
{
    if (stat.data_out_req)
    {
        if (!stat.data_out_fifo_empty)
        {
            // memcpy data to ram
            u32 dst = g_dma.ports[CH_MDECOUT].madr;
            // TODO: reorder blocks
            memcpy(g_ram + dst, data_fifo, data_fifo_size);
            stat.data_out_fifo_empty = 1;
        }
        else
        {
            transfer_pending = true; // TODO: assumes software won't write 0 to bit24
        }
    }
}

void mdec_decode(void)
{
    u32 bit_depth = (command >> 27) & 0x3;
    b8 signed_output = (command >> 26) & 0x1;
    b8 set_bit15 = (command >> 25) & 0x1;
    decode_index = 0;
    data_fifo_size = 0;

    while (decode_index < parameter_fifo_count)
    {
        if (bit_depth & 0x2)
        {
            u8 *iq_uv = &iq_table[64];
            u16 output[16 * 16];
            u16 crblk[64];
            u16 cbblk[64];
            u16 yblk[64][4];
            rl_decode_block(crblk, iq_uv);
            rl_decode_block(cbblk, iq_uv);
            rl_decode_block(yblk[0], iq_table);
            rl_decode_block(yblk[1], iq_table);
            rl_decode_block(yblk[2], iq_table);
            rl_decode_block(yblk[3], iq_table);
            if (bit_depth == MDEC_DEPTH_15)
            {
                u16 *dst = (u16 *)data_fifo + data_fifo_size;
                yuv_to_rgb15(dst, crblk, cbblk, yblk[0], 0, 0);
                yuv_to_rgb15(dst, crblk, cbblk, yblk[1], 0, 8);
                yuv_to_rgb15(dst, crblk, cbblk, yblk[2], 8, 0);
                yuv_to_rgb15(dst, crblk, cbblk, yblk[3], 8, 8);
                data_fifo_size += 256 * 2;
            }
            else
            {
                SY_ASSERT(0);
            }
        }
        else
        {
            SY_ASSERT(0);
        }
    }

    b8 dma_enabled = g_dma.control & 0x80;
    if (dma_enabled && stat.data_out_req && transfer_pending)
    {
        // memcpy the data
        // TODO: handle reading from the io port, which apparently does not do reordering of blocks
        transfer_pending = false;
        u32 dst = g_dma.ports[CH_MDECOUT].madr;
        // TODO: reorder blocks
        memcpy(g_ram + dst, data_fifo, data_fifo_size);
        dma_set_interrupt(CH_MDECOUT);
    }
    else
    {
        stat.data_out_fifo_empty = 0;
    }
}

void mdec_command(u32 word)
{
    if (!parameters_left)
    {
        printf("[MDEC] Send command: %d\n", (word >> 29));
        command = word;
        u32 cmd = word >> 29;
        switch (cmd)
        {
        case 0x1:
            parameters_left = word & 0xffff;
            SY_ASSERT(parameters_left < 65536);
            break;
        case 0x2:
            parameters_left = word & 0x1 ? 32 : 16;
            break;
        case 0x3:
            parameters_left = 32;
            break;
        default:
            break;
        }
        stat.busy = 1;
        parameter_fifo_count = 0;
        stat.parameters_left = parameters_left;
    }
    else
    {
        printf("[MDEC] Send parameter: %08x\n", word);
        parameter_fifo[parameter_fifo_count++] = word;

        --parameters_left;
        if (!parameters_left)
        {
            u32 cmd = command >> 29;
            switch (cmd)
            {
            case 1:
                mdec_decode();
                stat.data_out_fifo_empty = 0;
                break;
            case 2:
                u8 src = 0;
                u8 count = (command & 0x1) ? 64 : 128;
                for (u8 i = 0; i < count; i += 4)
                {
                    iq_table[i] = parameter_fifo[src] & 0xff;
                    iq_table[i + 1] = (parameter_fifo[src] >> 8) & 0xff;
                    iq_table[i + 2] = (parameter_fifo[src] >> 16) & 0xff;
                    iq_table[i + 3] = (parameter_fifo[src] >> 24) & 0xff;
                    ++src;
                }
                break;
            case 3:
                for (u8 i = 0; i < 32; ++i)
                {
                    scale_table[i * 2] = parameter_fifo[i] & 0xffff;
                    scale_table[i * 2 + 1] = (parameter_fifo[i] >> 16) & 0xffff;
                }
                break;
            default:
                break;
            }
            stat.busy = 0;
        }
    }

}

u32 mdec_read(void)
{
    u32 result = 0;
    if (!stat.data_out_fifo_empty)
    {
        if (data_fifo_index < data_fifo_size)
        {
            result = data_fifo[data_fifo_index++];
            if (data_fifo_index == data_fifo_size)
                stat.data_out_fifo_empty = 1;
        }
    }
    return result;
}
