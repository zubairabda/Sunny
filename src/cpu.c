#include "cpu.h"
#include "psx.h"
#include "event.h"
#include "memory.h"
#include "debug.h"
#include "disasm.h"

static inline reg_tuple set_register(u32 index, u32 value)
{
    reg_tuple load = {.index = index, .value = value};
    return load;
}

static inline void handle_exception(struct cpu_state* cpu, enum exception_code cause)
{
#if 0
    cpu->cop0[13] &= ~(0x7c);
    cpu->cop0[13] |= ((u32)cause << 2);
#else
    cpu->cop0[13] = ((u32)cause << 2);
#endif
    if (cause == EXCEPTION_CODE_INTERRUPT)
    {
        cpu->cop0[14] = cpu->pc;
    }
    else
    {
        cpu->cop0[14] = cpu->current_pc;
    }

    if (cpu->in_branch_delay)
    {
        cpu->cop0[13] |= (1 << 31);
        cpu->cop0[14] -= 4; //= cpu->current_pc - 4;
    }
#if 0
    else
    {
        cpu->cop0[14] = cpu->current_pc;
    }
#endif

    u32 stack = (cpu->cop0[12] << 2) & 0x3f;
    cpu->cop0[12] &= ~0x3f;
    cpu->cop0[12] |= stack;

    cpu->pc = (cpu->cop0[12] & 0x400000) ? 0xbfc00180 : 0x80000080;
    cpu->next_pc = cpu->pc + 4;
    cpu->in_branch_delay = 0;
}

static inline void handle_interrupts(struct cpu_state* cpu)
{
    if (cpu->i_stat & cpu->i_mask)
    {
        cpu->cop0[13] |= 0x400;
    }
    else
    {
        cpu->cop0[13] &= ~0x400;
    }

    u8 ip = (cpu->cop0[13] >> 8) & 0xff;
    u8 im = (cpu->cop0[12] >> 8) & 0xff;
    if ((cpu->cop0[12] & 0x1) && (ip & im))
    {
        handle_exception(cpu, EXCEPTION_CODE_INTERRUPT);
    }
}

static inline void log_tty(struct psx_state *psx)
{
    struct cpu_state *cpu = psx->cpu;
    if (cpu->pc == 0xb0)
    {
        switch (cpu->registers[9])
        {
        case 0x35:
            if (cpu->registers[4] != 1)
                break;
            char* str;
            u32 addr = cpu->registers[5] & 0x1fffffff;
            if (addr >= 0x1fc00000 && addr < 0x1fc80000)
            {
                str = (char *)(psx->bios + (addr - 0x1fc00000));
            }
            else
            {
                SY_ASSERT(0);
            }
            u32 size = cpu->registers[6];
            while (size--)
                debug_putchar(*str++);
            break;
        case 0x3d:
            debug_putchar(cpu->registers[4]);
            break;
        }
    }
}

u32 fetch_instruction(struct psx_state *psx, u32 pc)
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
            return *(u32 *)(psx->ram + (addr & 0x1fffff));
        }
        else if (addr >= 0x1f800000 && addr < 0x1f800400)
        {
            return *(u32 *)(psx->scratch + (addr & 0x3ff));
        }
        else if (addr >= 0x1fc00000 && addr < 0x1fc80000)
        {
            return *(u32 *)(psx->bios + (addr & 0x7ffff));
        }
        else
        {
            return 0;
        }
        break;
    }
    case 0x5: // KSEG1
    {
        return *(u32 *)mem_read(psx, addr);
    }
    SY_INVALID_CASE;
    }
}

u64 execute_instruction(struct psx_state *psx, u64 min_cycles)
{
    struct cpu_state *cpu = psx->cpu;
    b32 branched = SY_FALSE;
    reg_tuple new_load = {0};
    reg_tuple write = {0};
    b8 show_ins = 0;
    u64 target_cycles = g_cycles_elapsed + min_cycles;
    //u64 i;

    char buffer[64];
    memset(buffer, 0, sizeof(buffer));

    //for (i = 0; i < min_cycles; i += psx->pending_cycles)
    while (g_cycles_elapsed <= target_cycles)
    {
        ++g_cycles_elapsed;
        //psx->pending_cycles = 1;
        log_tty(psx);
#if 1
        // exe sideloading
        if (cpu->pc == 0x80030000)
        {
            u8* fp = g_debug.loaded_exe;
            u32 dst = U32FromPtr(fp + 0x18);
            u32 size = U32FromPtr(fp + 0x1c);
            memcpy((psx->ram + (dst & 0x1fffffff)), (fp + 0x800), size);
            cpu->pc = U32FromPtr(fp + 0x10);
            cpu->registers[28] = U32FromPtr(fp + 0x14);
            if (U32FromPtr(fp + 0x30) != 0)
            {
                cpu->registers[29] = U32FromPtr(fp + 0x30) + U32FromPtr(fp + 0x34);
                cpu->registers[30] = U32FromPtr(fp + 0x30) + U32FromPtr(fp + 0x34);
            }
            cpu->next_pc = cpu->pc + 4;
        }
#endif
        if (cpu->pc & 0x3) // NOTE: this seems to fix amidog exception tests but im not sure its needed
        {
            cpu->cop0[8] = cpu->pc;
            handle_exception(cpu, EXCEPTION_CODE_ADEL);
        }

        instruction ins = {.value = fetch_instruction(psx, cpu->pc)};
        cpu->current_pc = cpu->pc;

        cpu->pc = cpu->next_pc;
        cpu->next_pc = cpu->pc + 4;
        
        u32 immediate = ins.value & 0xffff;
        u32 target = ins.value & 0x3ffffff;

        switch ((enum primary_op)ins.op)
        {
        case BCOND:
        {
            // TODO: fix
            u8 type = ins.rt & 0x1; // if bit 16 is set, it is bgez, otherwise it is bltz
            u8 link = (ins.rt & 0x1e) == 0x10; // top 4 bits must be set to 0x10
            b8 branch = ((s32)cpu->registers[ins.rs] < 0) ^ type;

            if (link)
                write = set_register(31, cpu->next_pc);

            if (branch)
                cpu->next_pc = cpu->pc + (sign_extend16_32(immediate) << 2);
            branched = SY_TRUE;
            break;
        }
        case ANDI:
        {
            write = set_register(ins.rt, cpu->registers[ins.rs] & immediate);
            break;
        }
        case ADDI:
        {
            s64 add = (s64)((s32)cpu->registers[ins.rs]) + (s64)((s32)sign_extend16_32(immediate));
            s32 result = (s32)add;
            if (result != add)
            {
                handle_exception(cpu, EXCEPTION_CODE_OVERFLOW);
                break;
            }
            write = set_register(ins.rt, (u32)result);
            break;
        }
        case ADDIU:
        {
            write = set_register(ins.rt, cpu->registers[ins.rs] + sign_extend16_32(immediate));
            break;
        }
        case SLTI:
        {
            write = set_register(ins.rt, (s32)cpu->registers[ins.rs] < (s32)sign_extend16_32(immediate));
            break;
        }
        case SLTIU:
        {
            write = set_register(ins.rt, cpu->registers[ins.rs] < sign_extend16_32(immediate));
            break;
        }
        case ORI:
        {
            write = set_register(ins.rt, cpu->registers[ins.rs] | immediate);
            break;
        }
        case XORI:
        {
            write = set_register(ins.rt, cpu->registers[ins.rs] ^ immediate);
            break;
        }
        case LUI:
        {
            write = set_register(ins.rt, immediate << 16);
            break;
        }
        case LB:
        {
            u32 vaddr = cpu->registers[ins.rs] + sign_extend16_32(immediate);
            if (cpu->cop0[12] & 0x10000)
            {
                //printf("Unhandled load to data cache\n");
                break;
            }
            s8 value = load8(psx, vaddr);
            new_load.index = ins.rt;
            new_load.value = (u32)(value);
            break;
        }
        case LH:
        {
            u32 vaddr = cpu->registers[ins.rs] + sign_extend16_32(immediate);
            if (vaddr & 0x1)
            {
                cpu->cop0[8] = vaddr;
                handle_exception(cpu, EXCEPTION_CODE_ADEL);
                break;
            }
            if (cpu->cop0[12] & 0x10000)
            {
                break;
            }
            s16 value = (s16)load16(psx, vaddr);
            new_load.index = ins.rt;
            new_load.value = (u32)value;
            break;
        };
        case LWL:
        {
            u32 vaddr = cpu->registers[ins.rs] + sign_extend16_32(immediate);
            if (cpu->cop0[12] & 0x10000)
                break;
            u32 aligned_addr = vaddr & ~0x3;
            u32 value = load32(psx, aligned_addr);
            u32 merge = cpu->load_delay.index ? cpu->load_delay.value : cpu->registers[ins.rt];
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
            u32 vaddr = cpu->registers[ins.rs] + sign_extend16_32(immediate);
            if (cpu->cop0[12] & 0x10000)
                break;
            u32 aligned_addr = vaddr & ~0x3;
            u32 value = load32(psx, aligned_addr);
            u32 merge = cpu->load_delay.index ? cpu->load_delay.value : cpu->registers[ins.rt];
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
            u32 vaddr = cpu->registers[ins.rs] + sign_extend16_32(immediate);
            if (vaddr & 0x3)
            {
                cpu->cop0[8] = vaddr;
                handle_exception(cpu, EXCEPTION_CODE_ADEL);
                break;
            }
            if (cpu->cop0[12] & 0x10000)
            {
                //printf("Unhandled load to data cache\n");
                break;
            }
            new_load.index = ins.rt;
            new_load.value = load32(psx, vaddr);
            break;
        }
        case LBU:
        {
            u32 vaddr = cpu->registers[ins.rs] + sign_extend16_32(immediate);
            if (cpu->cop0[12] & 0x10000)
            {
                //printf("Unhandled load to data cache\n");
                break;
            }
            new_load.index = ins.rt;
            new_load.value = load8(psx, vaddr);
            break;
        }
        case LHU:
        {
            u32 vaddr = cpu->registers[ins.rs] + sign_extend16_32(immediate);
            if (vaddr & 0x1)
            {
                cpu->cop0[8] = vaddr;
                handle_exception(cpu, EXCEPTION_CODE_ADEL);
                break;
            }
            if (cpu->cop0[12] & 0x10000)
            {
                break;
            }
            new_load.index = ins.rt;
            new_load.value = load16(psx, vaddr);
            break;
        }
        case SB:
        {
            u32 vaddr = cpu->registers[ins.rs] + sign_extend16_32(immediate);
            if (cpu->cop0[12] & 0x10000)
            {
                //printf("Unhandled store to data cache\n");
                break;
            }
            store8(psx, vaddr, cpu->registers[ins.rt]);
            break;
        }
        case SH:
        {
            u32 vaddr = cpu->registers[ins.rs] + sign_extend16_32(immediate);
            if (vaddr & 0x1)
            {
                cpu->cop0[8] = vaddr;
                handle_exception(cpu, EXCEPTION_CODE_ADES);
                break;
            }
            if (cpu->cop0[12] & 0x10000)
            {
                //printf("Unhandled store to data cache\n");
                break;
            }
            store16(psx, vaddr, cpu->registers[ins.rt]);
            break;
        }
        case SWL:
        {
            u32 vaddr = cpu->registers[ins.rs] + sign_extend16_32(immediate);
            if (cpu->cop0[12] & 0x10000)
                break;
            u32 aligned = vaddr & ~0x3;
            u32 value = load32(psx, aligned);
            u32 store;
            switch (vaddr & 0x3)
            {
            case 0:
                store = (value & 0xffffff00) | (cpu->registers[ins.rt] >> 24);
                break;
            case 1:
                store = (value & 0xffff0000) | (cpu->registers[ins.rt] >> 16);
                break;
            case 2:
                store = (value & 0xff000000) | (cpu->registers[ins.rt] >> 8);
                break;
            case 3:
                store = cpu->registers[ins.rt];
                break;
            }
            store32(psx, aligned, store);
            break;
        }
        case SWR:
        {
            u32 vaddr = cpu->registers[ins.rs] + sign_extend16_32(immediate);
            if (cpu->cop0[12] & 0x10000)
                break;
            u32 aligned = vaddr & ~0x3;
            u32 value = load32(psx, aligned);
            u32 write;
            switch (vaddr & 0x3)
            {
            case 0:
                write = cpu->registers[ins.rt];
                break;
            case 1:
                write = (value & 0xff) | (cpu->registers[ins.rt] << 8);
                break;
            case 2:
                write = (value & 0xffff) | (cpu->registers[ins.rt] << 16);
                break;
            case 3:
                write = (value & 0xffffff) | (cpu->registers[ins.rt] << 24);
                break;
            }
            store32(psx, aligned, write);
            break;
        }
        case SW:
        {
            u32 vaddr = cpu->registers[ins.rs] + sign_extend16_32(immediate);
            if (vaddr & 0x3)
            {
                cpu->cop0[8] = vaddr;
                handle_exception(cpu, EXCEPTION_CODE_ADES);
                break;
            }
            if (cpu->cop0[12] & 0x10000)
            {
                //printf("Unhandled store to data cache\n");
                break;
            }
            store32(psx, vaddr, cpu->registers[ins.rt]);
            break;
        }
        case BEQ:
        {
            if (cpu->registers[ins.rs] == cpu->registers[ins.rt])
            {
                cpu->next_pc = cpu->pc + (sign_extend16_32(immediate) << 2);
            }
            branched = SY_TRUE;
            break;
        }
        case BNE:
        {
            if (cpu->registers[ins.rs] != cpu->registers[ins.rt])
            {
                cpu->next_pc = cpu->pc + (sign_extend16_32(immediate) << 2);
            }
            branched = SY_TRUE;
            break;
        }
        case BLEZ:
        {
            if ((s32)cpu->registers[ins.rs] <= 0)
            {
                cpu->next_pc = cpu->pc + (sign_extend16_32(immediate) << 2);
            }
            branched = SY_TRUE;
            break;
        }
        case BGTZ:
        {
            if ((s32)cpu->registers[ins.rs] > 0)
            {               
                cpu->next_pc = cpu->pc + (sign_extend16_32(immediate) << 2);
            }
            branched = SY_TRUE;
            break;
        }
        case J:
        {
            cpu->next_pc = (cpu->pc & 0xf0000000) | (target << 2);
            branched = SY_TRUE;
            break;
        }
        case JAL:
        {
            write = set_register(31, cpu->next_pc);
            cpu->next_pc = (cpu->pc & 0xf0000000) | (target << 2);
            branched = SY_TRUE;
            break;
        }
        case COP0:
            switch ((enum coprocessor_op)ins.rs)
            {
            case MF:
                new_load.index = ins.rt;
                new_load.value = cpu->cop0[ins.rd];
                break;
            case MTC:
                cpu->cop0[ins.rd] = cpu->registers[ins.rt];
                break;
            case RFE:
            {
                SY_ASSERT(ins.secondary == 0x10);
                u32 stack = (cpu->cop0[12] >> 2) & 0xf;
                cpu->cop0[12] &= ~0xf;
                cpu->cop0[12] |= stack;
            }   break;
            default:
                printf("WARNING: Unknown coprocessor op: %x\n", ins.rs); // TODO: RI exception
                break;
            }
            break;
        case COP2:
            printf("Unhandled COP2 operation: %02x\n", ins.rs);
            break;
        case SPECIAL:
            switch ((enum secondary_op)ins.secondary)
            {
            case SLL:
                write = set_register(ins.rd, cpu->registers[ins.rt] << ins.sa);
                break;
            case SRL:
                write = set_register(ins.rd, cpu->registers[ins.rt] >> ins.sa);
                break;
            case SRA:
                write = set_register(ins.rd, (s32)cpu->registers[ins.rt] >> ins.sa);
                break;
            case SLLV:
                write = set_register(ins.rd, cpu->registers[ins.rt] << (cpu->registers[ins.rs] & 0x1f));
                break;
            case SRLV:
                write = set_register(ins.rd, cpu->registers[ins.rt] >> (cpu->registers[ins.rs] & 0x1f));
                break;
            case SRAV:
                write = set_register(ins.rd, (s32)cpu->registers[ins.rt] >> (cpu->registers[ins.rs] & 0x1f));
                break;
            case JR:
                cpu->next_pc = cpu->registers[ins.rs];
                branched = SY_TRUE;
                break;
            case JALR:
                write = set_register(ins.rd, cpu->next_pc);
                cpu->next_pc = cpu->registers[ins.rs];
                branched = SY_TRUE;
                break;
            case SYSCALL:
                handle_exception(cpu, EXCEPTION_CODE_SYSCALL);
                break;
            case BREAK:
                handle_exception(cpu, EXCEPTION_CODE_BREAKPOINT);
                break;
            case MFHI:
                write = set_register(ins.rd, cpu->hi);
                break;
            case MTHI:
                cpu->hi = cpu->registers[ins.rs];
                break;
            case MFLO:
                write = set_register(ins.rd, cpu->lo);
                break;
            case MTLO:
                cpu->lo = cpu->registers[ins.rs];
                break;
            case MULT:
            {
                u64 result = (u64)((s64)sign_extend32_64(cpu->registers[ins.rs]) * (s64)sign_extend32_64(cpu->registers[ins.rt]));
                cpu->lo = (u32)result;
                cpu->hi = result >> 32;
                break;
            }
            case MULTU:
            {
                u64 result = (u64)cpu->registers[ins.rs] * (u64)cpu->registers[ins.rt];
                cpu->lo = (u32)result;
                cpu->hi = result >> 32;
                break;
            }
            case DIV:
            {
                s32 n = (s32)cpu->registers[ins.rs];
                s32 d = (s32)cpu->registers[ins.rt];
                if (d == 0)
                {
                    cpu->lo = n >= 0 ? 0xffffffff : 1;
                    cpu->hi = (u32)n;
                }
                else if (((u32)n == 0x80000000) && (d == -1))
                {
                    cpu->lo = 0x80000000;
                    cpu->hi = 0;
                }
                else
                {
                    cpu->lo = (u32)(n / d);
                    cpu->hi = (u32)(n % d);
                }
                break;
            }
            case DIVU:
            {
                if (cpu->registers[ins.rt] == 0)
                {
                    cpu->lo = 0xffffffff;
                    cpu->hi = cpu->registers[ins.rs];
                }
                else
                {
                    cpu->lo = cpu->registers[ins.rs] / cpu->registers[ins.rt];
                    cpu->hi = cpu->registers[ins.rs] % cpu->registers[ins.rt];
                }
                break;
            }
            case ADD:
            {
                s64 add = (s64)((s32)cpu->registers[ins.rs]) + (s64)((s32)cpu->registers[ins.rt]);
                s32 result = (s32)add;
                if (result != add)
                {
                    handle_exception(cpu, EXCEPTION_CODE_OVERFLOW);
                    break;
                }
                write = set_register(ins.rd, (u32)result);
                break;
            }
            case ADDU:
            {
                write = set_register(ins.rd, cpu->registers[ins.rs] + cpu->registers[ins.rt]);
                break;
            }
            case SUB:
            {
                s64 sub = (s64)((s32)cpu->registers[ins.rs]) - (s64)((s32)cpu->registers[ins.rt]);
                s32 result = (s32)sub;
                if (result != sub)
                {
                    handle_exception(cpu, EXCEPTION_CODE_OVERFLOW);
                    break;
                }
                write = set_register(ins.rd, (u32)result);
                break;
            }
            case SUBU:
            {
                write = set_register(ins.rd, cpu->registers[ins.rs] - cpu->registers[ins.rt]);
                break;
            }
            case AND:
            {
                write = set_register(ins.rd, cpu->registers[ins.rs] & cpu->registers[ins.rt]);
                break;
            }
            case OR:
            {
                write = set_register(ins.rd, cpu->registers[ins.rs] | cpu->registers[ins.rt]);
                break;
            }
            case XOR:
            {
                write = set_register(ins.rd, cpu->registers[ins.rs] ^ cpu->registers[ins.rt]);
                break;
            }
            case NOR:
            {
                write = set_register(ins.rd, ~(cpu->registers[ins.rs] | cpu->registers[ins.rt]));
                break;
            }
            case SLT:
            {
                write = set_register(ins.rd, ((s32)cpu->registers[ins.rs] < (s32)cpu->registers[ins.rt]));
                break;
            }
            case SLTU:
            {
                write = set_register(ins.rd, (cpu->registers[ins.rs] < cpu->registers[ins.rt]));
                break;
            }
            default:
                debug_log("WARNING: Unknown secondary: %x\n", ins.secondary);
                break;
            }
            break;
        default:
            debug_log("WARNING: Unknown opcode: %x\n", ins.op);
            handle_exception(cpu, EXCEPTION_CODE_RESERVED_INSTRUCTION);
            break;
        }
#if 0
        if (g_debug.show_disasm)
        {
            debug_log("%08x\t%08x\t", cpu->current_pc, ins.value);
            instr_to_string(ins, buffer, sizeof(buffer));
            debug_log(buffer);
            debug_log("\n");
        }
#endif
        if (cpu->load_delay.index != new_load.index)
        {
            cpu->registers[cpu->load_delay.index] = cpu->load_delay.value;
        }
        cpu->registers[write.index] = write.value;

        cpu->load_delay = new_load;
        new_load.index = 0;
        new_load.value = 0;

        write.index = 0;
        write.value = 0;

        cpu->in_branch_delay = branched;
        branched = SY_FALSE;

        cpu->registers[0] = 0;

        //g_cycles_elapsed += 1;//psx->pending_cycles;

        handle_interrupts(cpu);
    }
    return 0;//i;
}
