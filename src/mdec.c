#include "mdec.h"
#include "dma.h"
#include "memory.h"
#include "gpu_common.h"
#include "event.h"
#include "debug.h"

struct mdec_state g_mdec;

void mdec_reset(u32 flags)
{
    if (flags & MDEC_RESET)
    {
        g_mdec.stat.busy = 0;
        //stat.data_out_fifo_empty = 1;
        g_mdec.parameter_fifo_count = 0;
        g_mdec.parameters_left = 0;
        g_mdec.data_fifo_head = g_mdec.data_fifo_tail = 0;
        g_mdec.data_fifo_count = 0;
    }

    g_mdec.dma_out_enabled = (flags & MDEC_ENABLE_DATAOUT) ? true : false;
    g_mdec.dma_in_enabled = (flags & MDEC_ENABLE_DATAIN) ? true : false;
}

enum
{
    MDEC_STAT_DATA_OUT_REQ   = (1 << 27),
    MDEC_STAT_DATA_IN_REQ    = (1 << 28),
    MDEC_STAT_BUSY           = (1 << 29),
    MDEC_STAT_IN_FIFO_FULL   = (1 << 30),
    MDEC_STAT_OUT_FIFO_EMPTY = (1 << 31)
};

u32 mdec_getstat(void)
{
    u32 result = g_mdec.stat.value;
#if 0
    memcpy(&result, &stat, sizeof(u32));
#else
    result |= (g_mdec.parameters_left - 1) & 0xffff;
    if (g_mdec.dma_out_enabled && g_mdec.data_fifo_count)
        result |= MDEC_STAT_DATA_OUT_REQ; // ref: this flag is unset once the first few words are read
    // TODO: data-in fifo, depth/signed/set bits
    if (!g_mdec.data_fifo_count)
        result |= MDEC_STAT_OUT_FIFO_EMPTY;
#endif
    return result;
}

enum
{
    MDEC_DEPTH_4,
    MDEC_DEPTH_8,
    MDEC_DEPTH_24,
    MDEC_DEPTH_15
};

#define MDEC_DATA_END 0xFE00

static s32 clamp11(s32 value)
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

// used for fast idct
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
                    sum += (s16)src[y + z * 8] * (s32)(g_mdec.scale_table[x + z * 8] / 8);
                }
                dst[x + y * 8] = (sum + 0xfff) / 0x2000;
            }
        }
        u16 *swap = src;
        src = dst;
        dst = swap;
    }
}

static b8 rl_decode_block(u16 *blk, u8 *iq) 
{
    u16 *data = (u16 *)g_mdec.parameter_fifo;
    memset(blk, 0, sizeof(u16) * 64);

    do
    {
        u16 dct = data[g_mdec.decode_index++]; // the first value is the DC
        if (dct == MDEC_DATA_END) // padding value
            continue;
        
        u8 q = (dct >> 10) & 0x3f;
        u16 dc = dct & 0x3ff;
        s32 val = SIGN_EXTEND32(10, dc) * iq[0];
        u8 k = 0;
        while (k < 64)
        {
            if (q == 0)
            {
                val = SIGN_EXTEND32(10, dc) * 2;
            }
            val = clamp11(val);
            if (q > 0)
                blk[zagzig[k]] = val;
            else if (q == 0)
                blk[k] = val;
            u16 rle = data[g_mdec.decode_index++];
            //if (rle == 0xfe00)
            //    break;
            u16 ac = rle & 0x3ff;
            u16 len = (rle >> 10) & 0x3f;
            k = k + len + 1;
            val = (SIGN_EXTEND32(10, ac) * iq[k] * q + 4) / 8;
        }
        idct_core(blk);
        return true;
    } while (g_mdec.decode_index < g_mdec.halfwords);

    return false;
}

static u8 clamp8(s32 value)
{
    if (value > 127)
        return 127;
    if (value < -128)
        return -128;
    return value;
}

static void yuv_to_rgb15(u16 *dst, u16 *crblk, u16 *cbblk, u16 *yblk, u8 xx, u8 yy, b8 signed_output)
{
    for (u8 y = 0; y < 8; ++y)
    {
        for (u8 x = 0; x < 8; ++x)
        {
            s16 cr = *(crblk + ((x + xx) / 2) + ((y + yy) / 2) * 8);
            s16 cb = *(cbblk + ((x + xx) / 2) + ((y + yy) / 2) * 8);
            u32 g = (-0.3437f * cb) + (-0.7143f * cr);
            u32 r = cr * 1.402f;
            u32 b = cb * 1.772f;
            s16 Y = *(yblk + x + y * 8);
            r = clamp8(r + Y);
            g = clamp8(g + Y);
            b = clamp8(b + Y);
            #if 0
            u16 red = (r >> 3) & 0x1f;
            u16 green = (g >> 3) & 0x1f;
            u16 blue = (b >> 3) & 0x1f;
            u16 result = r | (g << 5) | (b << 10);
            #else
            u32 result = r | (g << 8) | (b << 16);
            result ^= (0x808080 * !signed_output);
            #endif
            dst[(x + xx) + (y + yy) * 16] = color16from24(result);
        }
    }
}

static void mdec_decode(void)
{
    u32 bit_depth = (g_mdec.command >> 27) & 0x3; // TODO: move
    b8 signed_output = (g_mdec.command >> 26) & 0x1;
    b8 set_bit15 = (g_mdec.command >> 25) & 0x1;
    u16 *data = (u16 *)g_mdec.parameter_fifo;

    u32 end = ((g_mdec.data_fifo_head - 1) & (DATA_FIFO_SIZE - 1));
    for (;;)
    {
#if 0
        u16 dct = data[decode_index++];
        if (dct == MDEC_DATA_END)
            continue;
#endif
        if (g_mdec.decode_index == g_mdec.halfwords)
            break;

        if (bit_depth & 0x2)
        {
            u32 block_size = (bit_depth == MDEC_DEPTH_24) ? 192 : 128;
            if ((g_mdec.data_fifo_count + block_size) > DATA_FIFO_SIZE)
                break;
            u8 *iq_uv = &g_mdec.iq_table[64];
            //u16 output[16 * 16];
            u16 crblk[64];
            u16 cbblk[64];
            u16 yblk[4][64];
            if (rl_decode_block(crblk, iq_uv))
            {
                rl_decode_block(cbblk, iq_uv);
                rl_decode_block(yblk[0], g_mdec.iq_table);
                rl_decode_block(yblk[1], g_mdec.iq_table);
                rl_decode_block(yblk[2], g_mdec.iq_table);
                rl_decode_block(yblk[3], g_mdec.iq_table);
            }
            else
            {
                break;
            }
#if 0
            // reorder blocks into contiguous buffer
            u16 lum[256];
            for (int x = 0; x < 16; ++x)
            {
                for (int y = 0; y < 16; ++y)
                {
                    int b = y >= 8 ? 2 : 0;
                    u16 Y = 0;
                    int row = y & 0x7; // normalize coords
                    int col = x & 0x7;
                    if (x < 8)
                    {
                        Y = yblk[b][row * 8 + col];
                    }
                    else
                    {
                        Y = yblk[1 + b][row * 8 + col];
                    }
                    u8 grayscale = Y >> 8;
                    u32 value = grayscale | (grayscale << 8) | (grayscale << 16);
                    value ^= (0x808080 * !signed_output);
                    lum[y * 16 + x] = color16from24(value);
                }
            }
#endif
            if (bit_depth == MDEC_DEPTH_15)
            {
                u32 dst[128];
                u16 *result = (u16 *)dst;
                yuv_to_rgb15(result, crblk, cbblk, yblk[0], 0, 0, signed_output);
                yuv_to_rgb15(result, crblk, cbblk, yblk[1], 8, 0, signed_output);
                yuv_to_rgb15(result, crblk, cbblk, yblk[2], 0, 8, signed_output);
                yuv_to_rgb15(result, crblk, cbblk, yblk[3], 8, 8, signed_output);
                u32 idx = g_mdec.data_fifo_tail;
                for (u16 i = 0; i < 128; ++i)
                {
                    g_mdec.data_fifo[idx] = dst[i];
                    idx = (idx + 1) & (DATA_FIFO_SIZE - 1);
                }
                g_mdec.data_fifo_tail = (g_mdec.data_fifo_tail + 128) & (DATA_FIFO_SIZE - 1);
                g_mdec.data_fifo_count += 128;
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

    SY_ASSERT(g_mdec.data_fifo_count <= DATA_FIFO_SIZE);
}

void mdec_command(u32 word)
{
    if (!g_mdec.parameters_left)
    {
        //debug_log("[MDEC] Send command: %d\n", (word >> 29));
        g_mdec.command = word;
        u32 cmd = word >> 29;
        switch (cmd)
        {
        case 0x1:
            g_mdec.parameters_left = word & 0xffff;
            break;
        case 0x2:
            g_mdec.parameters_left = word & 0x1 ? 32 : 16;
            break;
        case 0x3:
            g_mdec.parameters_left = 32;
            break;
        default:
            break;
        }
        g_mdec.stat.busy = 1;
        g_mdec.parameter_fifo_count = 0;
    }
    else
    {
        //debug_log("[MDEC] Send parameter: %08x\n", word);
        g_mdec.parameter_fifo[g_mdec.parameter_fifo_count++] = word;

        --g_mdec.parameters_left;
        if (!g_mdec.parameters_left)
        {
            u32 cmd = g_mdec.command >> 29;
            switch (cmd)
            {
            case 1:
                g_mdec.halfwords = g_mdec.parameter_fifo_count << 1;
                g_mdec.decode_index = 0;
                mdec_decode();
                struct dma_transfer *transfer = &g_dma.transfers[CH_MDECOUT];
                if (transfer->pending && (g_mdec.data_fifo_count >= transfer->pending_words))
                    transfer->pending = false;
                break;
            case 2:
                u8 src = 0;
                u8 count = (g_mdec.command & 0x1) ? 128 : 64;
                for (u8 i = 0; i < count; i += 4)
                {
                    u32 param = g_mdec.parameter_fifo[src++];
                    g_mdec.iq_table[i] = param & 0xff;
                    g_mdec.iq_table[i + 1] = (param >> 8) & 0xff;
                    g_mdec.iq_table[i + 2] = (param >> 16) & 0xff;
                    g_mdec.iq_table[i + 3] = (param >> 24) & 0xff;
                }
                break;
            case 3:
                for (u8 i = 0; i < 32; ++i)
                {
                    g_mdec.scale_table[i * 2] = g_mdec.parameter_fifo[i] & 0xffff;
                    g_mdec.scale_table[i * 2 + 1] = (g_mdec.parameter_fifo[i] >> 16) & 0xffff;
                }
                break;
            default:
                break;
            }
            g_mdec.stat.busy = 0;
        }
    }

}

u32 mdec_read(void)
{
    u32 result = 0;
    return result;
}

b8 mdecout_on_dma(b8 from_ram, s8 step, u32 size, u32 *paddr)
{
    if (g_mdec.dma_out_enabled)
    {
        SY_ASSERT(size < DATA_FIFO_SIZE);
        if (size > g_mdec.data_fifo_count)
        {
            mdec_decode();
            if (size > g_mdec.data_fifo_count)
                return false;
        }

        //u32 *at = (u32 *)(g_ram + dst);
        u32 addr = *paddr;
        u32 src = g_mdec.data_fifo_head;
        u32 left = size;
        while (left--)
        {
            u32 *dst = (u32 *)(g_ram + addr);
            *dst = g_mdec.data_fifo[src];
            src = (src + 1) & (DATA_FIFO_SIZE - 1);
            addr += step;
        }
        *paddr = addr;
        g_mdec.data_fifo_count -= size;
        g_mdec.data_fifo_head = (g_mdec.data_fifo_head + size) & (DATA_FIFO_SIZE - 1);
        debug_log("[MDEC] transferring block of %u bytes, %u left in queue..\n", size, g_mdec.data_fifo_count);

        //if (data_fifo_count == 0)
        //    stat.data_out_fifo_empty = 1;
        return true;
    }
    return false;
}
