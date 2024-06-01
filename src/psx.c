#include "psx.h"
#include "event.h"
#include "cpu.h"
#include "gpu.h"
#include "timers.h"
#include "cdrom.h"
#include "pad.h"
#include "dma.h"
#include "spu.h"

// TODO: read bios file in here so we can push it to the arena?
// TODO: pass in audio buffer size?
b8 psx_init(struct psx_state *psx, struct memory_arena* arena, void *bios)
{
    //struct psx_state* result = push_arena(arena, sizeof(struct psx_state));
    if (!psx) {
        return 0;
    }

    psx->cpu = push_arena(arena, sizeof(struct cpu_state));
    psx->timers[0] = push_arena(arena, sizeof(struct root_counter));
    psx->timers[1] = push_arena(arena, sizeof(struct root_counter));
    psx->timers[2] = push_arena(arena, sizeof(struct root_counter));
    psx->gpu = push_arena(arena, sizeof(struct gpu_state));
    psx->spu = push_arena(arena, sizeof(struct spu_state));
    psx->cdrom = push_arena(arena, sizeof(struct cdrom_state));
    psx->dma = push_arena(arena, sizeof(struct dma_state));
    psx->pad = push_arena(arena, sizeof(struct joypad_state));
    // TODO: push timers
    
    psx->cpu->pc = 0xbfc00000;
    psx->cpu->next_pc = 0xbfc00004;
    psx->cpu->cop0[15] = 0x2; // PRID

    psx->bios = bios;
    psx->ram = push_arena(arena, MEGABYTES(2));
    psx->scratch = push_arena(arena, KILOBYTES(1));

    psx->gpu->vram = push_arena(arena, VRAM_SIZE);
    psx->gpu->copy_buffer = push_arena(arena, VRAM_SIZE);
    //result->gpu.readback_buffer = push_arena(arena, VRAM_SIZE);
    // set to NTSC timings by default
    psx->gpu->vertical_timing = 263;
    psx->gpu->horizontal_timing = NTSC_VIDEO_CYCLES_PER_SCANLINE;
    // make sure we set draw area on the first draw in case it wasnt set by the program
    psx->gpu->draw_area_changed = 1;
    
    

    cdrom_init(psx->cdrom);
    //result->cdrom.status = 0x8; // set parameter fifo to empty
    
    psx->pad->rx_buffer = 0xff;
    psx->pad->stat.tx_started = 1;
    psx->pad->stat.tx_finished = 1;

    psx->dma->control = 0x07654321; // inital value of control register

    psx->peripheral = push_arena(arena, 32); // temp

    psx->gpu->stat.value = 0x14802000;
    memset(psx->ram, 0xcf, MEGABYTES(2)); // initialize with known garbage value 0xcf

    psx->spu->dram = push_arena(arena, KILOBYTES(512));
    psx->spu->buffered_samples = push_arena(arena, 2048);

    scheduler_init(arena);

    schedule_event(spu_tick, psx->spu, 0, 768, EVENT_ID_DEFAULT);
    //schedule_event(result, gpu_scanline_complete, 0, video_to_cpu_cycles(3413));

    return 1;
}

void run_psx(struct psx_state *psx)
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
    u64 cycles_ran = execute_instruction(psx, tick_count);
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
