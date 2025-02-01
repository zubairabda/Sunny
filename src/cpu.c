#include "cpu.h"
#include "gte.h"
#include "event.h"
#include "memory.h"
#include "debug.h"
#include "disasm.h"

#define COP0_SR 12
#define COP0_CAUSE 13
#define COP0_EPC 14

struct cpu_state g_cpu;

void set_interrupt(u32 param, s32 cycles_late)
{
    g_cpu.i_stat |= param;
}

static inline reg_tuple set_register(u32 index, u32 value)
{
    reg_tuple load = {.index = index, .value = value};
    return load;
}

u32 fetch_instruction(u32 pc); // TODO: temp
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
        if ((fetch_instruction(g_cpu.pc) & 0xfe000000) == 0x4a000000)
            return;
        else
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
                putchar(*str++);
            break;
        case 0x3d:
            putchar(g_cpu.registers[4]);
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
    b8 branched = false;
    reg_tuple new_load = {0};
    reg_tuple write = {0};
    u64 target_cycles = g_cycles_elapsed + min_cycles;

    while (g_cycles_elapsed < target_cycles)
    {
        ++g_cycles_elapsed;
        //g_cycles_elapsed += 2;
        log_tty();

        if (g_cpu.pc & 0x3) // NOTE: this seems to fix amidog exception tests but im not sure its needed
        {
            g_cpu.cop0[8] = g_cpu.pc;
            handle_exception(EXCEPTION_CODE_ADEL);
        }

        instruction ins = {.value = fetch_instruction(g_cpu.pc)};
        
        g_cpu.current_pc = g_cpu.pc;
        g_cpu.pc = g_cpu.next_pc;
        g_cpu.next_pc += 4;

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
            branched = true;
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
            branched = true;
            break;
        }
        case BNE:
        {
            if (g_cpu.registers[ins.rs] != g_cpu.registers[ins.rt])
            {
                g_cpu.next_pc = g_cpu.pc + (sign_extend16_32(immediate) << 2);
            }
            branched = true;
            break;
        }
        case BLEZ:
        {
            if ((s32)g_cpu.registers[ins.rs] <= 0)
            {
                g_cpu.next_pc = g_cpu.pc + (sign_extend16_32(immediate) << 2);
            }
            branched = true;
            break;
        }
        case BGTZ:
        {
            if ((s32)g_cpu.registers[ins.rs] > 0)
            {               
                g_cpu.next_pc = g_cpu.pc + (sign_extend16_32(immediate) << 2);
            }
            branched = true;
            break;
        }
        case J:
        {
            g_cpu.next_pc = (g_cpu.pc & 0xf0000000) | (target << 2);
            branched = true;
            break;
        }
        case JAL:
        {
            write = set_register(31, g_cpu.next_pc);
            g_cpu.next_pc = (g_cpu.pc & 0xf0000000) | (target << 2);
            branched = true;
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
                if (ins.rd == COP0_CAUSE) {
                    //printf("writing %08x to CAUSE\n", g_cpu.registers[ins.rt]);
                    g_cpu.cop0[COP0_CAUSE] &= 0xfffffcff;
                    g_cpu.cop0[COP0_CAUSE] |= g_cpu.registers[ins.rt] & 0x300;
                    break;
                }
#if 0
                if (ins.rd == 14) {
                    printf("writing %08x to EPC\n", g_cpu.registers[ins.rt]);
                }
                if (ins.rd == 8) {
                    printf("writing %08x to BadVaddr\n", g_cpu.registers[ins.rt]);
                }
#endif

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
                gte_command(ins.value);
                break;
            }

            switch ((enum coprocessor_op)ins.rs)
            {
            case MFC:
                new_load.index = ins.rt;
                new_load.value = gte_read(ins.rd);
                break;
            case CFC:
                new_load.index = ins.rt;
                new_load.value = gte_read(32 + ins.rd);
                break;
            case MTC:
                gte_write(ins.rd, g_cpu.registers[ins.rt]);
                break;
            case CTC:
                gte_write(32 + ins.rd, g_cpu.registers[ins.rt]);
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
                branched = true;
                break;
            case JALR:
                write = set_register(ins.rd, g_cpu.next_pc);
                g_cpu.next_pc = g_cpu.registers[ins.rs];
                branched = true;
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
                //handle_exception(EXCEPTION_CODE_RESERVED_INSTRUCTION);
                break;
            }
            break;
        default:
            debug_log("WARNING: Unknown opcode: %x\n", ins.op);
            //handle_exception(EXCEPTION_CODE_RESERVED_INSTRUCTION);
            break;
        }

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
        branched = false;

        g_cpu.registers[0] = 0;

        //g_cycles_elapsed += 1;//psx->pending_cycles;
        handle_interrupts();
    }
    return 0;//i;
}
