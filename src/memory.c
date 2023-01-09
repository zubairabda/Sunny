enum AddressMapping
{
    RAM_START = 0x200000,
    BIOS_START = 0x1fc00000,
    JOYPAD_START = 0x1f801040,
    IRQ_START = 0x1f801070,
    DMA_CHANNEL_START = 0x1f801080,
    DMA_REGISTER_START = 0x1f8010f0,
    TIMERS_START = 0x1f801100,
    GPU_START = 0x1f801810,
    SPU_START = 0x1f801c00,
    CDROM_START = 0x1f801800
};

static inline u16 joypad_read(struct cpu_state* cpu, u32 offset)
{
    u16 result = 0;
    switch (offset)
    {
    case 0x0:
        result = cpu->pad.stat;
        break;
    case 0x4:
        break;
    case 0x8:
        break;
    case 0xA:
        break;
    case 0xE:
        break;
    SY_INVALID_CASE;
    }
    return result;
}

static inline u32 irq_read(struct cpu_state* cpu, u32 offset)
{
    u32 result = 0;
    switch (offset)
    {
    case 0x0:
        result = cpu->i_stat;
        break;
    case 0x4:
        result = cpu->i_mask;
        break;
    default:
        printf("Unhandled read offset in IRQ\n");
        break;
    }
    return result;
}
// TODO: use masks instead of offsets
static u32 read32(struct cpu_state* cpu, u32 vaddr)
{
    u32 result = 0;

    u32 addr = vaddr & 0x1fffffff;
    // TODO: ram mirroring
    if (addr < 0x200000)
    { result = U32FromPtr(cpu->ram + addr); }
    else if (addr >= 0x1f000000 && addr < 0x1f800000)
    { result = 0xffffffff; }
    else if (addr >= 0x1f800000 && addr < 0x1f800400)
    { result = U32FromPtr(cpu->scratch + (addr & 0x3ff)); }
    else if (addr >= 0x1f801000 && addr < 0x1f801024) // memory control 1
    { result = 0xffffffff; }
    else if (addr >= 0x1f801040 && addr < 0x1f801060) // peripheral I/O
    { printf("Unhandled %s in PERIPHERAL at %08x\n", __FUNCTION__, addr); result = 0xffffffff; }
    else if (addr >= 0x1f801060 && addr < 0x1f801062) // memory control 2 NOTE: not sure if this is 4 or 2 in length
    { result = 0xffffffff; }
    else if (addr >= 0x1f801070 && addr < 0x1f801078)
    {
        result = irq_read(cpu, addr - IRQ_START);
    }
    else if (addr >= 0x1f801080 && addr < 0x1f8010f8)
    {
        // handle DMA read
        //result = U32FromPtr(cpu->dma + (addr - DMA_CHANNEL_START));
        u32 offset = addr - DMA_CHANNEL_START;
        result = dma_read(cpu, offset);
    }
    else if (addr >= 0x1f801100 && addr < 0x1f801130) // timers
    {
        result = timer_read(cpu, addr - TIMERS_START);
    }
    else if (addr == 0x1f801800)
    {
        result = (cpu->cdrom.status * 0x01010101); // ref: nocash
    }
    else if (addr == 0x1f801810)
    {
        result = gpuread(&cpu->gpu);
    }
    else if (addr == 0x1f801814)
    {
        result = cpu->gpu.stat.value; // GPUSTAT
    }
    else if (addr >= 0x1f801c00 && addr < 0x1f802000)
    {
        //result = U32FromPtr(cpu->spu + (addr - SPU_START));
        //printf("Unhandled %s in SPU at: %08x\n", __FUNCTION__, addr);
        result = 0xffffffff;
    }
    else if (addr >= 0x1F802000 && addr < 0x1F804000)
    { result = 0xffffffff; } // exp region 2
    else if (addr >= 0x1FA00000 && addr < 0x1FC00000)
    { result = 0xffffffff; } // exp region 3
    else if (addr >= 0x1FC00000 && addr < 0x1FC80000)
    { result = U32FromPtr(cpu->bios + (addr - 0x1fc00000)); }
    else if (vaddr >= 0xfffe0130 && vaddr < 0xfffe0134)
    { result = cpu->cachectrl; }
    else
    { printf("Unknown address mapping at: %08x\n", vaddr); result = 0xffffffff; }

    return result;
}

static u16 read16(struct cpu_state* cpu, u32 vaddr)
{
    u16 result = 0;

    u32 addr = vaddr & 0x1fffffff;

    if (addr < 0x200000)
    { result = U16FromPtr(cpu->ram + addr); }
    else if (addr >= 0x1f000000 && addr < 0x1f800000) // exp region 1
    { result = 0xffff; }
    else if (addr >= 0x1f800000 && addr < 0x1f800400)
    { result = U16FromPtr(cpu->scratch + (addr & 0x3ff)); }
    else if (addr >= 0x1f801000 && addr < 0x1f801024) // memory control 1
    { result = 0xffff; }
    else if (addr >= 0x1f801040 && addr < 0x1f801050) // peripheral I/O
    {
        result = 0xffff;//U16FromPtr(cpu->peripheral + (addr - JOYPAD_START));
        printf("%s in JOYPAD\n", __FUNCTION__);
    }
    else if (addr >= 0x1f801050 && addr < 0x1f801060)
    {
        printf("Unhandled %s in SIO at %08x\n", __FUNCTION__, addr);
    }
    else if (addr >= 0x1f801060 && addr < 0x1f801062) // memory control 2 NOTE: not sure if this is 4 or 2 in length
    { result = 0xffff; }
    else if (addr >= 0x1f801070 && addr < 0x1f801078)
    {
        result = irq_read(cpu, addr - IRQ_START);
    }
    else if (addr >= 0x1f801080 && addr < 0x1f8010f8)
    {
        // handle DMA read
        //result = U16FromPtr(cpu->dma + (addr - DMA_CHANNEL_START));
        SY_ASSERT(0);
    }
    else if (addr >= 0x1f801100 && addr < 0x1f801130) // timers
    {
        //printf("read16 in timers: %08x\n", addr);
        //result = 0xffff;
        result = timer_read(cpu, addr - TIMERS_START);
    }
    else if (addr >= 0x1f801800 && addr < 0x1f801804)
    {
        printf("Unhandled %s in CDROM at: %08x\n", __FUNCTION__, addr);
        result = 0xffff;
    }
    else if (addr == 0x1f801810)
    {
        SY_ASSERT(0);
    }
    else if (addr == 0x1f801814)
    {
        SY_ASSERT(0);
    }
    else if (addr >= 0x1f801c00 && addr < 0x1f801d80)
    {
        result = U16FromPtr(cpu->spu.voice.regs + (addr - SPU_START));
        //printf("SPUVOICE read result: %u\n", result);
    }
    else if (addr >= 0x1f801d80 && addr < 0x1f801dbc)
    {
        result = U16FromPtr(cpu->spu.control.regs + (addr - 0x1f801d80));
        //printf("SPUCTRL read result: %u\n", result);
    }
    else if (addr >= 0x1F802000 && addr < 0x1F804000)
    { result = 0xffff; } // exp region 2
    else if (addr >= 0x1FA00000 && addr < 0x1FC00000)
    { result = 0xffff; } // exp region 3
    else if (addr >= 0x1FC00000 && addr < 0x1FC80000)
    { result = U16FromPtr(cpu->bios + (addr - 0x1fc00000)); }
    else if (vaddr >= 0xfffe0130 && vaddr < 0xfffe0134)
    { result = U16FromPtr(&cpu->cachectrl + (vaddr - 0xfffe0130)); }
    else
    { printf("Unknown address mapping at: %08x\n", vaddr); result = 0xffff; }

    return (u16)result;
}

static u8 read8(struct cpu_state* cpu, u32 vaddr)
{
    u8 result = 0;

    u32 addr = vaddr & 0x1fffffff;

    if (addr < 0x200000)
    { result = U8FromPtr(cpu->ram + addr); }
    else if (addr >= 0x1f000000 && addr < 0x1f800000) // expansion region 1
    { result = 0xff; }
    else if (addr >= 0x1f800000 && addr < 0x1f800400)
    { result = U8FromPtr(cpu->scratch + (addr & 0x3ff)); }
    else if (addr >= 0x1f801000 && addr < 0x1f801024) // memory control 1
    { printf("Unhandled %s in MEMCTRL1 at %08x\n", __FUNCTION__, addr); }
    else if (addr >= 0x1f801040 && addr < 0x1f801050) // peripheral I/O
    {  
        result = U8FromPtr(cpu->peripheral + (addr - JOYPAD_START));
    }
    else if (addr >= 0x1f801050 && addr < 0x1f801060)
    {
        printf("Unhandled %s in SIO at %08x\n", __FUNCTION__, addr);
    }
    else if (addr >= 0x1f801060 && addr < 0x1f801062) // memory control 2 NOTE: not sure if this is 4 or 2 in length
    {
        printf("Unhandled %s in MEMCTRL2 at %08x\n", __FUNCTION__, addr); 
    }
    else if (addr >= 0x1f801070 && addr < 0x1f801078)
    {
        printf("Unhandled %s in IRQ_CTRL at %08x\n", __FUNCTION__, addr);
    }
    else if (addr >= 0x1f801080 && addr < 0x1f8010f8)
    {
        // handle DMA read
        //result = U8FromPtr(cpu->dma + (addr - DMA_CHANNEL_START));
        SY_ASSERT(0);
    }
    else if (addr >= 0x1f801100 && addr < 0x1f801130) // timers
    {
        SY_ASSERT(0);
    }
    else if (addr >= 0x1f801800 && addr < 0x1f801804)
    {
        u32 offset = addr - CDROM_START;
        result = cdrom_read(&cpu->cdrom, offset);
    }
    else if (addr == 0x1f801810)
    {
        SY_ASSERT(0);
    }
    else if (addr == 0x1f801814)
    {
        SY_ASSERT(0);
    }
    else if (addr >= 0x1f801c00 && addr < 0x1f802000)
    {
        //result = U8FromPtr(cpu->spu + (addr - SPU_START));
        //printf("Unhandled %s in SPU at: %08x\n", __FUNCTION__, addr);
    }
    else if (addr >= 0x1F802000 && addr < 0x1F804000)
    { result = 0xff; } // exp region 2
    else if (addr >= 0x1FA00000 && addr < 0x1FC00000)
    { result = 0xff; } // exp region 3
    else if (addr >= 0x1FC00000 && addr < 0x1FC80000)
    { result = U8FromPtr(cpu->bios + (addr - 0x1fc00000)); }
    else if (vaddr >= 0xfffe0130 && vaddr < 0xfffe0134)
    { result = U8FromPtr(&cpu->cachectrl + (vaddr - 0xfffe0130)); }
    else
    { printf("Unknown address mapping at: %08x\n", vaddr); }

    return result;
}

static void store32(struct cpu_state* cpu, u32 vaddr, u32 value)
{
    u32 addr = vaddr & 0x1fffffff;

    if (addr < 0x200000)
    { U32FromPtr(cpu->ram + addr) = value; }
    else if (addr >= 0x1F000000 && addr < 0x1F800000)
    { NoImplementation; }
    else if (addr >= 0x1F800000 && addr < 0x1F800400)
    { U32FromPtr(cpu->scratch + (addr & 0x3ff)) = value; }
    else if (addr >= 0x1f801000 && addr < 0x1f801024) // memory control 1
    { NoImplementation; }
    else if (addr >= 0x1f801040 && addr < 0x1f801060) // peripheral I/O
    { printf("Unhandled %s in PERIPHERAL at %08x\n", __FUNCTION__, addr); }
    else if (addr >= 0x1f801060 && addr < 0x1f801062) // memory control 2 NOTE: not sure if this is 4 or 2 in length
    { NoImplementation; }
    else if (addr >= 0x1f801070 && addr < 0x1f801078)
    {
        switch (addr - IRQ_START)
        {
        case 0x0:
            cpu->i_stat &= value & 0x7ff;
            break;
        case 0x4:
            cpu->i_mask = value & 0x7ff;
            break;
        default:
            printf("Unhandled write offset in IRQ\n");
            break;
        }
    }
    else if (addr >= 0x1f801080 && addr < 0x1f8010f8)
    {
        // handle DMA write
        //U32FromPtr(cpu->dma + (addr - DMA_CHANNEL_START)) = value;
        u32 offset = addr - DMA_CHANNEL_START;
        dma_write(cpu, offset, value);
    }
    else if (addr >= 0x1f801100 && addr < 0x1f801130) // timers
    {
        timer_store(cpu, addr - TIMERS_START, value);
    }
    else if (addr >= 0x1f801800 && addr < 0x1f801804)
    {
        printf("Unhandled %s in CDROM at: %08x\n", __FUNCTION__, addr);
    }
    else if (addr == 0x1f801810)
    {
        execute_gp0_command(&cpu->gpu, value);
    }
    else if (addr == 0x1f801814)
    {
        execute_gp1_command(&cpu->gpu, value);
    }
    else if (addr >= 0x1f801c00 && addr < 0x1f802000)
    {
        //U32FromPtr(cpu->spu + (addr - SPU_START)) = value;
        //printf("Unhandled %s in SPU at: %08x\n", __FUNCTION__, addr);
    }
    else if (addr >= 0x1F802000 && addr < 0x1F804000)
    {  } // exp region 2
    else if (addr >= 0x1FA00000 && addr < 0x1FC00000)
    {  } // exp region 3
    else if (addr >= 0x1FC00000 && addr < 0x1FC80000)
    { U32FromPtr(cpu->bios + (addr - 0x1fc00000)) = value; }
    else if (vaddr >= 0xfffe0130 && vaddr < 0xfffe0134)
    { cpu->cachectrl = value; }
    else
    { printf("Unknown address mapping at: %08x\n", vaddr); }
}

static void store16(struct cpu_state* cpu, u32 vaddr, u32 value)
{
    u32 addr = vaddr & 0x1fffffff;

    if (addr < 0x200000)
    { U16FromPtr(cpu->ram + addr) = value; }
    else if (addr >= 0x1F000000 && addr < 0x1F800000) // exp 1
    { NoImplementation; }
    else if (addr >= 0x1F800000 && addr < 0x1F800400)
    { U16FromPtr(cpu->scratch + (addr & 0x3ff)) = value; }
    else if (addr >= 0x1f801000 && addr < 0x1f801024) // memory control 1
    { NoImplementation; }
    else if (addr >= 0x1f801040 && addr < 0x1f801050) // peripheral I/O
    {  
        U16FromPtr(cpu->peripheral + (addr - JOYPAD_START)) = value;
    }
    else if (addr >= 0x1f801050 && addr < 0x1f801060)
    {
        printf("Unhandled %s in SIO at %08x\n", __FUNCTION__, addr);
    }
    else if (addr >= 0x1f801060 && addr < 0x1f801062) // memory control 2 NOTE: not sure if this is 4 or 2 in length
    { NoImplementation; }
    else if (addr >= 0x1f801070 && addr < 0x1f801078)
    {
        switch (addr - IRQ_START)
        {
        case 0x0:
            cpu->i_stat &= value & 0x7ff;
            break;
        case 0x4:
            cpu->i_mask = value & 0x7ff;
            break;
        default:
            printf("Unhandled write offset in IRQ\n");
            break;
        }
    }
    else if (addr >= 0x1f801080 && addr < 0x1f8010f8)
    {
        printf("Unexpected store16 at DMA\n");
    }
    else if (addr >= 0x1f801100 && addr < 0x1f801130) // timers
    {
        timer_store(cpu, addr - TIMERS_START, value);
    }
    else if (addr >= 0x1f801800 && addr < 0x1f801804)
    {
        printf("Unhandled %s in CDROM at: %08x\n", __FUNCTION__, addr);
    }
    else if (addr == 0x1f801810)
    {
        SY_ASSERT(0);
        // gp0 command here
        execute_gp0_command(&cpu->gpu, value);
    }
    else if (addr == 0x1f801814)
    {
        SY_ASSERT(0);
        // gp1 command here
        execute_gp1_command(&cpu->gpu, value);
    }
    else if (addr >= 0x1f801c00 && addr < 0x1f801d80)
    {
        //result = U16FromPtr(cpu->spu + (addr - SPU_START));
        //printf("Unhandled %s in SPU at: %08x\n", __FUNCTION__, addr);
        U16FromPtr(cpu->spu.voice.regs + (addr - SPU_START)) = value;
        //printf("SPUVOICE store at %08x: %u\n", addr, value);
    }
    else if (addr >= 0x1f801d80 && addr < 0x1f801dbc)
    {
        U16FromPtr(cpu->spu.control.regs + (addr - 0x1f801d80)) = value;
        //printf("SPUCTRL store at %08x: %u\n", addr, value);
    }
    else if (addr >= 0x1F802000 && addr < 0x1F804000)
    {  } // exp region 2
    else if (addr >= 0x1FA00000 && addr < 0x1FC00000)
    {  } // exp region 3
    else if (addr >= 0x1FC00000 && addr < 0x1FC80000)
    { U16FromPtr(cpu->bios + (addr - 0x1fc00000)) = value; }
    else if (vaddr >= 0xfffe0130 && vaddr < 0xfffe0134)
    { U16FromPtr(&cpu->cachectrl + (vaddr - 0xfffe0130)) = value; }
    else
    { printf("Unknown address mapping at: %08x\n", vaddr); }
}

static void store8(struct cpu_state* cpu, u32 vaddr, u32 value)
{
    u32 addr = vaddr & 0x1fffffff;

    if (addr < 0x200000)
    { U8FromPtr(cpu->ram + addr) = value; }
    else if (addr >= 0x1F000000 && addr < 0x1F800000)
    { NoImplementation; }
    else if (addr >= 0x1F800000 && addr < 0x1F800400)
    { U8FromPtr(cpu->scratch + (addr & 0x3ff)) = value; }
    else if (addr >= 0x1f801000 && addr < 0x1f801024) // memory control 1
    { NoImplementation; }
    else if (addr >= 0x1f801040 && addr < 0x1f801050) // peripheral I/O
    {
        U8FromPtr(cpu->peripheral + (addr - JOYPAD_START)) = value;
    }
    else if (addr >= 0x1f801050 && addr < 0x1f801060)
    {
        printf("Unhandled %s in SIO at %08x\n", __FUNCTION__, addr);
    }
    else if (addr >= 0x1f801060 && addr < 0x1f801062) // memory control 2 NOTE: not sure if this is 4 or 2 in length
    { NoImplementation; }
    else if (addr >= 0x1f801070 && addr < 0x1f801078)
    {
        printf("Unhandled %s at %08x\n", __FUNCTION__, addr);
    }
    else if (addr >= 0x1f801080 && addr < 0x1f8010f8)
    {
        SY_ASSERT(0);
    }
    else if (addr >= 0x1f801100 && addr < 0x1f801130) // timers
    {
        SY_ASSERT(0);
    }
    else if (addr >= 0x1f801800 && addr < 0x1f801804)
    {
        u32 offset = addr - CDROM_START;
        cdrom_store(cpu, offset, value);
    }
    else if (addr == 0x1f801810)
    {
        SY_ASSERT(0);
        // gp0 command here
        execute_gp0_command(&cpu->gpu, value);
    }
    else if (addr == 0x1f801814)
    {
        SY_ASSERT(0);
        // gp1 command here
        execute_gp1_command(&cpu->gpu, value);
    }
    else if (addr >= 0x1f801c00 && addr < 0x1f802000)
    {
        //U8FromPtr(cpu->spu + (addr - SPU_START)) = value;
        //printf("Unhandled %s in SPU at: %08x\n", __FUNCTION__, addr);
    }
    else if (addr >= 0x1F802000 && addr < 0x1F804000)
    {  } // exp region 2
    else if (addr >= 0x1FA00000 && addr < 0x1FC00000)
    {  } // exp region 3
    else if (addr >= 0x1FC00000 && addr < 0x1FC80000)
    { U8FromPtr(cpu->bios + (addr - 0x1fc00000)) = value; }
    else if (vaddr >= 0xfffe0130 && vaddr < 0xfffe0134)
    { U8FromPtr(&cpu->cachectrl + (vaddr - 0xfffe0130)) = value; }
    else
    { printf("Unknown address mapping at: %08x\n", vaddr); }
}