#include "gte.h"
#include "cpu.h"
#include "debug.h"

#include <immintrin.h>

#define VXY0 0
#define VZ0 1

#define VXY1 2
#define VZ1 3

#define VXY2 4
#define VZ2 5

#define RGBC 6

#define OTZ 7

#define IR0 8
#define IR1 9
#define IR2 10
#define IR3 11

#define SXY0 12
#define SXY1 13
#define SXY2 14
#define SXYP 15

#define SZ0 16
#define SZ1 17
#define SZ2 18
#define SZ3 19

#define RGB0 20
#define RGB1 21
#define RGB2 22

#define MAC0 24
#define MAC1 25
#define MAC2 26
#define MAC3 27

#define IRGB 28
#define ORGB 29

#define LZCS 30
#define LZCR 31

#define RT11RT12 32
#define RT13RT21 33
#define RT22RT23 34
#define RT31RT32 35
#define RT33 36

#define TRX 37
#define TRY 38
#define TRZ 39

#define L11L12 40
#define L13L21 41
#define L22L23 42
#define L31L32 43
#define L33 44

#define RBK 45
#define GBK 46
#define BBK 47

#define LR1LR2 48
#define LR3LG1 49
#define LG2LG3 50
#define LB1LB2 51
#define LB3 52

#define RFC 53
#define GFC 54
#define BFC 55

#define OFX 56
#define OFY 57

#define H 58

#define DQA 59
#define DQB 60

#define ZSF3 61
#define ZSF4 62

#define FLAG 63

#define IR1_FLAG (1 << 24)
#define IR2_FLAG (1 << 23)
#define IR3_FLAG (1 << 22)

static u8 unr_table[] = 
{
    0xFF,0xFD,0xFB,0xF9,0xF7,0xF5,0xF3,0xF1,0xEF,0xEE,0xEC,0xEA,0xE8,0xE6,0xE4,0xE3, 
    0xE1,0xDF,0xDD,0xDC,0xDA,0xD8,0xD6,0xD5,0xD3,0xD1,0xD0,0xCE,0xCD,0xCB,0xC9,0xC8,
    0xC6,0xC5,0xC3,0xC1,0xC0,0xBE,0xBD,0xBB,0xBA,0xB8,0xB7,0xB5,0xB4,0xB2,0xB1,0xB0, 
    0xAE,0xAD,0xAB,0xAA,0xA9,0xA7,0xA6,0xA4,0xA3,0xA2,0xA0,0x9F,0x9E,0x9C,0x9B,0x9A, 
    0x99,0x97,0x96,0x95,0x94,0x92,0x91,0x90,0x8F,0x8D,0x8C,0x8B,0x8A,0x89,0x87,0x86, 
    0x85,0x84,0x83,0x82,0x81,0x7F,0x7E,0x7D,0x7C,0x7B,0x7A,0x79,0x78,0x77,0x75,0x74,
    0x73,0x72,0x71,0x70,0x6F,0x6E,0x6D,0x6C,0x6B,0x6A,0x69,0x68,0x67,0x66,0x65,0x64, 
    0x63,0x62,0x61,0x60,0x5F,0x5E,0x5D,0x5D,0x5C,0x5B,0x5A,0x59,0x58,0x57,0x56,0x55, 
    0x54,0x53,0x53,0x52,0x51,0x50,0x4F,0x4E,0x4D,0x4D,0x4C,0x4B,0x4A,0x49,0x48,0x48, 
    0x47,0x46,0x45,0x44,0x43,0x43,0x42,0x41,0x40,0x3F,0x3F,0x3E,0x3D,0x3C,0x3C,0x3B,
    0x3A,0x39,0x39,0x38,0x37,0x36,0x36,0x35,0x34,0x33,0x33,0x32,0x31,0x31,0x30,0x2F, 
    0x2E,0x2E,0x2D,0x2C,0x2C,0x2B,0x2A,0x2A,0x29,0x28,0x28,0x27,0x26,0x26,0x25,0x24, 
    0x24,0x23,0x22,0x22,0x21,0x20,0x20,0x1F,0x1E,0x1E,0x1D,0x1D,0x1C,0x1B,0x1B,0x1A, 
    0x19,0x19,0x18,0x18,0x17,0x16,0x16,0x15,0x15,0x14,0x14,0x13,0x12,0x12,0x11,0x11,
    0x10,0x0F,0x0F,0x0E,0x0E,0x0D,0x0D,0x0C,0x0C,0x0B,0x0A,0x0A,0x09,0x09,0x08,0x08, 
    0x07,0x07,0x06,0x06,0x05,0x05,0x04,0x04,0x03,0x03,0x02,0x02,0x01,0x01,0x00,0x00, 
    0x00
};

enum gte_op
{
    RTPS = 0x1,
    NCLIP = 0x6,
    OP = 0xC,
    DPCS = 0x10,
    INTPL = 0x11,
    MVMVA = 0x12,
    NCDS = 0x13,
    CDP = 0x14,
    NCDT = 0x16,
    NCCS = 0x1B,
    CC = 0x1C,
    NCS = 0x1E,
    NCT = 0x20,
    SQR = 0x28,
    DCPL = 0x29,
    DPCT = 0x2A,
    AVSZ3 = 0x2D,
    AVSZ4 = 0x2E,
    RTPT = 0x30,
    GPF = 0x3D,
    GPL = 0x3E,
    NCCT = 0x3F
};

static inline u32 lzcnt32(u32 value)
{
#if defined(__LZCNT__) 
    return _lzcnt_u32(value);
#else
    u32 r = 0;
    u32 c = 31;
    while (!(value & (1 << c))) {
        --c;
        ++r;
    }
    return r;
#endif
}

static inline s64 sign_extend16_64(s16 value) 
{
    return (s64)value;
}

static inline s64 gte_lim_mac(s64 v, u32 reg)
{
    if (v > 0x7ffffffffffll) {
        g_cpu.cop2[FLAG] |= (1 << (55 - reg));
    }
    else if (v < -0x80000000000ll) {
        g_cpu.cop2[FLAG] |= (1 << (52 - reg));
    }
    /* truncate to s44 */
    return (v << 20) >> 20;
}

static inline s64 gte_lim_mac0(s64 v)
{
    if (v > 0x7fffffffll) {
        g_cpu.cop2[FLAG] |= (1 << 16);
    }
    else if (v < -0x80000000ll) {
        g_cpu.cop2[FLAG] |= (1 << 15);
    }
    return v;
}

static inline u32 gte_clamp_c(s32 value, u32 flag)
{
    if (value < 0) {
        g_cpu.cop2[FLAG] |= flag;
        return 0;
    }
    else if (value > 0xff) {
        g_cpu.cop2[FLAG] |= flag;
        return 0xff;
    }
    return value;
}

static inline s32 gte_clamp_ir(s64 value, u32 flag, s8 lm)
{
    s32 min = -0x8000 * !lm;
    if (value < min) {
        g_cpu.cop2[FLAG] |= flag;
        return min;
    }
    else if (value > 0x7fff) {
        g_cpu.cop2[FLAG] |= flag;
        return 0x7fff;
    }
    return value;
}

static inline s64 gte_clamp_ir0(s64 value)
{
    if (value > 0x1000) {
        g_cpu.cop2[FLAG] |= (1 << 12);
        return 0x1000;
    }
    else if (value < 0x0) {
        g_cpu.cop2[FLAG] |= (1 << 12);
        return 0x0;
    }
    return value;
}

static inline void gte_rtps(s8 v, s8 sf, s8 lm)
{
    g_cpu.cop2[MAC1] = gte_lim_mac((s64)((s32)g_cpu.cop2[TRX]) * 0x1000 + 
                        (s64)((s16)g_cpu.cop2[RT11RT12]) * (s64)((s16)g_cpu.cop2[v]) + 
                        (s64)((s16)(g_cpu.cop2[RT11RT12] >> 16)) * (s64)((s16)(g_cpu.cop2[v] >> 16)) +
                        (s64)((s16)g_cpu.cop2[RT13RT21]) * (s64)((s16)(g_cpu.cop2[v + 1])), MAC1) >> sf;
#if 0
    g_cpu.cop2[MAC2] = gte_lim_mac((s64)((s64)g_cpu.cop2[TRY] * 0x1000 + 
                        sign_extend16_32(g_cpu.cop2[RT13RT21] >> 16) * sign_extend16_32(g_cpu.cop2[v]) + 
                        sign_extend16_32(g_cpu.cop2[RT22RT23]) * sign_extend16_32(g_cpu.cop2[v] >> 16) +
                        sign_extend16_32(g_cpu.cop2[RT22RT23] >> 16) * sign_extend16_32(g_cpu.cop2[v + 1])), MAC2) >> (sf * 12);

    g_cpu.cop2[MAC3] = gte_lim_mac((s64)((s64)g_cpu.cop2[TRZ] * 0x1000 + 
                        sign_extend16_32(g_cpu.cop2[RT31RT32]) * sign_extend16_32(g_cpu.cop2[v]) + 
                        sign_extend16_32(g_cpu.cop2[RT31RT32] >> 16) * sign_extend16_32(g_cpu.cop2[v] >> 16) +
                        sign_extend16_32(g_cpu.cop2[RT33]) * sign_extend16_32(g_cpu.cop2[v + 1])), MAC3) >> (sf * 12);
#else
    g_cpu.cop2[MAC2] = gte_lim_mac((s64)((s32)g_cpu.cop2[TRY]) * 0x1000 + 
                        (s64)((s16)(g_cpu.cop2[RT13RT21] >> 16)) * (s64)((s16)g_cpu.cop2[v]) + 
                        (s64)((s16)(g_cpu.cop2[RT22RT23])) * (s64)((s16)(g_cpu.cop2[v] >> 16)) +
                        (s64)((s16)(g_cpu.cop2[RT22RT23] >> 16)) * (s64)((s16)(g_cpu.cop2[v + 1])), MAC2) >> sf;

    s64 mac3 = gte_lim_mac((s64)((s32)g_cpu.cop2[TRZ]) * 0x1000 + 
                        (s64)((s16)g_cpu.cop2[RT31RT32]) * (s64)((s16)g_cpu.cop2[v]) + 
                        (s64)((s16)(g_cpu.cop2[RT31RT32] >> 16)) * (s64)((s16)(g_cpu.cop2[v] >> 16)) +
                        (s64)((s16)g_cpu.cop2[RT33]) * (s64)((s16)(g_cpu.cop2[v + 1])), MAC3) >> sf;

    g_cpu.cop2[MAC3] = (u32)mac3;
#endif

    // NOTE: spx says saturation is always -0x8000..0x7fff?
    g_cpu.cop2[IR1] = gte_clamp_ir((s32)g_cpu.cop2[MAC1], (1 << 24), lm);
    g_cpu.cop2[IR2] = gte_clamp_ir((s32)g_cpu.cop2[MAC2], (1 << 23), lm);
#if 1
    // hardware bug: IR3 saturation flag not set if sf=0 unless range exceeds -0x8000..0x7fff
    if (sf) {
        g_cpu.cop2[IR3] = gte_clamp_ir((s32)g_cpu.cop2[MAC3], (1 << 22), lm);
    }
    else {
        s64 mac3_shift = (s32)mac3 >> 12;
        if (mac3_shift < -0x8000ll) {
            g_cpu.cop2[FLAG] |= (1 << 22);
        }
        else if (mac3_shift > 0x7fffll) {
            g_cpu.cop2[IR3] = 0x7fff;
            g_cpu.cop2[FLAG] |= (1 << 22);
        }

        if (lm) {
            if (mac3_shift < 0x0) {
                g_cpu.cop2[IR3] = 0x0;
            }
        }
        else {
            if (mac3_shift < -0x8000) {
                /* flag not set */
                g_cpu.cop2[IR3] = -0x8000;
            }
        }
    }
#else
    g_cpu.cop2[IR3] = gte_clamp_ir(g_cpu.cop2[MAC3], IR3, lm);
    if (!sf) {
        s32 mac3 = (s32)g_cpu.cop2[MAC3] >> 12;
        if (mac3 > 0x7fff || mac3 < -0x8000) {
            g_cpu.cop2[FLAG] |= (1 << 22);
        }
    }
#endif

    /* push fifo */
    g_cpu.cop2[SZ0] = g_cpu.cop2[SZ1];
    g_cpu.cop2[SZ1] = g_cpu.cop2[SZ2];
    g_cpu.cop2[SZ2] = g_cpu.cop2[SZ3];
#if 1
    g_cpu.cop2[SZ3] = mac3 >> (12 - sf);
#else
    g_cpu.cop2[SZ3] = mac3 >> 12;
#endif

    if ((s32)g_cpu.cop2[SZ3] > 0xffff) {
        g_cpu.cop2[SZ3] = 0xffff;
        g_cpu.cop2[FLAG] |= (1 << 18);
    }
    else if ((s32)g_cpu.cop2[SZ3] < 0) {
        g_cpu.cop2[SZ3] = 0;
        g_cpu.cop2[FLAG] |= (1 << 18);
    }

    u32 h = (u32)((u16)g_cpu.cop2[H]);
    //s32 h = sign_extend16_32(g_cpu.cop2[H]);
    s32 n;

    if (h < g_cpu.cop2[SZ3] * 2)
    {
        u32 z = lzcnt32((g_cpu.cop2[SZ3] << 16) | 0xffff);
        n = h << z;
        u32 d = g_cpu.cop2[SZ3] << z;
        u32 u = unr_table[(d - 0x7fc0) >> 7] + 0x101;
        d = ((0x2000080 - (d * u)) >> 8);
        d = ((0x0000080 + (d * u)) >> 8);
        u32 div = (((n * d) + 0x8000) >> 16);
        n = MIN(0x1ffff, div);
    }
    else
    {
        /* overflow occurred */
        n = 0x1ffff;
        g_cpu.cop2[FLAG] |= (1 << 17);
    }

    /* push fifo */
    g_cpu.cop2[SXY0] = g_cpu.cop2[SXY1];
    g_cpu.cop2[SXY1] = g_cpu.cop2[SXY2];

    s32 sx2 = gte_lim_mac0((s64)n * sign_extend32_64(g_cpu.cop2[IR1]) + sign_extend32_64(g_cpu.cop2[OFX])) >> 16;
    if (sx2 > 0x3ff) {
        sx2 = 0x3ff;
        g_cpu.cop2[FLAG] |= (1 << 14);
    }
    else if (sx2 < -0x400) {
        g_cpu.cop2[FLAG] |= (1 << 14);
        sx2 = -0x400;
    }

    s32 sy2 = gte_lim_mac0((s64)n * sign_extend32_64(g_cpu.cop2[IR2]) + sign_extend32_64(g_cpu.cop2[OFY])) >> 16;
    if (sy2 > 0x3ff) {
        sy2 = 0x3ff;
        g_cpu.cop2[FLAG] |= (1 << 13);
    }
    else if (sy2 < -0x400) {
        g_cpu.cop2[FLAG] |= (1 << 13);
        sy2 = -0x400;
    }
#if 0
    g_cpu.cop2[SXY2] = g_cpu.cop2[SXYP]; // TODO: ?
    g_cpu.cop2[SXYP] = sx2 | (sy2 << 16);
#else
    g_cpu.cop2[SXY2] = g_cpu.cop2[SXYP] = (sx2 & 0xffff) | (sy2 << 16);
#endif
    s64 mac0 = gte_lim_mac0(((s64)n * (s64)((s16)g_cpu.cop2[DQA])) + sign_extend32_64(g_cpu.cop2[DQB]));
    g_cpu.cop2[MAC0] = (u32)mac0;//gte_lim_mac0((s64)n * (s16)g_cpu.cop2[DQA] + g_cpu.cop2[DQB]);
    //g_cpu.cop2[IR0] = g_cpu.cop2[MAC0] / 0x1000;
    g_cpu.cop2[IR0] = gte_clamp_ir0(mac0 >> 12);
}

static inline void gte_interpolate(s64 c1, s64 c2, s64 c3, u8 sf, u8 lm)
{
    s32 mac1 = gte_lim_mac(((s64)((s32)g_cpu.cop2[RFC]) << 12) - c1, MAC1) >> sf;
    s32 mac2 = gte_lim_mac(((s64)((s32)g_cpu.cop2[GFC]) << 12) - c2, MAC2) >> sf;
    s32 mac3 = gte_lim_mac(((s64)((s32)g_cpu.cop2[BFC]) << 12) - c3, MAC3) >> sf;

    s64 ir0 = (s64)((s16)g_cpu.cop2[IR0]);

    g_cpu.cop2[MAC1] = gte_lim_mac(c1 + (gte_clamp_ir(mac1, 1 << 24, 0) * ir0), MAC1) >> sf;
    g_cpu.cop2[MAC2] = gte_lim_mac(c2 + (gte_clamp_ir(mac2, 1 << 23, 0) * ir0), MAC2) >> sf;
    g_cpu.cop2[MAC3] = gte_lim_mac(c3 + (gte_clamp_ir(mac3, 1 << 22, 0) * ir0), MAC3) >> sf;

    g_cpu.cop2[IR1] = gte_clamp_ir((s32)g_cpu.cop2[MAC1], 1 << 24, lm);
    g_cpu.cop2[IR2] = gte_clamp_ir((s32)g_cpu.cop2[MAC2], 1 << 23, lm);
    g_cpu.cop2[IR3] = gte_clamp_ir((s32)g_cpu.cop2[MAC3], 1 << 22, lm);

    g_cpu.cop2[RGB0] = g_cpu.cop2[RGB1];
    g_cpu.cop2[RGB1] = g_cpu.cop2[RGB2];
    g_cpu.cop2[RGB2] = gte_clamp_c((s32)g_cpu.cop2[MAC1] >> 4, 1 << 21) | gte_clamp_c((s32)g_cpu.cop2[MAC2] >> 4, 1 << 20) << 8 | gte_clamp_c((s32)g_cpu.cop2[MAC3] >> 4, 1 << 19) << 16 | (g_cpu.cop2[RGBC] & 0xff000000);
}

static inline void gte_ncds(u32 v, u8 sf, u8 lm)
{
    s64 vx = sign_extend16_64(g_cpu.cop2[v]);
    s64 vy = sign_extend16_64(g_cpu.cop2[v] >> 16);
    s64 vz = sign_extend16_64(g_cpu.cop2[v + 1]);

    s32 mac1 = gte_lim_mac(sign_extend16_64(g_cpu.cop2[L11L12]) * vx + 
                            sign_extend16_64(g_cpu.cop2[L11L12] >> 16) * vy +
                            sign_extend16_64(g_cpu.cop2[L13L21]) * vz, MAC1) >> sf;

    s32 mac2 = gte_lim_mac(sign_extend16_64(g_cpu.cop2[L13L21] >> 16) * vx + 
                            sign_extend16_64(g_cpu.cop2[L22L23]) * vy +
                            sign_extend16_64(g_cpu.cop2[L22L23] >> 16) * vz, MAC2) >> sf;

    s32 mac3 = gte_lim_mac(sign_extend16_64(g_cpu.cop2[L31L32]) * vx + 
                            sign_extend16_64(g_cpu.cop2[L31L32] >> 16) * vy +
                            sign_extend16_64(g_cpu.cop2[L33]) * vz, MAC3) >> sf;

    s32 ir1 = gte_clamp_ir(mac1, IR1_FLAG, lm);
    s32 ir2 = gte_clamp_ir(mac2, IR2_FLAG, lm);
    s32 ir3 = gte_clamp_ir(mac3, IR3_FLAG, lm);

    mac1 = (gte_lim_mac(gte_lim_mac(gte_lim_mac(((s64)((s32)g_cpu.cop2[RBK]) << 12) + sign_extend16_64(g_cpu.cop2[LR1LR2]) * ir1, MAC1) +
            sign_extend16_64(g_cpu.cop2[LR1LR2] >> 16) * ir2, MAC1) +
            sign_extend16_64(g_cpu.cop2[LR3LG1]) * ir3, MAC1)) >> sf;

    mac2 = (gte_lim_mac(gte_lim_mac(gte_lim_mac(((s64)((s32)g_cpu.cop2[GBK]) << 12) + sign_extend16_64(g_cpu.cop2[LR3LG1] >> 16) * ir1, MAC2) +
            sign_extend16_64(g_cpu.cop2[LG2LG3]) * ir2, MAC2) +
            sign_extend16_64(g_cpu.cop2[LG2LG3] >> 16) * ir3, MAC2)) >> sf;

    mac3 = (gte_lim_mac(gte_lim_mac(gte_lim_mac(((s64)((s32)g_cpu.cop2[BBK]) << 12) + sign_extend16_64(g_cpu.cop2[LB1LB2]) * ir1, MAC3) +
            sign_extend16_64(g_cpu.cop2[LB1LB2] >> 16) * ir2, MAC3) +
            sign_extend16_64(g_cpu.cop2[LB3]) * ir3, MAC3)) >> sf;

    ir1 = gte_clamp_ir(mac1, IR1_FLAG, lm);
    ir2 = gte_clamp_ir(mac2, IR2_FLAG, lm);
    ir3 = gte_clamp_ir(mac3, IR3_FLAG, lm);

    s64 red = (g_cpu.cop2[RGBC] & 0xff) << 4;
    s64 green = ((g_cpu.cop2[RGBC] >> 8) & 0xff) << 4;
    s64 blue = ((g_cpu.cop2[RGBC] >> 16) & 0xff) << 4;

    gte_interpolate(red * ir1, green * ir2, blue * ir3, sf, lm);
}

void gte_command(u32 command)
{
    g_cpu.cop2[FLAG] = 0; /* flags reset on new command */
    u8 op = command & 0x3f;
    switch ((enum gte_op)op)
    {
    case RTPS:
    {
        u8 sf = ((command & (1 << 19)) != 0) * 12;
        u8 lm = (command & (1 << 10)) != 0;
        gte_rtps(VXY0, sf, lm);
        break;
    }
    case NCLIP:
    {
        g_cpu.cop2[MAC0] = gte_lim_mac0((s64)((s16)g_cpu.cop2[SXY0]) * (s64)((s16)(g_cpu.cop2[SXY1] >> 16)) +
                           (s64)((s16)g_cpu.cop2[SXY1]) * (s64)((s16)(g_cpu.cop2[SXY2] >> 16)) +
                           (s64)((s16)g_cpu.cop2[SXY2]) * (s64)((s16)(g_cpu.cop2[SXY0] >> 16)) -
                           (s64)((s16)g_cpu.cop2[SXY0]) * (s64)((s16)(g_cpu.cop2[SXY2] >> 16)) -
                           (s64)((s16)g_cpu.cop2[SXY1]) * (s64)((s16)(g_cpu.cop2[SXY0] >> 16)) -
                           (s64)((s16)g_cpu.cop2[SXY2]) * (s64)((s16)(g_cpu.cop2[SXY1] >> 16)));
        break;
    }
    case OP:
    {
        u8 sf = ((command & (1 << 19)) != 0) * 12;
        u8 lm = (command & (1 << 10)) != 0;
        g_cpu.cop2[MAC1] = gte_lim_mac((s64)((s16)g_cpu.cop2[IR3]) * (s64)((s16)g_cpu.cop2[RT22RT23]) - (s64)((s16)g_cpu.cop2[IR2]) * (s64)((s16)g_cpu.cop2[RT33]), MAC1) >> sf;
        g_cpu.cop2[MAC2] = gte_lim_mac((s64)((s16)g_cpu.cop2[IR1]) * (s64)((s16)g_cpu.cop2[RT33]) - (s64)((s16)g_cpu.cop2[IR3]) * (s64)((s16)g_cpu.cop2[RT11RT12]), MAC2) >> sf;
        g_cpu.cop2[MAC3] = gte_lim_mac((s64)((s16)g_cpu.cop2[IR2]) * (s64)((s16)g_cpu.cop2[RT11RT12]) - (s64)((s16)g_cpu.cop2[IR1]) * (s64)((s16)g_cpu.cop2[RT22RT23]), MAC3) >> sf;

        g_cpu.cop2[IR1] = gte_clamp_ir((s32)g_cpu.cop2[MAC1], (1 << 24), lm);
        g_cpu.cop2[IR2] = gte_clamp_ir((s32)g_cpu.cop2[MAC2], (1 << 23), lm);
        g_cpu.cop2[IR3] = gte_clamp_ir((s32)g_cpu.cop2[MAC3], (1 << 22), lm);
        break;
    }
    case DPCS:
    {
        u8 sf = ((command & (1 << 19)) != 0) * 12;
        u8 lm = (command & (1 << 10)) != 0;

        s64 red = (g_cpu.cop2[RGBC] & 0xff) << 16;
        s64 green = (g_cpu.cop2[RGBC] & 0xff00) << 8;
        s64 blue = (g_cpu.cop2[RGBC] & 0xff0000);

        // NOTE: changed clamp_ir to take an s64 and cast each arg to s32
        gte_interpolate(red, green, blue, sf, lm);
        break;
    }
    case INTPL:
    {
        u8 sf = ((command & (1 << 19)) != 0) * 12;
        u8 lm = (command & (1 << 10)) != 0;

        s64 red = ((s16)g_cpu.cop2[IR1]) << 12;
        s64 green = ((s16)g_cpu.cop2[IR2]) << 12;
        s64 blue = ((s16)g_cpu.cop2[IR3]) << 12;

        gte_interpolate(red, green, blue, sf, lm);
        break;
    }
    case NCDS:
    {
        u8 sf = ((command & (1 << 19)) != 0) * 12;
        u8 lm = (command & (1 << 10)) != 0;
        gte_ncds(VXY0, sf, lm);
        break;
    }
    case CDP:
    {
        u8 sf = ((command & (1 << 19)) != 0) * 12;
        u8 lm = (command & (1 << 10)) != 0;

        s32 ir1 = sign_extend16_32(g_cpu.cop2[IR1]);
        s32 ir2 = sign_extend16_32(g_cpu.cop2[IR2]);
        s32 ir3 = sign_extend16_32(g_cpu.cop2[IR3]);

        s32 mac1 = (gte_lim_mac(gte_lim_mac(gte_lim_mac(((s64)((s32)g_cpu.cop2[RBK]) << 12) + sign_extend16_64(g_cpu.cop2[LR1LR2]) * ir1, MAC1) +
                    sign_extend16_64(g_cpu.cop2[LR1LR2] >> 16) * ir2, MAC1) +
                    sign_extend16_64(g_cpu.cop2[LR3LG1]) * ir3, MAC1)) >> sf;

        s32 mac2 = (gte_lim_mac(gte_lim_mac(gte_lim_mac(((s64)((s32)g_cpu.cop2[GBK]) << 12) + sign_extend16_64(g_cpu.cop2[LR3LG1] >> 16) * ir1, MAC2) +
                    sign_extend16_64(g_cpu.cop2[LG2LG3]) * ir2, MAC2) +
                    sign_extend16_64(g_cpu.cop2[LG2LG3] >> 16) * ir3, MAC2)) >> sf;

        s32 mac3 = (gte_lim_mac(gte_lim_mac(gte_lim_mac(((s64)((s32)g_cpu.cop2[BBK]) << 12) + sign_extend16_64(g_cpu.cop2[LB1LB2]) * ir1, MAC3) +
                    sign_extend16_64(g_cpu.cop2[LB1LB2] >> 16) * ir2, MAC3) +
                    sign_extend16_64(g_cpu.cop2[LB3]) * ir3, MAC3)) >> sf;

        ir1 = gte_clamp_ir(mac1, IR1_FLAG, lm);
        ir2 = gte_clamp_ir(mac2, IR2_FLAG, lm);
        ir3 = gte_clamp_ir(mac3, IR3_FLAG, lm);

        s64 red = (g_cpu.cop2[RGBC] & 0xff) << 4;
        s64 green = ((g_cpu.cop2[RGBC] >> 8) & 0xff) << 4;
        s64 blue = ((g_cpu.cop2[RGBC] >> 16) & 0xff) << 4;

        gte_interpolate(red * ir1, green * ir2, blue * ir3, sf, lm);      
        break;
    }
    case NCDT:
    {
        u8 sf = ((command & (1 << 19)) != 0) * 12;
        u8 lm = (command & (1 << 10)) != 0;
        gte_ncds(VXY0, sf, lm);
        gte_ncds(VXY1, sf, lm);
        gte_ncds(VXY2, sf, lm);
        break;
    }
    case AVSZ3:
    {
        g_cpu.cop2[MAC0] = sign_extend16_32(g_cpu.cop2[ZSF3]) * (g_cpu.cop2[SZ1] + g_cpu.cop2[SZ2] + g_cpu.cop2[SZ3]);
        g_cpu.cop2[OTZ] = g_cpu.cop2[MAC0] / 0x1000;
        break;
    }
    case RTPT:
    {
        u8 sf = ((command & (1 << 19)) != 0) * 12;
        u8 lm = (command & (1 << 10)) != 0;
        gte_rtps(VXY0, sf, lm);
        gte_rtps(VXY1, sf, lm);
        gte_rtps(VXY2, sf, lm);
        break;
    }
    case MVMVA:
    {
        const u8 tx_lookup[] = {TRX, RBK, RFC, 64};
        //const u8 vx_lookup[] = {VXY0, VXY1, VXY2, IR1};
        //const u8 mx_lookup[] = {RT11RT12, L11L12, LR1LR2};

        u8 sf = ((command & (1 << 19)) != 0) * 12;
        u8 lm = (command & (1 << 10)) != 0;

        u8 tx_index = (command >> 13) & 0x3;
        //SY_ASSERT(tx_index < 3);
        u8 tx = tx_lookup[tx_index];
        
        s16 vxi[3];
        u8 vx_index = (command >> 15) & 0x3;
        if (vx_index == 3) {
            vxi[0] = (s16)g_cpu.cop2[IR1];
            vxi[1] = (s16)g_cpu.cop2[IR2];
            vxi[2] = (s16)g_cpu.cop2[IR3];
        }
        else {
            //vxi = (s16 *)&g_cpu.cop2[vx_index << 1];
            vxi[0] = (s16)g_cpu.cop2[vx_index << 1];
            vxi[1] = (s16)(g_cpu.cop2[vx_index << 1] >> 16);
            vxi[2] = (s16)g_cpu.cop2[(vx_index << 1) + 1];
        }
        //u8 vx = vx_lookup[vx_index];
        s16 mxi[9];
        
        u8 mx_index = (command >> 17) & 0x3;
        if (mx_index == 3) {
            /* garbage matrix values */
            u16 r = (g_cpu.cop2[RGBC] & 0xff) << 4;
            mxi[0] = -r;
            mxi[1] = r;
            mxi[2] = (s16)g_cpu.cop2[IR0];
            mxi[3] = mxi[4] = mxi[5] = (s16)g_cpu.cop2[RT13RT21];
            mxi[6] = mxi[7] = mxi[8] = (s16)g_cpu.cop2[RT22RT23];
        } 
        else {
            mx_index = 32 + (8 * mx_index);
            mxi[0] = (s16)g_cpu.cop2[mx_index];
            mxi[1] = (s16)(g_cpu.cop2[mx_index] >> 16);
            mxi[2] = (s16)g_cpu.cop2[mx_index + 1];
            mxi[3] = (s16)(g_cpu.cop2[mx_index + 1] >> 16);
            mxi[4] = (s16)g_cpu.cop2[mx_index + 2];
            mxi[5] = (s16)(g_cpu.cop2[mx_index + 2] >> 16);
            mxi[6] = (s16)g_cpu.cop2[mx_index + 3];
            mxi[7] = (s16)(g_cpu.cop2[mx_index + 3] >> 16);
            mxi[8] = (s16)g_cpu.cop2[mx_index + 4];
        }
        //SY_ASSERT(mx_index < 3);
        //u8 mx = mx_lookup[(command >> 17) & 0x3];
        
        if (tx_index == 2) 
        {
            g_cpu.cop2[MAC1] = gte_lim_mac(sign_extend16_64(mxi[1]) * sign_extend16_64(vxi[1]) + 
                                            sign_extend16_64(mxi[2]) * sign_extend16_64(vxi[2]), MAC1) >> sf;

            g_cpu.cop2[MAC2] = gte_lim_mac(sign_extend16_64(mxi[4]) * sign_extend16_64(vxi[1]) + 
                                            sign_extend16_64(mxi[5]) * sign_extend16_64(vxi[2]), MAC2) >> sf;
                                    
            g_cpu.cop2[MAC3] = gte_lim_mac(sign_extend16_64(mxi[7]) * sign_extend16_64(vxi[1]) + 
                                            sign_extend16_64(mxi[8]) * sign_extend16_64(vxi[2]), MAC3) >> sf;

            gte_clamp_ir(gte_lim_mac(((s64)((s32)g_cpu.cop2[tx]) << 12) + sign_extend16_64(mxi[0]) * sign_extend16_64(vxi[0]), MAC1) >> sf, IR1_FLAG, 0);
            gte_clamp_ir(gte_lim_mac(((s64)((s32)g_cpu.cop2[tx + 1]) << 12) + sign_extend16_64(mxi[3]) * sign_extend16_64(vxi[0]), MAC2) >> sf, IR2_FLAG, 0);
            gte_clamp_ir(gte_lim_mac(((s64)((s32)g_cpu.cop2[tx + 2]) << 12) + sign_extend16_64(mxi[6]) * sign_extend16_64(vxi[0]), MAC3) >> sf, IR3_FLAG, 0);
        }
        else 
        {
            g_cpu.cop2[MAC1] = gte_lim_mac(((s64)((s32)g_cpu.cop2[tx]) << 12) + 
                                sign_extend16_64(mxi[0]) * sign_extend16_64(vxi[0]) + 
                                sign_extend16_64(mxi[1]) * sign_extend16_64(vxi[1]) + 
                                sign_extend16_64(mxi[2]) * sign_extend16_64(vxi[2]), MAC1) >> sf;

            g_cpu.cop2[MAC2] = gte_lim_mac(((s64)((s32)g_cpu.cop2[tx + 1]) << 12) + 
                                sign_extend16_64(mxi[3]) * sign_extend16_64(vxi[0]) + 
                                sign_extend16_64(mxi[4]) * sign_extend16_64(vxi[1]) + 
                                sign_extend16_64(mxi[5]) * sign_extend16_64(vxi[2]), MAC2) >> sf;

            g_cpu.cop2[MAC3] = gte_lim_mac(((s64)((s32)g_cpu.cop2[tx + 2]) << 12) + 
                                sign_extend16_64(mxi[6]) * sign_extend16_64(vxi[0]) + 
                                sign_extend16_64(mxi[7]) * sign_extend16_64(vxi[1]) + 
                                sign_extend16_64(mxi[8]) * sign_extend16_64(vxi[2]), MAC3) >> sf;
        }

        g_cpu.cop2[IR1] = gte_clamp_ir((s32)g_cpu.cop2[MAC1], IR1_FLAG, lm);
        g_cpu.cop2[IR2] = gte_clamp_ir((s32)g_cpu.cop2[MAC2], IR2_FLAG, lm);
        g_cpu.cop2[IR3] = gte_clamp_ir((s32)g_cpu.cop2[MAC3], IR3_FLAG, lm);
        break;
    }
    default:
        debug_log("Unhandled GTE command: %02xh\n", op);
        break;
    }
}

u32 gte_read(u32 reg)
{
    switch (reg)
    {
    case VZ0:
    case VZ1:
    case VZ2:
    case IR0:
    case IR1:
    case IR2:
    case IR3:
    case RT33:
    case L33:
    case LB3:
    case H:
    case DQA:
    case ZSF3:
    case ZSF4:
        return sign_extend16_32(g_cpu.cop2[reg]);
    case OTZ:
    case SZ0:
    case SZ1:
    case SZ2:
    case SZ3:
        return g_cpu.cop2[reg] & 0xffff;
    case IRGB:
    case ORGB:
        s32 red = (s32)sign_extend16_32(g_cpu.cop2[IR1]) / 0x80;
        if (red < 0x0) {
            red = 0x0;
        }
        else if (red > 0x1f) {
            red = 0x1f;
        }

        s32 green = (s32)sign_extend16_32(g_cpu.cop2[IR2]) / 0x80;
        if (green < 0x0) {
            green = 0x0;
        }
        else if (green > 0x1f) {
            green = 0x1f;
        }

        s32 blue = (s32)sign_extend16_32(g_cpu.cop2[IR3]) / 0x80;
        if (blue < 0x0) {
            blue = 0x0;
        }
        else if (blue > 0x1f) {
            blue = 0x1f;
        }
        //u32 red = (u32)clamp(g_cpu.cop2[IR1] / 0x80, 0x0, 0x1f);
        //u32 green = (u32)clamp(g_cpu.cop2[IR2] / 0x80, 0x0, 0x1f);
        //u32 blue = (u32)clamp(g_cpu.cop2[IR3] / 0x80, 0x0, 0x1f);
        return (red | (green << 5) | (blue << 10)); //g_cpu.cop2[IRGB] & 0x7fff; // ORGB is a readonly mirror of IRGB
    case LZCR:
        u32 v = g_cpu.cop2[LZCS];
        if ((s32)v < 0) {
            v = ~v;
        }
        return lzcnt32(v);
    case FLAG:
        if (g_cpu.cop2[FLAG] & 0x7f800000 || g_cpu.cop2[FLAG] & 0x7e000) {
            g_cpu.cop2[FLAG] |= 0x80000000;
        }
        else {
            g_cpu.cop2[FLAG] &= 0x7fffffff;
        }
        return g_cpu.cop2[FLAG] & 0xfffff000;  
    default:
        return g_cpu.cop2[reg];
    }
}

void gte_write(u32 reg, u32 value)
{
    // TODO: write masks here, don't do them on read
    switch (reg)
    {
    case SXYP:
        g_cpu.cop2[SXY0] = g_cpu.cop2[SXY1];
        g_cpu.cop2[SXY1] = g_cpu.cop2[SXY2];
        g_cpu.cop2[SXY2] = g_cpu.cop2[SXYP] = value;
        break;
    case IRGB:
        g_cpu.cop2[IR1] = (value & 0x1f) * 0x80;
        g_cpu.cop2[IR2] = ((value >> 5) & 0x1f) * 0x80;
        g_cpu.cop2[IR3] = ((value >> 10) & 0x1f) * 0x80;
        g_cpu.cop2[IRGB] = value;
        break;
    default:
        g_cpu.cop2[reg] = value;
        break;
    }
}
