#include "cpu.h"
#include "event.h"
#include "memory.h"
#include "debug.h"
#include "disasm.h"
#include "sy_math.h"

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

#define HPPD 58

#define DQA 59
#define DQB 60

#define ZSF3 61
#define ZSF4 62

#define COP0_SR 12
#define COP0_CAUSE 13
#define COP0_EPC 14

struct cpu_state g_cpu;

static inline void gte_rtps(s8 v, s8 sf);
static inline vec3u gte_mat3_mul_v3(s8 m11m12, s8 v, s8 sf);

static inline reg_tuple set_register(u32 index, u32 value)
{
    reg_tuple load = {.index = index, .value = value};
    return load;
}

static inline void handle_exception(enum exception_code cause)
{
#if 0
    g_cpu.cop0[13] &= ~(0x7c);
    g_cpu.cop0[13] |= ((u32)cause << 2);
#else
    g_cpu.cop0[COP0_CAUSE] = (u32)cause << 2;
#endif
#if 1
    if (cause == EXCEPTION_CODE_INTERRUPT)
    {
        g_cpu.cop0[COP0_EPC] = g_cpu.pc;
    }
    else
    {
        g_cpu.cop0[COP0_EPC] = g_cpu.current_pc;
    }
#endif
    //g_cpu.cop0[COP0_EPC] = g_cpu.current_pc;
    if (g_cpu.in_branch_delay)
    {
        g_cpu.cop0[COP0_CAUSE] |= (1 << 31);
        g_cpu.cop0[COP0_EPC] -= 4; //= g_cpu.current_pc - 4;
    }

#if 0
    else
    {
        g_cpu.cop0[14] = g_cpu.current_pc;
    }
#endif

    /* push zeros to IE stack */
    u32 stack = (g_cpu.cop0[COP0_SR] << 2) & 0x3f;
    g_cpu.cop0[COP0_SR] &= ~0x3f;
    g_cpu.cop0[COP0_SR] |= stack;

    g_cpu.pc = (g_cpu.cop0[COP0_SR] & 0x400000) ? 0xbfc00180 : 0x80000080;
    g_cpu.next_pc = g_cpu.pc + 4;
    g_cpu.in_branch_delay = 0;
}

static inline void handle_interrupts(void)
{
    if (g_cpu.i_stat & g_cpu.i_mask)
    {
        g_cpu.cop0[COP0_CAUSE] |= 0x400;
    }
    else
    {
        g_cpu.cop0[COP0_CAUSE] &= ~0x400;
    }

    u8 ip = (g_cpu.cop0[COP0_CAUSE] >> 8) & 0xff;
    u8 im = (g_cpu.cop0[COP0_SR] >> 8) & 0xff;
    if ((g_cpu.cop0[COP0_SR] & 0x1) && (ip & im))
    {
        handle_exception(EXCEPTION_CODE_INTERRUPT);
    }
}

static inline void log_tty(void)
{
    if (g_cpu.pc == 0xb0)
    {
        switch (g_cpu.registers[9])
        {
        case 0x35:
            if (g_cpu.registers[4] != 1)
                break;
            char* str;
            u32 addr = g_cpu.registers[5] & 0x1fffffff;
            if (addr >= 0x1fc00000 && addr < 0x1fc80000)
            {
                str = (char *)(g_bios + (addr - 0x1fc00000));
            }
            else
            {
                SY_ASSERT(0);
            }
            u32 size = g_cpu.registers[6];
            while (size--)
                debug_putchar(*str++);
            break;
        case 0x3d:
            debug_putchar(g_cpu.registers[4]);
            break;
        }
    }
}

void cpu_init(void)
{
    g_cpu.pc = 0xbfc00000;
    g_cpu.next_pc = 0xbfc00004;
    g_cpu.cop0[15] = 0x2; // PRID
}

static u32 fetch_instruction(u32 pc)
{
    u32 addr = pc & 0x1fffffff;
    u8 region = pc >> 29;
    switch (region)
    {
    case 0x0: // KUSEG
    case 0x4: // KSEG0
    {
        if (addr < 0x800000)
        {
            return *(u32 *)(g_ram + (addr & 0x1fffff));
        }
        else if (addr >= 0x1f800000 && addr < 0x1f800400)
        {
            return *(u32 *)(g_scratch + (addr & 0x3ff));
        }
        else if (addr >= 0x1fc00000 && addr < 0x1fc80000)
        {
            return *(u32 *)(g_bios + (addr & 0x7ffff));
        }
        else
        {
            return 0;
        }
        break;
    }
    case 0x5: // KSEG1
    {
        return *(u32 *)mem_read(addr);
    }
    INVALID_CASE;
    }
    return 0;
}

u64 execute_instruction(u64 min_cycles)
{
    b32 branched = SY_FALSE;
    reg_tuple new_load = {0};
    reg_tuple write = {0};
    u64 target_cycles = g_cycles_elapsed + min_cycles;
    //u64 i;
//#define LOG_DISASM
#ifdef LOG_DISASM
    char buffer[64];
    memset(buffer, 0, sizeof(buffer));
#endif
    //for (i = 0; i < min_cycles; i += psx->pending_cycles)
    while (g_cycles_elapsed <= target_cycles)
    {
        ++g_cycles_elapsed;
        //psx->pending_cycles = 1;
        log_tty();
#if 0
        // exe sideloading
        if (g_cpu.pc == 0x80030000)
        {
            g_debug.show_disasm = 1;
            u8* fp = g_debug.loaded_exe;
            u32 dst = U32FromPtr(fp + 0x18);
            u32 size = U32FromPtr(fp + 0x1c);
            memcpy((g_ram + (dst & 0x1fffffff)), (fp + 0x800), size);
            g_cpu.pc = U32FromPtr(fp + 0x10);
            g_cpu.registers[28] = U32FromPtr(fp + 0x14);
            if (U32FromPtr(fp + 0x30) != 0)
            {
                g_cpu.registers[29] = U32FromPtr(fp + 0x30) + U32FromPtr(fp + 0x34);
                g_cpu.registers[30] = U32FromPtr(fp + 0x30) + U32FromPtr(fp + 0x34);
            }
            g_cpu.next_pc = g_cpu.pc + 4;
        }
#endif
        if (g_cpu.pc & 0x3) // NOTE: this seems to fix amidog exception tests but im not sure its needed
        {
            g_cpu.cop0[8] = g_cpu.pc;
            handle_exception(EXCEPTION_CODE_ADEL);
        }

        instruction ins = {.value = fetch_instruction(g_cpu.pc)};
        g_cpu.current_pc = g_cpu.pc;

        g_cpu.pc = g_cpu.next_pc;
        g_cpu.next_pc = g_cpu.pc + 4;
        
        u32 immediate = ins.value & 0xffff;
        u32 target = ins.value & 0x3ffffff;

        switch ((enum primary_op)ins.op)
        {
        case BCOND:
        {
            // TODO: fix
            u8 type = ins.rt & 0x1; // if bit 16 is set, it is bgez, otherwise it is bltz
            u8 link = (ins.rt & 0x1e) == 0x10; // top 4 bits must be set to 0x10
            b8 branch = ((s32)g_cpu.registers[ins.rs] < 0) ^ type;

            if (link)
                write = set_register(31, g_cpu.next_pc);

            if (branch)
                g_cpu.next_pc = g_cpu.pc + (sign_extend16_32(immediate) << 2);
            branched = SY_TRUE;
            break;
        }
        case ANDI:
        {
            write = set_register(ins.rt, g_cpu.registers[ins.rs] & immediate);
            break;
        }
        case ADDI:
        {
            s64 add = (s64)((s32)g_cpu.registers[ins.rs]) + (s64)((s32)sign_extend16_32(immediate));
            s32 result = (s32)add;
            if (result != add)
            {
                handle_exception(EXCEPTION_CODE_OVERFLOW);
                break;
            }
            write = set_register(ins.rt, (u32)result);
            break;
        }
        case ADDIU:
        {
            write = set_register(ins.rt, g_cpu.registers[ins.rs] + sign_extend16_32(immediate));
            break;
        }
        case SLTI:
        {
            write = set_register(ins.rt, (s32)g_cpu.registers[ins.rs] < (s32)sign_extend16_32(immediate));
            break;
        }
        case SLTIU:
        {
            write = set_register(ins.rt, g_cpu.registers[ins.rs] < sign_extend16_32(immediate));
            break;
        }
        case ORI:
        {
            write = set_register(ins.rt, g_cpu.registers[ins.rs] | immediate);
            break;
        }
        case XORI:
        {
            write = set_register(ins.rt, g_cpu.registers[ins.rs] ^ immediate);
            break;
        }
        case LUI:
        {
            write = set_register(ins.rt, immediate << 16);
            break;
        }
        case LB:
        {
            u32 vaddr = g_cpu.registers[ins.rs] + sign_extend16_32(immediate);
            if (g_cpu.cop0[12] & 0x10000)
            {
                //printf("Unhandled load to data cache\n");
                break;
            }
            s8 value = load8(vaddr);
            new_load.index = ins.rt;
            new_load.value = (u32)(value);
            break;
        }
        case LH:
        {
            u32 vaddr = g_cpu.registers[ins.rs] + sign_extend16_32(immediate);
            if (vaddr & 0x1)
            {
                g_cpu.cop0[8] = vaddr;
                handle_exception(EXCEPTION_CODE_ADEL);
                break;
            }
            if (g_cpu.cop0[12] & 0x10000)
            {
                break;
            }
            s16 value = (s16)load16(vaddr);
            new_load.index = ins.rt;
            new_load.value = (u32)value;
            break;
        };
        case LWL:
        {
            u32 vaddr = g_cpu.registers[ins.rs] + sign_extend16_32(immediate);
            if (g_cpu.cop0[12] & 0x10000)
                break;
            u32 aligned_addr = vaddr & ~0x3;
            u32 value = load32(aligned_addr);
            u32 merge = g_cpu.load_delay.index ? g_cpu.load_delay.value : g_cpu.registers[ins.rt];
            new_load.index = ins.rt;
            switch (vaddr & 0x3)
            {
            case 0:
                new_load.value = (merge & 0xffffff) | (value << 24);
                break;
            case 1:
                new_load.value = (merge & 0xffff) | (value << 16);
                break;
            case 2:
                new_load.value = (merge & 0xff) | (value << 8);
                break;
            case 3:
                new_load.value = value;
                break;
            }
            break;
        }
        case LWR:
        {
            u32 vaddr = g_cpu.registers[ins.rs] + sign_extend16_32(immediate);
            if (g_cpu.cop0[12] & 0x10000)
                break;
            u32 aligned_addr = vaddr & ~0x3;
            u32 value = load32(aligned_addr);
            u32 merge = g_cpu.load_delay.index ? g_cpu.load_delay.value : g_cpu.registers[ins.rt];
            new_load.index = ins.rt;
            switch (vaddr & 0x3)
            {
            case 0:
                new_load.value = value;
                break;
            case 1:
                new_load.value = (merge & 0xff000000) | (value >> 8);
                break;
            case 2:
                new_load.value = (merge & 0xffff0000) | (value >> 16);
                break;
            case 3:
                new_load.value = (merge & 0xffffff00) | (value >> 24);
                break;
            }
            break;
        }
        case LW:
        {
            u32 vaddr = g_cpu.registers[ins.rs] + sign_extend16_32(immediate);
            if (vaddr & 0x3)
            {
                g_cpu.cop0[8] = vaddr;
                handle_exception(EXCEPTION_CODE_ADEL);
                break;
            }
            if (g_cpu.cop0[12] & 0x10000)
            {
                //printf("Unhandled load to data cache\n");
                break;
            }
            new_load.index = ins.rt;
            new_load.value = load32(vaddr);
            break;
        }
        case LBU:
        {
            u32 vaddr = g_cpu.registers[ins.rs] + sign_extend16_32(immediate);
            if (g_cpu.cop0[12] & 0x10000)
            {
                //printf("Unhandled load to data cache\n");
                break;
            }
            new_load.index = ins.rt;
            new_load.value = load8(vaddr);
            break;
        }
        case LHU:
        {
            u32 vaddr = g_cpu.registers[ins.rs] + sign_extend16_32(immediate);
            if (vaddr & 0x1)
            {
                g_cpu.cop0[8] = vaddr;
                handle_exception(EXCEPTION_CODE_ADEL);
                break;
            }
            if (g_cpu.cop0[12] & 0x10000)
            {
                break;
            }
            new_load.index = ins.rt;
            new_load.value = load16(vaddr);
            break;
        }
        case SB:
        {
            u32 vaddr = g_cpu.registers[ins.rs] + sign_extend16_32(immediate);
            if (g_cpu.cop0[12] & 0x10000)
            {
                //printf("Unhandled store to data cache\n");
                break;
            }
            store8(vaddr, g_cpu.registers[ins.rt]);
            break;
        }
        case SH:
        {
            u32 vaddr = g_cpu.registers[ins.rs] + sign_extend16_32(immediate);
            if (vaddr & 0x1)
            {
                g_cpu.cop0[8] = vaddr;
                handle_exception(EXCEPTION_CODE_ADES);
                break;
            }
            if (g_cpu.cop0[12] & 0x10000)
            {
                //printf("Unhandled store to data cache\n");
                break;
            }
            store16(vaddr, g_cpu.registers[ins.rt]);
            break;
        }
        case SWL:
        {
            u32 vaddr = g_cpu.registers[ins.rs] + sign_extend16_32(immediate);
            if (g_cpu.cop0[12] & 0x10000)
                break;
            u32 aligned = vaddr & ~0x3;
            u32 value = load32(aligned);
            u32 store;
            switch (vaddr & 0x3)
            {
            case 0:
                store = (value & 0xffffff00) | (g_cpu.registers[ins.rt] >> 24);
                break;
            case 1:
                store = (value & 0xffff0000) | (g_cpu.registers[ins.rt] >> 16);
                break;
            case 2:
                store = (value & 0xff000000) | (g_cpu.registers[ins.rt] >> 8);
                break;
            case 3:
                store = g_cpu.registers[ins.rt];
                break;
            }
            store32(aligned, store);
            break;
        }
        case SWR:
        {
            u32 vaddr = g_cpu.registers[ins.rs] + sign_extend16_32(immediate);
            if (g_cpu.cop0[12] & 0x10000)
                break;
            u32 aligned = vaddr & ~0x3;
            u32 value = load32(aligned);
            u32 write;
            switch (vaddr & 0x3)
            {
            case 0:
                write = g_cpu.registers[ins.rt];
                break;
            case 1:
                write = (value & 0xff) | (g_cpu.registers[ins.rt] << 8);
                break;
            case 2:
                write = (value & 0xffff) | (g_cpu.registers[ins.rt] << 16);
                break;
            case 3:
                write = (value & 0xffffff) | (g_cpu.registers[ins.rt] << 24);
                break;
            }
            store32(aligned, write);
            break;
        }
        case SW:
        {
            u32 vaddr = g_cpu.registers[ins.rs] + sign_extend16_32(immediate);
            if (vaddr & 0x3)
            {
                g_cpu.cop0[8] = vaddr;
                handle_exception(EXCEPTION_CODE_ADES);
                break;
            }
            if (g_cpu.cop0[12] & 0x10000)
            {
                //printf("Unhandled store to data cache\n");
                break;
            }
            store32(vaddr, g_cpu.registers[ins.rt]);
            break;
        }
        case BEQ:
        {
            if (g_cpu.registers[ins.rs] == g_cpu.registers[ins.rt])
            {
                g_cpu.next_pc = g_cpu.pc + (sign_extend16_32(immediate) << 2);
            }
            branched = SY_TRUE;
            break;
        }
        case BNE:
        {
            if (g_cpu.registers[ins.rs] != g_cpu.registers[ins.rt])
            {
                g_cpu.next_pc = g_cpu.pc + (sign_extend16_32(immediate) << 2);
            }
            branched = SY_TRUE;
            break;
        }
        case BLEZ:
        {
            if ((s32)g_cpu.registers[ins.rs] <= 0)
            {
                g_cpu.next_pc = g_cpu.pc + (sign_extend16_32(immediate) << 2);
            }
            branched = SY_TRUE;
            break;
        }
        case BGTZ:
        {
            if ((s32)g_cpu.registers[ins.rs] > 0)
            {               
                g_cpu.next_pc = g_cpu.pc + (sign_extend16_32(immediate) << 2);
            }
            branched = SY_TRUE;
            break;
        }
        case J:
        {
            g_cpu.next_pc = (g_cpu.pc & 0xf0000000) | (target << 2);
            branched = SY_TRUE;
            break;
        }
        case JAL:
        {
            write = set_register(31, g_cpu.next_pc);
            g_cpu.next_pc = (g_cpu.pc & 0xf0000000) | (target << 2);
            branched = SY_TRUE;
            break;
        }
        case COP0:
            switch ((enum coprocessor_op)ins.rs)
            {
            case MFC:
                new_load.index = ins.rt;
                new_load.value = g_cpu.cop0[ins.rd];
                break;
            case MTC:
                g_cpu.cop0[ins.rd] = g_cpu.registers[ins.rt];
                break;
            case RFE:
            {
                SY_ASSERT(ins.secondary == 0x10);
                u32 stack = (g_cpu.cop0[12] >> 2) & 0xf;
                g_cpu.cop0[12] &= ~0xf;
                g_cpu.cop0[12] |= stack;
            }   break;
            default:
                debug_log("WARNING: Unknown coprocessor op: %x\n", ins.rs); // NOTE: RI exception
                break;
            }
            break;
        case COP2:
            if (ins.value & (1 << 25)) {
                // GTE command
                // g_cpu.cop2[63] = 0 /* flags reset on new command */

                u32 command = ins.value & 0x1ffffff;
                u8 op = command & 0x3f;
                switch ((enum gte_command)op)
                {
                case RTPS:
                {
                    u8 sf = command & (1 << 19);
                    gte_rtps(VXY0, sf);
                    break;
                }
                case NCLIP:
                {
                    g_cpu.cop2[MAC0] = (s16)g_cpu.cop2[SXY0] * sign_extend16_32(g_cpu.cop2[SXY1] >> 16) +
                                       (s16)g_cpu.cop2[SXY1] * sign_extend16_32(g_cpu.cop2[SXY2] >> 16) +
                                       (s16)g_cpu.cop2[SXY2] * sign_extend16_32(g_cpu.cop2[SXY0] >> 16) -
                                       (s16)g_cpu.cop2[SXY0] * sign_extend16_32(g_cpu.cop2[SXY2] >> 16) -
                                       (s16)g_cpu.cop2[SXY1] * sign_extend16_32(g_cpu.cop2[SXY0] >> 16) -
                                       (s16)g_cpu.cop2[SXY2] * sign_extend16_32(g_cpu.cop2[SXY1] >> 16);
                    break;
                }
                case NCDS:
                {
                    u8 sf = command & (1 << 19);
                    u32 v0x = sign_extend16_32(g_cpu.cop2[VXY0]);
                    u32 v0y = sign_extend16_32(g_cpu.cop2[VXY0] >> 16);
                    u32 v0z = sign_extend16_32(g_cpu.cop2[VZ0]);

                    u32 c1 = (s32)(sign_extend16_32(g_cpu.cop2[L11L12]) * v0x +
                                        sign_extend16_32(g_cpu.cop2[L11L12] >> 16) * v0y +
                                            sign_extend16_32(g_cpu.cop2[L13L21]) * v0z) >> (sf * 12);

                    u32 c2 = (s32)(sign_extend16_32(g_cpu.cop2[L13L21] >> 16) * v0x +
                                        sign_extend16_32(g_cpu.cop2[L22L23]) * v0y +
                                            sign_extend16_32(g_cpu.cop2[L22L23] >> 16) * v0z) >> (sf * 12);

                    u32 c3 = (s32)(sign_extend16_32(g_cpu.cop2[L31L32]) * v0x +
                                        sign_extend16_32(g_cpu.cop2[L31L32] >> 16) * v0y +
                                            sign_extend16_32(g_cpu.cop2[L33]) * v0z) >> (sf * 12);

                    u32 rbk = g_cpu.cop2[RBK] * 0x1000;
                    u32 gbk = g_cpu.cop2[GBK] * 0x1000;
                    u32 bbk = g_cpu.cop2[BBK] * 0x1000;

                    u32 n1 = (s32)(rbk + sign_extend16_32(g_cpu.cop2[LR1LR2]) * c1 +
                                        sign_extend16_32(g_cpu.cop2[LR1LR2] >> 16) * c2 +
                                            sign_extend16_32(g_cpu.cop2[LR3LG1]) * c3) >> (sf * 12);

                    u32 n2 = (s32)(gbk + sign_extend16_32(g_cpu.cop2[LR3LG1] >> 16) * c1 +
                                        sign_extend16_32(g_cpu.cop2[LG2LG3]) * c2 +
                                            sign_extend16_32(g_cpu.cop2[LG2LG3] >> 16) * c3) >> (sf * 12);

                    u32 n3 = (s32)(bbk + sign_extend16_32(g_cpu.cop2[LB1LB2]) * c1 +
                                        sign_extend16_32(g_cpu.cop2[LB1LB2] >> 16) * c2 +
                                            sign_extend16_32(g_cpu.cop2[LB3]) * c3) >> (sf * 12);

                    u32 rgbc = g_cpu.cop2[RGBC];
                    u32 m1 = ((rgbc & 0xff) * n1) << 4;
                    u32 m2 = (((rgbc >> 8) & 0xff) * n2) << 4;
                    u32 m3 = (((rgbc >> 16) & 0xff) * n3) << 4;

                    m1 = m1 + (g_cpu.cop2[RFC] - m1) * g_cpu.cop2[IR0];
                    m2 = m2 + (g_cpu.cop2[GFC] - m2) * g_cpu.cop2[IR0];
                    m3 = m3 + (g_cpu.cop2[BFC] - m3) * g_cpu.cop2[IR0];

                    m1 = (s32)m1 >> (sf * 12);
                    m2 = (s32)m2 >> (sf * 12);
                    m3 = (s32)m3 >> (sf * 12);

                    g_cpu.cop2[RGB0] = g_cpu.cop2[RGB1];
                    g_cpu.cop2[RGB1] = g_cpu.cop2[RGB2];
                    g_cpu.cop2[RGB2] = ((m1 / 16) & 0xff) | ((m2 / 16) & 0xff) << 8 | ((m3 / 16) & 0xff) << 16 | (g_cpu.cop2[RGBC] & 0xff000000);

                    g_cpu.cop2[IR1] = g_cpu.cop2[MAC1] = m1;
                    g_cpu.cop2[IR2] = g_cpu.cop2[MAC2] = m2;
                    g_cpu.cop2[IR3] = g_cpu.cop2[MAC3] = m3;

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
                    u8 sf = command & (1 << 19);
                    gte_rtps(VXY0, sf);
                    gte_rtps(VXY1, sf);
                    gte_rtps(VXY2, sf);
                    break;
                }
                case MVMVA:
                {
                    const u8 tv_lookup[] = {TRX, RBK, RFC, 64};
                    const u8 mv_lookup[] = {VXY0, VXY1, VXY2, IR1};
                    const u8 mm_lookup[] = {RT11RT12, L11L12, LR1LR2, 0};

                    u8 sf = command & (1 << 19);

                    SY_ASSERT(((command >> 13) & 0x3) != 2);
                    u8 tv = tv_lookup[(command >> 13) & 0x3];

                    
                    SY_ASSERT(((command >> 15) & 0x3) < 3);
                    u8 mv = mv_lookup[(command >> 15) & 0x3];

                    SY_ASSERT(((command >> 17) & 0x3) < 3);
                    u8 mm = mm_lookup[(command >> 17) & 0x3];

                    g_cpu.cop2[MAC1] = (s32)(g_cpu.cop2[tv] * 0x1000 + 
                                        sign_extend16_32(g_cpu.cop2[mm]) * sign_extend16_32(g_cpu.cop2[mv]) + 
                                        sign_extend16_32(g_cpu.cop2[mm] >> 16) * sign_extend16_32(g_cpu.cop2[mv] >> 16) + 
                                        sign_extend16_32(g_cpu.cop2[mm + 1]) * sign_extend16_32(g_cpu.cop2[mv + 1])) >> (sf * 12);

                    g_cpu.cop2[MAC2] = (s32)(g_cpu.cop2[tv + 1] * 0x1000 + 
                                        sign_extend16_32(g_cpu.cop2[mm + 1] >> 16) * sign_extend16_32(g_cpu.cop2[mv]) + 
                                        sign_extend16_32(g_cpu.cop2[mm + 2]) * sign_extend16_32(g_cpu.cop2[mv] >> 16) + 
                                        sign_extend16_32(g_cpu.cop2[mm + 2] >> 16) * sign_extend16_32(g_cpu.cop2[mv + 1])) >> (sf * 12);

                    g_cpu.cop2[MAC3] = (s32)(g_cpu.cop2[tv + 2] * 0x1000 + 
                                        sign_extend16_32(g_cpu.cop2[mm + 3]) * sign_extend16_32(g_cpu.cop2[mv]) + 
                                        sign_extend16_32(g_cpu.cop2[mm + 3] >> 16) * sign_extend16_32(g_cpu.cop2[mv] >> 16) + 
                                        sign_extend16_32(g_cpu.cop2[mm + 4]) * sign_extend16_32(g_cpu.cop2[mv + 1])) >> (sf * 12);
                    
                    g_cpu.cop2[IR1] = g_cpu.cop2[MAC1];
                    g_cpu.cop2[IR2] = g_cpu.cop2[MAC2];
                    g_cpu.cop2[IR3] = g_cpu.cop2[MAC3];
                    break;
                }
                default:
                    debug_log("Unhandled GTE command: %02xh\n", op);
                    break;
                }

                break;
            }

            switch ((enum coprocessor_op)ins.rs)
            {
            case MFC:
                new_load.index = ins.rt;
                new_load.value = g_cpu.cop2[ins.rd];
                break;
            case CFC:
                new_load.index = ins.rt;
                new_load.value = g_cpu.cop2[31 + ins.rd];
                break;
            case MTC:
                g_cpu.cop2[ins.rd] = g_cpu.registers[ins.rt];
                break;
            case CTC:
                g_cpu.cop2[31 + ins.rd] = g_cpu.registers[ins.rt];
                break;
            default:
                debug_log("Unhandled COP2 operation: %02x\n", ins.rs);
                break;
            }
            break;
        case LWC2:
        {
            u32 vaddr = g_cpu.registers[ins.rs] + sign_extend16_32(immediate);
            if (vaddr & 0x3)
            {
                g_cpu.cop0[8] = vaddr;
                handle_exception(EXCEPTION_CODE_ADEL);
                break;
            }
            if (g_cpu.cop0[12] & 0x10000)
            {
                //printf("Unhandled load to data cache\n");
                break;
            }
            g_cpu.cop2[ins.rt] = load32(vaddr);
            //new_load.index = ins.rt;
            //new_load.value = load32(vaddr);
            break;
        }
        case SWC2:
        {
            u32 vaddr = g_cpu.registers[ins.rs] + sign_extend16_32(immediate);
            if (vaddr & 0x3)
            {
                g_cpu.cop0[8] = vaddr;
                handle_exception(EXCEPTION_CODE_ADES);
                break;
            }
            if (g_cpu.cop0[12] & 0x10000)
            {
                //printf("Unhandled store to data cache\n");
                break;
            }
            store32(vaddr, g_cpu.cop2[ins.rt]);
            break;
        }
        case SPECIAL:
            switch ((enum secondary_op)ins.secondary)
            {
            case SLL:
                write = set_register(ins.rd, g_cpu.registers[ins.rt] << ins.sa);
                break;
            case SRL:
                write = set_register(ins.rd, g_cpu.registers[ins.rt] >> ins.sa);
                break;
            case SRA:
                write = set_register(ins.rd, (s32)g_cpu.registers[ins.rt] >> ins.sa);
                break;
            case SLLV:
                write = set_register(ins.rd, g_cpu.registers[ins.rt] << (g_cpu.registers[ins.rs] & 0x1f));
                break;
            case SRLV:
                write = set_register(ins.rd, g_cpu.registers[ins.rt] >> (g_cpu.registers[ins.rs] & 0x1f));
                break;
            case SRAV:
                write = set_register(ins.rd, (s32)g_cpu.registers[ins.rt] >> (g_cpu.registers[ins.rs] & 0x1f));
                break;
            case JR:
                g_cpu.next_pc = g_cpu.registers[ins.rs];
                branched = SY_TRUE;
                break;
            case JALR:
                write = set_register(ins.rd, g_cpu.next_pc);
                g_cpu.next_pc = g_cpu.registers[ins.rs];
                branched = SY_TRUE;
                break;
            case SYSCALL:
                handle_exception(EXCEPTION_CODE_SYSCALL);
                break;
            case BREAK:
                handle_exception(EXCEPTION_CODE_BREAKPOINT);
                break;
            case MFHI:
                write = set_register(ins.rd, g_cpu.hi);
                break;
            case MTHI:
                g_cpu.hi = g_cpu.registers[ins.rs];
                break;
            case MFLO:
                write = set_register(ins.rd, g_cpu.lo);
                break;
            case MTLO:
                g_cpu.lo = g_cpu.registers[ins.rs];
                break;
            case MULT:
            {
                u64 result = (u64)((s64)sign_extend32_64(g_cpu.registers[ins.rs]) * (s64)sign_extend32_64(g_cpu.registers[ins.rt]));
                g_cpu.lo = (u32)result;
                g_cpu.hi = result >> 32;
                break;
            }
            case MULTU:
            {
                u64 result = (u64)g_cpu.registers[ins.rs] * (u64)g_cpu.registers[ins.rt];
                g_cpu.lo = (u32)result;
                g_cpu.hi = result >> 32;
                break;
            }
            case DIV:
            {
                s32 n = (s32)g_cpu.registers[ins.rs];
                s32 d = (s32)g_cpu.registers[ins.rt];
                if (d == 0)
                {
                    g_cpu.lo = n >= 0 ? 0xffffffff : 1;
                    g_cpu.hi = (u32)n;
                }
                else if (((u32)n == 0x80000000) && (d == -1))
                {
                    g_cpu.lo = 0x80000000;
                    g_cpu.hi = 0;
                }
                else
                {
                    g_cpu.lo = (u32)(n / d);
                    g_cpu.hi = (u32)(n % d);
                }
                break;
            }
            case DIVU:
            {
                if (g_cpu.registers[ins.rt] == 0)
                {
                    g_cpu.lo = 0xffffffff;
                    g_cpu.hi = g_cpu.registers[ins.rs];
                }
                else
                {
                    g_cpu.lo = g_cpu.registers[ins.rs] / g_cpu.registers[ins.rt];
                    g_cpu.hi = g_cpu.registers[ins.rs] % g_cpu.registers[ins.rt];
                }
                break;
            }
            case ADD:
            {
                s64 add = (s64)((s32)g_cpu.registers[ins.rs]) + (s64)((s32)g_cpu.registers[ins.rt]);
                s32 result = (s32)add;
                if (result != add)
                {
                    handle_exception(EXCEPTION_CODE_OVERFLOW);
                    break;
                }
                write = set_register(ins.rd, (u32)result);
                break;
            }
            case ADDU:
            {
                write = set_register(ins.rd, g_cpu.registers[ins.rs] + g_cpu.registers[ins.rt]);
                break;
            }
            case SUB:
            {
                s64 sub = (s64)((s32)g_cpu.registers[ins.rs]) - (s64)((s32)g_cpu.registers[ins.rt]);
                s32 result = (s32)sub;
                if (result != sub)
                {
                    handle_exception(EXCEPTION_CODE_OVERFLOW);
                    break;
                }
                write = set_register(ins.rd, (u32)result);
                break;
            }
            case SUBU:
            {
                write = set_register(ins.rd, g_cpu.registers[ins.rs] - g_cpu.registers[ins.rt]);
                break;
            }
            case AND:
            {
                write = set_register(ins.rd, g_cpu.registers[ins.rs] & g_cpu.registers[ins.rt]);
                break;
            }
            case OR:
            {
                write = set_register(ins.rd, g_cpu.registers[ins.rs] | g_cpu.registers[ins.rt]);
                break;
            }
            case XOR:
            {
                write = set_register(ins.rd, g_cpu.registers[ins.rs] ^ g_cpu.registers[ins.rt]);
                break;
            }
            case NOR:
            {
                write = set_register(ins.rd, ~(g_cpu.registers[ins.rs] | g_cpu.registers[ins.rt]));
                break;
            }
            case SLT:
            {
                write = set_register(ins.rd, ((s32)g_cpu.registers[ins.rs] < (s32)g_cpu.registers[ins.rt]));
                break;
            }
            case SLTU:
            {
                write = set_register(ins.rd, (g_cpu.registers[ins.rs] < g_cpu.registers[ins.rt]));
                break;
            }
            default:
                debug_log("WARNING: Unknown secondary: %x\n", ins.secondary);
                break;
            }
            break;
        default:
            debug_log("WARNING: Unknown opcode: %x\n", ins.op);
            handle_exception(EXCEPTION_CODE_RESERVED_INSTRUCTION);
            break;
        }
#ifdef LOG_DISASM
        if (g_debug.show_disasm)
        {
            debug_log("%08x\t%08x\t", g_cpu.current_pc, ins.value);
            instr_to_string(ins, buffer, sizeof(buffer));
            debug_log(buffer);
            debug_log("\n");
        }
#endif
        if (g_cpu.load_delay.index != new_load.index)
        {
            g_cpu.registers[g_cpu.load_delay.index] = g_cpu.load_delay.value;
        }
        g_cpu.registers[write.index] = write.value;

        g_cpu.load_delay = new_load;
        new_load.index = 0;
        new_load.value = 0;

        write.index = 0;
        write.value = 0;

        g_cpu.in_branch_delay = branched;
        branched = SY_FALSE;

        g_cpu.registers[0] = 0;

        //g_cycles_elapsed += 1;//psx->pending_cycles;
        handle_interrupts();
    }
    return 0;//i;
}

static inline void gte_rtps(s8 v, s8 sf)
{
    g_cpu.cop2[IR1] = g_cpu.cop2[MAC1] = (s32)(g_cpu.cop2[TRX] * 0x1000 + 
                        sign_extend16_32(g_cpu.cop2[RT11RT12]) * sign_extend16_32(g_cpu.cop2[v]) + 
                        sign_extend16_32(g_cpu.cop2[RT11RT12] >> 16) * sign_extend16_32(g_cpu.cop2[v] >> 16) +
                        sign_extend16_32(g_cpu.cop2[RT13RT21]) * sign_extend16_32(g_cpu.cop2[v + 1])) >> (sf * 12);
    
    g_cpu.cop2[IR2] = g_cpu.cop2[MAC2] = (s32)(g_cpu.cop2[TRY] * 0x1000 + 
                        sign_extend16_32(g_cpu.cop2[RT13RT21] >> 16) * sign_extend16_32(g_cpu.cop2[v]) + 
                        sign_extend16_32(g_cpu.cop2[RT22RT23]) * sign_extend16_32(g_cpu.cop2[v] >> 16) +
                        sign_extend16_32(g_cpu.cop2[RT22RT23] >> 16) * sign_extend16_32(g_cpu.cop2[v + 1])) >> (sf * 12);

    g_cpu.cop2[IR3] = g_cpu.cop2[MAC3] = (s32)(g_cpu.cop2[TRZ] * 0x1000 + 
                        sign_extend16_32(g_cpu.cop2[RT31RT32]) * sign_extend16_32(g_cpu.cop2[v]) + 
                        sign_extend16_32(g_cpu.cop2[RT31RT32] >> 16) * sign_extend16_32(g_cpu.cop2[v] >> 16) +
                        sign_extend16_32(g_cpu.cop2[RT33]) * sign_extend16_32(g_cpu.cop2[v + 1])) >> (sf * 12);

    /* push fifo */
    g_cpu.cop2[SZ0] = g_cpu.cop2[SZ1];
    g_cpu.cop2[SZ1] = g_cpu.cop2[SZ2];
    g_cpu.cop2[SZ2] = g_cpu.cop2[SZ3];
    g_cpu.cop2[SZ3] = (s32)g_cpu.cop2[MAC3] >> ((1 - sf) * 12);

    u32 div = (((g_cpu.cop2[HPPD] * 0x20000 / g_cpu.cop2[SZ3]) + 1) / 2);
    if (div > 0x1ffff) {
        div = 0x1ffff;
        // TODO: set divide overflow in flag reg
    }
    g_cpu.cop2[MAC0] = div * g_cpu.cop2[IR1] + g_cpu.cop2[OFX];

    /* push fifo */
    g_cpu.cop2[SXY0] = g_cpu.cop2[SXY1];
    g_cpu.cop2[SXY1] = g_cpu.cop2[SXY2];

    u32 sx2 = g_cpu.cop2[MAC0] / 0x10000; // NOTE: sign extend?

    g_cpu.cop2[MAC0] = div * g_cpu.cop2[IR2] + g_cpu.cop2[OFY];

    u32 sy2 = g_cpu.cop2[MAC0] / 0x10000;

    g_cpu.cop2[SXY2] = g_cpu.cop2[SXYP]; // TODO: ?
    g_cpu.cop2[SXYP] = sx2 | (sy2 << 16);

    g_cpu.cop2[MAC0] = div * g_cpu.cop2[DQA] + g_cpu.cop2[DQB];
    g_cpu.cop2[IR0] = g_cpu.cop2[MAC0] / 0x1000;
}

static inline vec3u gte_mat3_mul_v3(s8 m, s8 v, s8 sf)
{
    u32 v0x = sign_extend16_32(g_cpu.cop2[v]);
    u32 v0y = sign_extend16_32(g_cpu.cop2[v] >> 16);
    u32 v0z = sign_extend16_32(g_cpu.cop2[v + 1]);

    u32 c1 = (s32)(sign_extend16_32(g_cpu.cop2[m]) * v0x +
                        sign_extend16_32(g_cpu.cop2[m] >> 16) * v0y +
                            sign_extend16_32(g_cpu.cop2[m + 1]) * v0z) >> (sf * 12);

    u32 c2 = (s32)(sign_extend16_32(g_cpu.cop2[m + 1] >> 16) * v0x +
                        sign_extend16_32(g_cpu.cop2[m + 2]) * v0y +
                            sign_extend16_32(g_cpu.cop2[m + 2] >> 16) * v0z) >> (sf * 12);

    u32 c3 = (s32)(sign_extend16_32(g_cpu.cop2[m + 3]) * v0x +
                        sign_extend16_32(g_cpu.cop2[m + 3] >> 16) * v0y +
                            sign_extend16_32(g_cpu.cop2[m + 4]) * v0z) >> (sf * 12);

    return v3u(c1, c2, c3);
}
