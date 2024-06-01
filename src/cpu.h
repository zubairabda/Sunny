#ifndef CPU_H
#define CPU_H

#include "psx.h"

#define CPU_CLOCK 33868800

enum primary_op
{
    SPECIAL = 0x0,
    BCOND = 0x1,
    J = 0x2,
    JAL = 0x3,
    BEQ = 0x4,
    BNE = 0x5,
    BLEZ = 0x6,
    BGTZ = 0x7,
    ADDI = 0x8,
    ADDIU = 0x9,
    SLTI = 0xA,
    SLTIU = 0xB,
    ANDI = 0xC,
    ORI = 0xD,
    XORI = 0xE,
    LUI = 0xF,
    COP0 = 0x10,
    COP2 = 0x12,
    LB = 0x20,
    LH = 0x21,
    LWL = 0x22,
    LW = 0x23,
    LBU = 0x24,
    LHU = 0x25,
    LWR = 0x26,
    SB = 0x28,
    SH = 0x29,
    SWL = 0x2A,
    SW = 0x2B,
    SWR = 0x2E
};

enum secondary_op
{
    SLL = 0x0,
    SRL = 0x2,
    SRA = 0x3,
    SLLV = 0x4,
    SRLV = 0x6,
    SRAV = 0x7,
    JR = 0x8,
    JALR = 0x9,
    SYSCALL = 0xC,
    BREAK = 0xD,
    MFHI = 0x10,
    MTHI = 0x11,
    MFLO = 0x12,
    MTLO = 0x13,
    MULT = 0x18,
    MULTU = 0x19,
    DIV = 0x1A,
    DIVU = 0x1B,
    ADD = 0x20,
    ADDU = 0x21,
    SUB = 0x22,
    SUBU = 0x23,
    AND = 0x24,
    OR = 0x25,
    XOR = 0x26,
    NOR = 0x27,
    SLT = 0x2A,
    SLTU = 0x2B 
};

enum coprocessor_op
{
    MF = 0x0,
    MTC = 0x4,
    RFE = 0x10
};

typedef union {
    struct {
        u32 secondary : 6;
        u32 sa : 5;
        u32 rd : 5;
        u32 rt : 5;
        u32 rs : 5;
        u32 op : 6;
    };
    u32 value;
} instruction;

enum exception_code
{
    EXCEPTION_CODE_INTERRUPT = 0x0,
    EXCEPTION_CODE_ADEL = 0x4,
    EXCEPTION_CODE_ADES = 0x5,
    EXCEPTION_CODE_IBE = 0x6,
    EXCEPTION_CODE_DBE = 0x7,
    EXCEPTION_CODE_SYSCALL = 0x8,
    EXCEPTION_CODE_BREAKPOINT = 0x9,
    EXCEPTION_CODE_RESERVED_INSTRUCTION = 0xA,
    EXCEPTION_CODE_COP_UNUSABLE = 0xB,
    EXCEPTION_CODE_OVERFLOW = 0xC
};

enum interrupt_code
{
    INTERRUPT_VBLANK = 0x1,
    INTERRUPT_GPU = 0x2,
    INTERRUPT_CDROM = 0x4,
    INTERRUPT_DMA = 0x8,
    INTERRUPT_TIMER0 = 0x10,
    INTERRUPT_TIMER1 = 0x20,
    INTERRUPT_TIMER2 = 0x40,
    INTERRUPT_CONTROLLER = 0x80,
    INTERRUPT_SIO = 0x100,
    INTERRUPT_SPU = 0x200,
    INTERRUPT_PIO = 0x400
};

typedef struct {
    u32 index;
    u32 value;
} reg_tuple;

struct cpu_state
{
    u32 registers[32];
    u32 hi;
    u32 lo;
    u32 pc;
    u32 next_pc;
    u32 current_pc;
    reg_tuple load_delay;
    reg_tuple new_load;
    b8 in_branch_delay;
    u32 cachectrl;
    u32 i_stat;
    u32 i_mask;

    u32 cop0[32];
    u32 cop2[64];
};

#define USEC_PER_CYCLE 1000000.0f / CPU_CLOCK

inline u32 usec_to_cycles(u32 usec)
{
    f64 a = usec / USEC_PER_CYCLE;
    return (u32)a;
}

inline u32 sign_extend8_32(u32 val)
{
    s8 temp = (s8)val;
    return (u32)temp;
}

inline u32 sign_extend16_32(u32 val)
{
    s16 temp = (s16)val;
    return (u32)temp;
}

inline u64 sign_extend32_64(u64 val)
{
    s32 temp = (s32)val;
    return (u64)temp;
}

u64 execute_instruction(struct psx_state *psx, u64 min_cycles);

#endif
