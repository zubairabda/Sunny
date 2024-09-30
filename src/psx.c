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

//#include "fileio.h"

// TODO: read bios file in here so we can push it to the arena?
// TODO: pass in audio buffer size?
void psx_init(struct memory_arena *arena, void *bios)
{
    //struct psx_state* result = push_arena(arena, sizeof(struct psx_state));
#if 0
    psx->cpu = push_arena(arena, sizeof(struct cpu_state));
    psx->timers[0] = push_arena(arena, sizeof(struct root_counter));
    psx->timers[1] = push_arena(arena, sizeof(struct root_counter));
    psx->timers[2] = push_arena(arena, sizeof(struct root_counter));
    psx->gpu = push_arena(arena, sizeof(struct gpu_state));
    psx->spu = push_arena(arena, sizeof(struct spu_state));
    psx->cdrom = push_arena(arena, sizeof(struct cdrom_state));
    psx->dma = push_arena(arena, sizeof(struct dma_state));
    psx->pad = push_arena(arena, sizeof(struct joypad_state));
#endif
    cpu_init();

    g_bios = bios;
    g_ram = push_arena(arena, MEGABYTES(2));
    g_scratch = push_arena(arena, KILOBYTES(1));

    g_gpu.vram = push_arena(arena, VRAM_SIZE);
    g_gpu.copy_buffer = push_arena(arena, VRAM_SIZE);
    //result->gpu.readback_buffer = push_arena(arena, VRAM_SIZE);
    // set to NTSC timings by default
    gpu_reset();
    g_gpu.vertical_timing = 263;
    g_gpu.horizontal_timing = NTSC_VIDEO_CYCLES_PER_SCANLINE;
    // make sure we set draw area on the first draw in case it wasnt set by the program
    g_gpu.draw_area_changed = 1;
    
    
    platform_file disk = open_file("C:\\Users\\Zubair\\Desktop\\psx\\Crash Bandicoot (USA)\\Crash Bandicoot (USA).bin");
    //platform_file disk = open_file("C:\\Users\\Zubair\\Desktop\\psx\\Spyro - Year of the Dragon (USA) (Rev 1)\\Spyro - Year of the Dragon (USA) (Rev 1).bin");
    cdrom_init(disk);

    //result->cdrom.status = 0x8; // set parameter fifo to empty
    
    g_sio.rx_buffer = 0xff;
    g_sio.stat.tx_started = 1;
    g_sio.stat.tx_finished = 1;
    g_sio.buttons.value = 0xffff;

    dma_init();

    g_peripheral = push_arena(arena, 32); // temp

    g_gpu.stat.value = 0x14802000;
    memset(g_ram, 0xcf, MEGABYTES(2)); // initialize with known garbage value 0xcf

    g_spu.dram = push_arena(arena, KILOBYTES(512));
    g_spu.buffered_samples = push_arena(arena, 2048);

    scheduler_init(arena);

    schedule_event(spu_tick, 0, 768);
    //schedule_event(result, gpu_scanline_complete, 0, video_to_cpu_cycles(3413));
    // TODO: reschedule gpu events when display settings are changed
    schedule_event(gpu_scanline_complete, 0, (s32)video_to_cpu_cycles(NTSC_VIDEO_CYCLES_PER_SCANLINE));
    //schedule_event(cpu, gpu_hblank_event, 0, cpu->gpu.horizontal_display_x2 - cpu->gpu.horizontal_display_x1, EVENT_ID_GPU_HBLANK);
}

void psx_run(void)
{
    // get input
    #if 0
    dinput_get_data(input);
    cpu->pad.buttons = input->pad;
    #endif
    //s32 cycles_to_run = min(cpu->cycles_until_next_event, 384);
    u64 tick_count = get_tick_count();
    // NOTE: leftover_cycles are extra cycles that the cpu ran ahead of the cycles to run
    //s32 leftover_cycles = execute_instruction(cpu, cycles_to_run);
    // the cpu may have had to halt execution if an event was scheduled sooner than the cycles remaining
    u64 cycles_ran = execute_instruction(tick_count);
    // TODO: add in extra cycles executed by the cpu if any
    //s32 tick_count = cycles_executed;/*cycles_to_run + leftover_cycles;*/

    tick_events(cycles_ran);
    
#if 0
    cpu->spu.ticks += tick_count;
    if (cpu->spu.ticks > 768.0f) {
        spu_tick(cpu, 0);
        cpu->spu.ticks -= 768.0f;
    }
    b8 vsync = 0;
    cpu->gpu.ticks += tick_count * (715909.0f / 451584.0f);
    if (gpu_run(&cpu->gpu)) {
        vsync = 1;
        ++g_vblank_counter;
        cpu->i_stat |= INTERRUPT_VBLANK;
        SetEvent(g_present_thread_handle);
    }
#endif
}
