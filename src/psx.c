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
#include "stream.h"

struct psx_image
{
    platform_file file;
    psx_image_type type;
};

static struct psx_image mounted_image;
static struct psx_image loaded_image;

b8 psx_load_exe(platform_file *file)
{
    u64 fsize = platform_get_file_size(file);
    void *buffer = malloc(fsize);
    platform_read_file(file, 0, buffer, fsize);

    u8 *fp = buffer;
    if (memcmp(fp, "PS-X EXE", 8))
    {
        return false;
    }

    g_gpu.enable_output = false;
    g_spu.enable_output = false;

    while (g_cpu.pc != 0x80030000)
        psx_step();

    g_gpu.enable_output = true;
    g_spu.enable_output = true;

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

    return true;
}

b8 psx_mount_from_file(const char *path)
{
    if (platform_open_file(path, &mounted_image.file))
    {
        if (string_ends_with_ignore_case(path, ".exe"))
        {
            mounted_image.type = EXE;
        }
        else if (string_ends_with_ignore_case(path, ".bin"))
        {
            mounted_image.type = BIN;
        }
        else
        {
            SY_ASSERT(0);
        }
        return true;
    }
    return false;
}

void psx_load_image(void)
{
    loaded_image = mounted_image;
    switch (loaded_image.type)
    {
    case EXE:
    {
        cdrom_init(NULL);
        if (!psx_load_exe(&loaded_image.file))
            return;
        break;
    }
    case BIN:
    {
        cdrom_init(&loaded_image.file);
        break;
    }
    INVALID_CASE;
    }
}

void psx_mount_image(platform_file file, psx_image_type type)
{
    mounted_image.file = file;
    mounted_image.type = type;
}

void psx_reset(void)
{
    cpu_init();
    gpu_reset();
    dma_init();
    scheduler_reset();
    schedule_event(spu_tick, 0, 768);
    schedule_event(gpu_scanline_complete, 0, (s32)video_to_cpu_cycles(NTSC_VIDEO_CYCLES_PER_SCANLINE));
    psx_load_image();
}

void psx_init(struct memory_arena *arena, void *bios)
{
    cpu_init();

    g_bios = bios;
    
    g_ram = push_arena(arena, MEGABYTES(2));
    g_scratch = push_arena(arena, KILOBYTES(1));

    //g_gpu.vram = push_arena(arena, VRAM_SIZE);
    g_gpu.copy_buffer = push_arena(arena, VRAM_SIZE);
    //result->gpu.readback_buffer = push_arena(arena, VRAM_SIZE);
    // set to NTSC timings by default
    gpu_reset();
    g_gpu.enable_output = true;
    g_gpu.vertical_timing = 263;
    g_gpu.horizontal_timing = NTSC_VIDEO_CYCLES_PER_SCANLINE;
    // make sure we set draw area on the first draw in case it wasnt set by the program
    g_gpu.draw_area_changed = true;
    
    cdrom_init(NULL);

    //result->cdrom.status = 0x8; // set parameter fifo to empty

    dma_init();

    g_peripheral = push_arena(arena, 32); // temp

    g_gpu.stat.value = 0x14802000;
    memset(g_ram, 0xcf, MEGABYTES(2)); // initialize with known garbage value 0xcf

    g_spu.enable_output = true;
    g_spu.dram = push_arena(arena, KILOBYTES(512));
    g_spu.buffered_samples = push_arena(arena, 2048);

    scheduler_init(arena);

    //g_sio.rx_buffer = 0xff;
    g_sio.stat.tx_fifo_not_full = 1;
    g_sio.stat.tx_finished = 1;
    //g_sio.buttons.value = 0xffff;

    schedule_event(spu_tick, 0, 768);
    //schedule_event(result, gpu_scanline_complete, 0, video_to_cpu_cycles(3413));
    // TODO: reschedule gpu events when display settings are changed
    schedule_event(gpu_scanline_complete, 0, (s32)video_to_cpu_cycles(NTSC_VIDEO_CYCLES_PER_SCANLINE));
    //schedule_event(cpu, gpu_hblank_event, 0, cpu->gpu.horizontal_display_x2 - cpu->gpu.horizontal_display_x1, EVENT_ID_GPU_HBLANK);
}

void psx_run(void)
{
    // TODO: cycles_ran does nothing
    u64 tick_count = get_tick_count();

    u64 cycles_ran = execute_instruction(tick_count);

    tick_events(cycles_ran);
}

void psx_step(void)
{
    u64 cycles_ran = execute_instruction(1);
    tick_events(cycles_ran);
}
