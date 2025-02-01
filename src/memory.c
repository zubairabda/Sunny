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
        else if (addr >= 0x1f801c00 && addr < 0x1f801fff)
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
}
