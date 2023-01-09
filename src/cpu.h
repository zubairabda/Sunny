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

typedef union
{
    struct
    {
        u32 secondary : 6;
        u32 sa : 5;
        u32 rd : 5;
        u32 rt : 5;
        u32 rs : 5;
        u32 op : 6;
    };
    u32 value;
} Instruction;

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

typedef struct
{
    u32 index;
    u32 value;
} RegTuple;

struct spu_block
{
    u8 filter;
    u8 flags;
    u8 data[14];
};

struct spu_voice
{
    u16 volume_left;
    u16 volume_right;
    u16 sample_rate;
    u16 start_addr;
    u32 adsr;
    u16 adsr_volume; // TODO: used?
    u16 repeat_addr;
};

struct spu_control
{
    u16 main_volume_left; // D80
    u16 main_volume_right;
    u16 reverb_volume_left;
    u16 reverb_volume_right;
    u32 keyon;
    u32 keyoff;
    u32 pitch_modulation_enable;
    u32 noise_mode;
    u32 reverb_mode;
    u32 status;
    u16 unk0; // DA0
    u16 reverb_work_start_addr;
    u16 irq_addr;
    u16 data_transfer_addr;
    u16 data_transfer_fifo;
    u16 spucnt; // DAA
    u16 transfer_control;
    u16 spustat; // DAE
    u16 cd_volume_left;
    u16 cd_volume_right;
    u16 extern_volume_left;
    u16 extern_volume_right;
    u16 current_main_volume_left;
    u16 current_main_volume_right;          
};

struct spu_state
{
    union
    {
        struct spu_voice data[24]; // TODO: SOA?
        u8 regs[sizeof(struct spu_voice) * 24];
    } voice;

    union
    {
        struct spu_control data;
        u8 regs[sizeof(struct spu_control)];
    } control;
    
};

struct cpu_state
{
    u32 registers[32];
    u32 hi;
    u32 lo;
    u32 pc;
    u32 next_pc;
    u32 current_pc;
    RegTuple load_delay;
    RegTuple new_load;
    b8 in_branch_delay;
    u32 cachectrl;
    u32 i_stat;
    u32 i_mask;

    u32 cop0[32];
    u32 cop2[64];

    struct root_counter timers[3];

    struct memory_pool event_pool;
    struct tick_event* sentinel_event;
    s32 system_tick_count;

    u8* bios;
    u8* ram;
    u8* scratch;
    u8* peripheral;
    struct dma_state dma;
    struct gpu_state gpu;
    u8* sound_ram;
    struct spu_state spu;
    struct joypad_state pad;
    struct cdrom_state cdrom;
};