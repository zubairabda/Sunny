#include "memory.h"
#include "event.h"
#include "cpu.h"
#include "counters.h"
#include "spu.h"
#include "cdrom.h"
#include "gpu.h"
#include "dma.h"
#include "debug.h"
#include "sio.h"

#define DELAY_CYCLES(cycles) g_cycles_elapsed += (cycles)
//#define DELAY_CYCLES(cycles)
//#define debug_log

u8 *g_bios;
u8 *g_ram;
u8 *g_scratch;
u8 *g_peripheral;

enum address_mapping
{
    RAM_START = 0x0,
    BIOS_START = 0x1fc00000,
    JOYPAD_START = 0x1f801040,
    IRQ_START = 0x1f801070,
    DMA_CHANNEL_START = 0x1f801080,
    DMA_REGISTER_START = 0x1f8010f0,
    TIMERS_START = 0x1f801100,
    GPU_START = 0x1f801810,
    SPU_REGS_START = 0x1f801c00,
    SPU_CONTROL_START = 0x1f801d80,
    SPU_REVERB_START = 0x1f801dc0,
    CDROM_START = 0x1f801800
};

static u32 io_read32(u32 addr)
{
    switch (addr)
    {
    case 0x1f801070:
        return g_cpu.i_stat;
    case 0x1f801074:
        return g_cpu.i_mask;
    case 0x1f801810:
        return gpuread();
    case 0x1f801814:
        return g_gpu.stat.value & ~(1 << 19);
    default:
        if (addr >= 0x1f801080 && addr < 0x1f8010f8)
        {
            return dma_read(addr & 0x7f);
        }
        else if (addr >= 0x1f801100 && addr < 0x1f801130)
        {
            return counters_read(addr & 0x3f);
        }
        debug_log("Unknown 32-bit read address: %08x\n", addr);
        return 0;
    }
}

static u16 io_read16(u32 addr)
{
    switch (addr)
    {
    case 0x1f801070:
        return (u16)g_cpu.i_stat;
    case 0x1f801074:
        return (u16)g_cpu.i_mask;
    default:
        if (addr >= 0x1f801100 && addr < 0x1f801130)
        {
            DELAY_CYCLES(2);
            return counters_read(addr & 0x3f);
        }
        else if (addr >= 0x1f801c00 && addr < 0x1f801e00)
        {
            DELAY_CYCLES(18);
            return spu_read(addr & 0x3ff);
        }
        else if (addr >= 0x1f801040 && addr < 0x1f801050)
        {
            DELAY_CYCLES(3);
            return sio_read(addr & 0x1f);
        }
        debug_log("Unknown 16-bit read address: %08x\n", addr);
        return 0;
    }
}

static u8 io_read8(u32 addr)
{
    switch (addr)
    {
    default:
        if (addr >= 0x1f801800 && addr < 0x1f801804)
        {
            return cdrom_read(addr & 0x3);
        }
        else if (addr >= 0x1f801040 && addr < 0x1f801050)
        {
            return sio_read(addr & 0x1f);
        }
        debug_log("Unknown 8-bit read address: %08x\n", addr);
        return 0;
    }
}

static void io_write32(u32 addr, u32 value)
{
    switch (addr)
    {
    case 0x1f801070:
        g_cpu.i_stat &= value & 0x7ff;
        break;
    case 0x1f801074:
        g_cpu.i_mask = value & 0x7ff;
        break;
    case 0x1f801810:
        execute_gp0_command(value);
        break;
    case 0x1f801814:
        execute_gp1_command(value);
        break;
    default:
        if (addr >= 0x1f801000 && addr < 0x1f801024)
            debug_log("Unhandled 32-bit store to Memory Control 1\n");
        else if (addr >= 0x1f801080 && addr < 0x1f8010f8)
            dma_write(addr & 0x7f, value);
        else if (addr >= 0x1f801100 && addr < 0x1f801130)
            counters_store(addr & 0x3f, value);
        else if (addr >= 0x1f801c00 && addr < 0x1f801e00)
            spu_write(addr & 0x3ff, value);
        else
        {
            debug_log("Unknown 32-bit store address: %08x\n", addr);
        }
        break;
    }
}

static void io_write16(u32 addr, u32 value)
{
    switch (addr)
    {
    case 0x1f801070:
        g_cpu.i_stat &= value & 0x7ff;
        break;
    case 0x1f801074:
        g_cpu.i_mask = value & 0x7ff;
        break;
    default:
        if (addr >= 0x1f801100 && addr < 0x1f801130)
            counters_store(addr & 0x3f, value);
        else if (addr >= 0x1f801c00 && addr < 0x1f801e00)
            spu_write(addr & 0x3ff, value);
        else if (addr >= 0x1f801040 && addr < 0x1f801050)
            sio_store(addr & 0x1f, value);
        else
            debug_log("Unknown 16-bit store address: %08x\n", addr);
        break;
    }
}

static void io_write8(u32 addr, u32 value)
{
    switch (addr)
    {
    default:
        if (addr >= 0x1f801800 && addr < 0x1f801804)
            cdrom_store(addr & 0x3, value);
        else if (addr >= 0x1f801040 && addr < 0x1f801050)
            sio_store(addr & 0x1f, value);
        else
            debug_log("Unknown 8-bit store address: %08x\n", addr);
        break;
    }
}

void *mem_read(u32 addr)
{
    void *result = NULL;
    if (addr < 0x800000)
    {
        DELAY_CYCLES(4);
        result = g_ram + (addr & 0x1fffff);
    }
    else if (addr >= 0x1f000000 && addr < 0x1f800000)
    {
        debug_log("Unhandled read from EXP 1\n");
    }
    else if (addr >= 0x1f800000 && addr < 0x1f800400)
    {
        result = g_scratch + (addr & 0x3ff);
    }
    else if (addr >= 0x1fc00000 && addr < 0x1fc80000)
    {
        //DELAY_CYCLES(15);
        result = g_bios + (addr & 0x7ffff);
    }
    return result;
}

void *mem_write(u32 addr)
{
    void *result = NULL;
    if (addr < 0x800000)
    {
        result = g_ram + (addr & 0x1fffff);
    }
    else if (addr >= 0x1f000000 && addr < 0x1f800000)
    {
        debug_log("Unhandled read from EXP 1\n");
    }
    else if (addr >= 0x1f800000 && addr < 0x1f800400)
    {
        result = g_scratch + (addr & 0x3ff);
    }
    else if (addr >= 0x1fc00000 && addr < 0x1fc80000)
    {
        result = g_bios + (addr & 0x7ffff);
    }
    return result;
}

u32 load32(u32 vaddr)
{
    u32 addr = vaddr & 0x1fffffff;
    void *mem = mem_read(addr);

    if (mem)
    {
        return U32FromPtr(mem);
    }
    else
    {
        return io_read32(addr);
    }
#if 0
    // if (addr )
    // return U32FromPtr(mem_read(psx, addr));
    if (addr < 0x200000)
    { result = U32FromPtr(psx->ram + addr); }
    else if (addr >= 0x1f000000 && addr < 0x1f800000)
    { result = 0xffffffff; }
    else if (addr >= 0x1f800000 && addr < 0x1f800400)
    { result = U32FromPtr(psx->scratch + (addr & 0x3ff)); }
    else if (addr >= 0x1f801000 && addr < 0x1f801024) // memory control 1
    { result = 0xffffffff; }
    else if (addr >= 0x1f801040 && addr < 0x1f801060) // peripheral I/O
    { debug_log("Unhandled %s in PERIPHERAL at %08x\n", __FUNCTION__, addr); result = 0xffffffff; }
    else if (addr >= 0x1f801060 && addr < 0x1f801062) // memory control 2 NOTE: not sure if this is 4 or 2 in length
    { result = 0xffffffff; }
    else if (addr >= 0x1f801070 && addr < 0x1f801078)
    {
        result = irq_read(psx, addr - IRQ_START);
    }
    else if (addr >= 0x1f801080 && addr < 0x1f8010f8)
    {
        // handle DMA read
        //result = U32FromPtr(cpu->dma + (addr - DMA_CHANNEL_START));
        u32 offset = addr - DMA_CHANNEL_START;
        result = dma_read(psx, offset);
    }
    else if (addr >= 0x1f801100 && addr < 0x1f801130) // timers
    {
        result = timer_read(psx, addr - TIMERS_START);
    }
    else if (addr == 0x1f801800)
    {
        result = (psx->cdrom->status * 0x01010101); // ref: nocash
    }
    else if (addr == 0x1f801810)
    {
        result = gpuread(&psx->gpu);
    }
    else if (addr == 0x1f801814)
    {
        result = psx->gpu->stat.value; // GPUSTAT
    }
    else if (addr >= 0x1f801c00 && addr < 0x1f802000)
    {
        //result = U32FromPtr(cpu->spu + (addr - SPU_START));
        //debug_log("Unhandled %s in SPU at: %08x\n", __FUNCTION__, addr);
        result = 0xffffffff;
    }
    else if (addr >= 0x1F802000 && addr < 0x1F804000)
    { result = 0xffffffff; } // exp region 2
    else if (addr >= 0x1FA00000 && addr < 0x1FC00000)
    { result = 0xffffffff; } // exp region 3
    else if (addr >= 0x1FC00000 && addr < 0x1FC80000) { 
        result = U32FromPtr(psx->bios + (addr - 0x1fc00000));
    }
    else if (vaddr >= 0xfffe0130 && vaddr < 0xfffe0134)
    { result = psx->cachectrl; }
    else
    { debug_log("Unknown address mapping at: %08x\n", vaddr); result = 0xffffffff; }
#endif
    //return result;
}

u16 load16(u32 vaddr)
{
    u32 addr = vaddr & 0x1fffffff;
    void *mem = mem_read(addr);

    if (mem)
    {
        return U16FromPtr(mem);
    }
    else
    {
        return io_read16(addr);
    }
#if 0
    u16 result = 0;

    u32 addr = vaddr & 0x1fffffff;

    if (addr < 0x200000)
    { result = U16FromPtr(psx->ram + addr); }
    else if (addr >= 0x1f000000 && addr < 0x1f800000) // exp region 1
    { result = 0xffff; }
    else if (addr >= 0x1f800000 && addr < 0x1f800400)
    { result = U16FromPtr(psx->scratch + (addr & 0x3ff)); }
    else if (addr >= 0x1f801000 && addr < 0x1f801024) // memory control 1
    { result = 0xffff; }
    else if (addr >= 0x1f801040 && addr < 0x1f801050) // peripheral I/O
    {
        u32 offset = addr - JOYPAD_START;
        result = joypad_read(psx, offset);
    }
    else if (addr >= 0x1f801050 && addr < 0x1f801060)
    {
        debug_log("Unhandled %s in SIO at %08x\n", __FUNCTION__, addr);
    }
    else if (addr >= 0x1f801060 && addr < 0x1f801062) // memory control 2 NOTE: not sure if this is 4 or 2 in length
    { result = 0xffff; }
    else if (addr >= 0x1f801070 && addr < 0x1f801078)
    {
        result = irq_read(psx, addr - IRQ_START);
    }
    else if (addr >= 0x1f801080 && addr < 0x1f8010f8)
    {
        // handle DMA read
        //result = U16FromPtr(cpu->dma + (addr - DMA_CHANNEL_START));
        SY_ASSERT(0);
    }
    else if (addr >= 0x1f801100 && addr < 0x1f801130) // timers
    {
        //debug_log("read16 in timers: %08x\n", addr);
        //result = 0xffff;
        result = timer_read(psx, addr - TIMERS_START);
    }
    else if (addr >= 0x1f801800 && addr < 0x1f801804)
    {
        debug_log("Unhandled %s in CDROM at: %08x\n", __FUNCTION__, addr);
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
        result = U16FromPtr(psx->spu.voice.regs + (addr - SPU_REGS_START));
        //debug_log("SPU voice #%d, register #%d, read: %d\n", ((addr - 0x1f801c00) >> 4), ((addr - 0x1f801c00) & 0xf) >> 1, result);
    }
    else if (addr >= 0x1f801d80 && addr < 0x1f801dbc)
    {
        result = spu_read(psx, addr - SPU_CONTROL_START);
        //debug_log("[%08x] SPUCTRL read: %u\n", addr, result);
    }
    else if (addr >= 0x1f801dc0 && addr < 0x1f801dff)
    {
        SY_ASSERT(!(addr & 0x1));
        result = U16FromPtr(psx->spu.reverb.regs + (addr - SPU_REVERB_START));
        debug_log("SPU reverb read from: %08x -> %hu\n", addr, result);
    }
    else if (addr >= 0x1F802000 && addr < 0x1F804000)
    { result = 0xffff; } // exp region 2
    else if (addr >= 0x1FA00000 && addr < 0x1FC00000)
    { result = 0xffff; } // exp region 3
    else if (addr >= 0x1FC00000 && addr < 0x1FC80000)
    { result = U16FromPtr(psx->bios + (addr - 0x1fc00000)); }
    else if (vaddr >= 0xfffe0130 && vaddr < 0xfffe0134)
    { result = U16FromPtr(&psx->cachectrl + (vaddr - 0xfffe0130)); }
    else
    { debug_log("Unknown address mapping at: %08x\n", vaddr); result = 0xffff; }

    return (u16)result;
#endif
}

u8 load8(u32 vaddr)
{
    u32 addr = vaddr & 0x1fffffff;
    void *mem = mem_read(addr);

    if (mem)
    {
        return U8FromPtr(mem);
    }
    else
    {
        return io_read8(addr);
    }
#if 0
    u8 result = 0;

    u32 addr = vaddr & 0x1fffffff;

    if (addr < 0x200000)
    { result = U8FromPtr(psx->ram + addr); }
    else if (addr >= 0x1f000000 && addr < 0x1f800000) // expansion region 1
    { result = 0xff; }
    else if (addr >= 0x1f800000 && addr < 0x1f800400)
    { result = U8FromPtr(psx->scratch + (addr & 0x3ff)); }
    else if (addr >= 0x1f801000 && addr < 0x1f801024) // memory control 1
    { debug_log("Unhandled %s in MEMCTRL1 at %08x\n", __FUNCTION__, addr); }
    else if (addr >= 0x1f801040 && addr < 0x1f801050) // peripheral I/O
    {
        u32 offset = addr - JOYPAD_START;
        result = joypad_read(psx, offset);
    }
    else if (addr >= 0x1f801050 && addr < 0x1f801060)
    {
        debug_log("Unhandled %s in SIO at %08x\n", __FUNCTION__, addr);
    }
    else if (addr >= 0x1f801060 && addr < 0x1f801062) // memory control 2 NOTE: not sure if this is 4 or 2 in length
    {
        debug_log("Unhandled %s in MEMCTRL2 at %08x\n", __FUNCTION__, addr); 
    }
    else if (addr >= 0x1f801070 && addr < 0x1f801078)
    {
        debug_log("Unhandled %s in IRQ_CTRL at %08x\n", __FUNCTION__, addr);
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
        result = cdrom_read(&psx->cdrom, offset);
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
        //debug_log("Unhandled %s in SPU at: %08x\n", __FUNCTION__, addr);
    }
    else if (addr >= 0x1F802000 && addr < 0x1F804000)
    { result = 0xff; } // exp region 2
    else if (addr >= 0x1FA00000 && addr < 0x1FC00000)
    { result = 0xff; } // exp region 3
    else if (addr >= 0x1FC00000 && addr < 0x1FC80000)
    { result = U8FromPtr(psx->bios + (addr - 0x1fc00000)); }
    else if (vaddr >= 0xfffe0130 && vaddr < 0xfffe0134)
    { result = U8FromPtr(&psx->cachectrl + (vaddr - 0xfffe0130)); }
    else
    { debug_log("Unknown address mapping at: %08x\n", vaddr); }

    return result;
#endif
}

void store32(u32 vaddr, u32 value)
{
    u32 addr = vaddr & 0x1fffffff;
    void *mem = mem_write(addr);

    if (mem)
    {
        U32FromPtr(mem) = value;
    }
    else
    {
        io_write32(addr, value);
    }
#if 0
    u32 addr = vaddr & 0x1fffffff;

    if (addr < 0x200000)
    { U32FromPtr(psx->ram + addr) = value; }
    else if (addr >= 0x1F000000 && addr < 0x1F800000)
    { NoImplementation; }
    else if (addr >= 0x1F800000 && addr < 0x1F800400)
    { U32FromPtr(psx->scratch + (addr & 0x3ff)) = value; }
    else if (addr >= 0x1f801000 && addr < 0x1f801024) // memory control 1
    { NoImplementation; }
    else if (addr >= 0x1f801040 && addr < 0x1f801060) // peripheral I/O
    { debug_log("Unhandled %s in PERIPHERAL at %08x\n", __FUNCTION__, addr); }
    else if (addr >= 0x1f801060 && addr < 0x1f801062) // memory control 2 NOTE: not sure if this is 4 or 2 in length
    { NoImplementation; }
    else if (addr >= 0x1f801070 && addr < 0x1f801078)
    {
        switch (addr - IRQ_START)
        {
        case 0x0:
            psx->i_stat &= value & 0x7ff;
            break;
        case 0x4:
            psx->i_mask = value & 0x7ff;
            break;
        default:
            debug_log("Unhandled write offset in IRQ\n");
            break;
        }
    }
    else if (addr >= 0x1f801080 && addr < 0x1f8010f8)
    {
        // handle DMA write
        //U32FromPtr(cpu->dma + (addr - DMA_CHANNEL_START)) = value;
        u32 offset = addr - DMA_CHANNEL_START;
        dma_write(psx, offset, value);
    }
    else if (addr >= 0x1f801100 && addr < 0x1f801130) // timers
    {
        timer_store(psx, addr - TIMERS_START, value);
    }
    else if (addr >= 0x1f801800 && addr < 0x1f801804)
    {
        debug_log("Unhandled %s in CDROM at: %08x\n", __FUNCTION__, addr);
    }
    else if (addr == 0x1f801810)
    {
        execute_gp0_command(&psx->gpu, value);
    }
    else if (addr == 0x1f801814)
    {
        execute_gp1_command(psx, value);
    }
    else if (addr >= 0x1f801c00 && addr < 0x1f801d80)
    {
        SY_ASSERT(!(addr & 0x1));
        // TODO: not sure if this is the correct behavior
        u32 offset = addr - SPU_REGS_START;
        U16FromPtr(psx->spu.voice.regs + offset) = value;
        U16FromPtr(psx->spu.voice.regs + offset + 2) = value >> 16;
        int v = (addr - 0x1f801c00) >> 4;
        int reg = ((addr - 0x1f801c00) & 0xf) >> 1;
        debug_log("SPU voice #%d, register #%d, store: %d\n", v, reg, value);
        if (reg == 6)
            DebugBreak();
    }
    else if (addr >= 0x1f801d80 && addr < 0x1f801dbc)
    {
        u32 offset = addr - SPU_CONTROL_START;
        spu_write(psx, offset, value);
        spu_write(psx, offset + 2, value >> 16);
    }
    else if (addr >= 0x1f801dc0 && addr < 0x1f801dff)
    {
        SY_ASSERT(!(addr & 0x1));
        u32 offset = addr - SPU_REVERB_START;
        U16FromPtr(psx->spu.reverb.regs + offset) = value;
        U16FromPtr(psx->spu.reverb.regs + offset + 2) = (value >> 16);
        debug_log("SPU reverb write to: %08x <- %hu\n", addr, value);
    }
    else if (addr >= 0x1F802000 && addr < 0x1F804000)
    {  } // exp region 2
    else if (addr >= 0x1FA00000 && addr < 0x1FC00000)
    {  } // exp region 3
    else if (addr >= 0x1FC00000 && addr < 0x1FC80000)
    { U32FromPtr(psx->bios + (addr - 0x1fc00000)) = value; }
    else if (vaddr >= 0xfffe0130 && vaddr < 0xfffe0134)
    { psx->cachectrl = value; }
    else
    { debug_log("Unknown address mapping at: %08x\n", vaddr); }
#endif
}

void store16(u32 vaddr, u32 value)
{
    u32 addr = vaddr & 0x1fffffff;
    void *mem = mem_write(addr);

    if (mem)
    {
        U16FromPtr(mem) = value;
    }
    else
    {
        io_write16(addr, value);
    }
#if 0
    u32 addr = vaddr & 0x1fffffff;

    if (addr < 0x200000)
    { U16FromPtr(psx->ram + addr) = value; }
    else if (addr >= 0x1F000000 && addr < 0x1F800000) // exp 1
    { NoImplementation; }
    else if (addr >= 0x1F800000 && addr < 0x1F800400)
    { U16FromPtr(psx->scratch + (addr & 0x3ff)) = value; }
    else if (addr >= 0x1f801000 && addr < 0x1f801024) // memory control 1
    { NoImplementation; }
    else if (addr >= 0x1f801040 && addr < 0x1f801050) // peripheral I/O
    {
        u32 offset = addr - JOYPAD_START;
        joypad_store(psx, offset, value);
    }
    else if (addr >= 0x1f801050 && addr < 0x1f801060)
    {
        debug_log("Unhandled %s in SIO at %08x\n", __FUNCTION__, addr);
    }
    else if (addr >= 0x1f801060 && addr < 0x1f801062) // memory control 2 NOTE: not sure if this is 4 or 2 in length
    { NoImplementation; }
    else if (addr >= 0x1f801070 && addr < 0x1f801078)
    {
        switch (addr - IRQ_START)
        {
        case 0x0:
            psx->i_stat &= value & 0x7ff;
            break;
        case 0x4:
            psx->i_mask = value & 0x7ff;
            break;
        default:
            debug_log("Unhandled write offset in IRQ\n");
            break;
        }
    }
    else if (addr >= 0x1f801080 && addr < 0x1f8010f8)
    {
        debug_log("Unexpected store16 at DMA\n");
        SY_ASSERT(0);
    }
    else if (addr >= 0x1f801100 && addr < 0x1f801130) // timers
    {
        timer_store(psx, addr - TIMERS_START, value);
    }
    else if (addr >= 0x1f801800 && addr < 0x1f801804)
    {
        debug_log("Unhandled %s in CDROM at: %08x\n", __FUNCTION__, addr);
    }
    else if (addr == 0x1f801810)
    {
        SY_ASSERT(0);
        // gp0 command here
        execute_gp0_command(&psx->gpu, value);
    }
    else if (addr == 0x1f801814)
    {
        SY_ASSERT(0);
    }
    else if (addr >= 0x1f801c00 && addr < 0x1f801d80)
    {
        SY_ASSERT(!(addr & 0x1));
        
        #ifdef SY_DEBUG
        char *debug_spu_reg_table[] = {"Volume left", "Volume right", "Sample rate", "Start address", "ADSR lo", "ADSR hi", "ADSR volume", "Repeat address"};
        #endif

        U16FromPtr(psx->spu.voice.regs + (addr - SPU_REGS_START)) = value;
        int v = (addr - 0x1f801c00) >> 4;
        int reg = ((addr - 0x1f801c00) & 0xf) >> 1;
        //debug_log("SPU voice #%d, %s <- %d\n", v, debug_spu_reg_table[reg], value);
    }
    else if (addr >= 0x1f801d80 && addr < 0x1f801dbc)
    {
        //U16FromPtr(cpu->spu.control.regs + (addr - 0x1f801d80)) = value;
        spu_write(psx, (addr - SPU_CONTROL_START), value);
        //debug_log("SPUCTRL store at %08x: %u\n", addr, value);
    }
    else if (addr >= 0x1f801dc0 && addr < 0x1f801dff)
    {
        SY_ASSERT(!(addr & 0x1));
        U16FromPtr(psx->spu.reverb.regs + (addr - SPU_REVERB_START)) = value;
        debug_log("SPU reverb write to: %08x <- %hu\n", addr, value);
    }
    else if (addr >= 0x1F802000 && addr < 0x1F804000)
    {  } // exp region 2
    else if (addr >= 0x1FA00000 && addr < 0x1FC00000)
    {  } // exp region 3
    else if (addr >= 0x1FC00000 && addr < 0x1FC80000)
    { U16FromPtr(psx->bios + (addr - 0x1fc00000)) = value; }
    else if (vaddr >= 0xfffe0130 && vaddr < 0xfffe0134)
    { U16FromPtr(&psx->cachectrl + (vaddr - 0xfffe0130)) = value; }
    else
    { debug_log("Unknown address mapping at: %08x\n", vaddr); }
#endif
}

void store8(u32 vaddr, u32 value)
{
    u32 addr = vaddr & 0x1fffffff;
    void *mem = mem_write(addr);

    if (mem)
    {
        U8FromPtr(mem) = value;
    }
    else
    {
        io_write8(addr, value);
    }
#if 0
    u32 addr = vaddr & 0x1fffffff;

    if (addr < 0x200000)
    { U8FromPtr(psx->ram + addr) = value; }
    else if (addr >= 0x1F000000 && addr < 0x1F800000)
    { NoImplementation; }
    else if (addr >= 0x1F800000 && addr < 0x1F800400)
    { U8FromPtr(psx->scratch + (addr & 0x3ff)) = value; }
    else if (addr >= 0x1f801000 && addr < 0x1f801024) // memory control 1
    { NoImplementation; }
    else if (addr >= 0x1f801040 && addr < 0x1f801050) // peripheral I/O
    {
        u32 offset = addr - JOYPAD_START;
        joypad_store(psx, offset, value);
    }
    else if (addr >= 0x1f801050 && addr < 0x1f801060)
    {
        debug_log("Unhandled %s in SIO at %08x\n", __FUNCTION__, addr);
    }
    else if (addr >= 0x1f801060 && addr < 0x1f801062) // memory control 2 NOTE: not sure if this is 4 or 2 in length
    { NoImplementation; }
    else if (addr >= 0x1f801070 && addr < 0x1f801078)
    {
        debug_log("Unhandled %s at %08x\n", __FUNCTION__, addr);
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
        cdrom_store(psx, offset, value);
    }
    else if (addr == 0x1f801810)
    {
        SY_ASSERT(0);
        // gp0 command here
        execute_gp0_command(&psx->gpu, value);
    }
    else if (addr == 0x1f801814)
    {
        SY_ASSERT(0);
    }
    else if (addr >= 0x1f801c00 && addr < 0x1f802000)
    {
        SY_ASSERT(0);
        //U8FromPtr(cpu->spu + (addr - SPU_START)) = value;
        //debug_log("Unhandled %s in SPU at: %08x\n", __FUNCTION__, addr);
    }
    else if (addr >= 0x1F802000 && addr < 0x1F804000)
    {  } // exp region 2
    else if (addr >= 0x1FA00000 && addr < 0x1FC00000)
    {  } // exp region 3
    else if (addr >= 0x1FC00000 && addr < 0x1FC80000)
    { U8FromPtr(psx->bios + (addr - 0x1fc00000)) = value; }
    else if (vaddr >= 0xfffe0130 && vaddr < 0xfffe0134)
    { U8FromPtr(&psx->cachectrl + (vaddr - 0xfffe0130)) = value; }
    else
    { debug_log("Unknown address mapping at: %08x\n", vaddr); }
#endif
}
