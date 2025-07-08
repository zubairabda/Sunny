#include "psx.h"
#include "event.h"
#include "cpu.h"
#include "gpu.h"
#include "counters.h"
#include "cdrom.h"
#include "sio.h"
#include "dma.h"
#include "spu.h"
#include "memory.h"
#include "debug.h"

enum system_state g_state;

b8 psx_load_exe(platform_file *file)
{
    u64 fsize = platform_get_file_size(file);
    SY_ASSERT(fsize <= 0xffffffff);
    void *buffer = malloc(fsize);
    platform_read_file(file, 0, buffer, fsize);

    u8 *fp = buffer;
    if (memcmp(fp, "PS-X EXE", 8) != 0)
    {
        return false;
    }

    while (g_cpu.pc != 0x80030000)
        psx_step();

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

    free(buffer);

    g_cpu.next_pc = g_cpu.pc + 4;

    return true;
}

b8 psx_load_image(const char *path)
{
    psx_image_type type;
    if (string_ends_with_ignore_case(path, ".exe"))
    {
        platform_file exe;
        if (!platform_open_file(path, &exe))
            return false;
        psx_reset();
        psx_load_exe(&exe);
        platform_close_file(&exe);
        return true;
    }
    else if (string_ends_with_ignore_case(path, ".bin"))
    {
        type = BIN;
    }
    else if (string_ends_with_ignore_case(path, ".cue"))
    {
        type = CUE;
    }
    else
    {
        printf("Unrecognized file extension for file: %s\n", path);
        return false;
    }

    disk_image *disk = open_disk(path, type);
    if (disk)
    {
        psx_reset();
        cdrom_load_disk(disk);
        return true;
    }
    else
    {
        return false;
    }
}

void psx_reset(void)
{
    cpu_init();
    gpu_reset();
    dma_init();
    spu_reset();
    cdrom_reset();
    scheduler_reset();
    schedule_event(spu_tick, 0, 768, 768);
    // TODO: reschedule gpu events when display settings are changed
    s32 cycles_per_scanline = (s32)video_to_cpu_cycles(NTSC_VIDEO_CYCLES_PER_SCANLINE);
    schedule_event(gpu_scanline_complete, 0, cycles_per_scanline, cycles_per_scanline);
}

b8 psx_can_boot(void)
{
    return (g_bios != NULL); // TODO: validate bios
}

void psx_init(struct memory_arena *arena, void *bios)
{
    g_bios = bios;
    
    g_ram = push_arena(arena, MEGABYTES(2));
    g_scratch = push_arena(arena, KILOBYTES(1));

    g_gpu.copy_buffer = push_arena(arena, VRAM_SIZE);
    //g_gpu.readback_buffer = push_arena(arena, VRAM_SIZE);
    // set to NTSC timings by default
    g_gpu.vertical_timing = 263;
    g_gpu.horizontal_timing = NTSC_VIDEO_CYCLES_PER_SCANLINE;
    // make sure we set draw area on the first draw in case it wasnt set by the program
    g_gpu.draw_area_changed = true;

    g_peripheral = push_arena(arena, 32); // temp

    // initialize ram with known garbage debug value
    memset32(g_ram, 0xdeadbeef, RAM_SIZE / 4);

    g_spu.dram = push_arena(arena, KILOBYTES(512));

    g_sio.stat.tx_fifo_not_full = 1;
    g_sio.stat.tx_finished = 1;

    scheduler_init(arena);
}

b32 psx_run(void)
{
    b32 result = execute_instructions();
    tick_events();
    return result;
}

void psx_step(void)
{
    b8 value = g_debug.breakpoints_enabled; // NOTE: as a hack to prevent stopping on breakpoints, we disable them :(
    g_debug.breakpoints_enabled = false;
    g_target_cycles = g_cycles_elapsed + 1;
    execute_instructions();
    tick_events();
    g_debug.breakpoints_enabled = value;
}
