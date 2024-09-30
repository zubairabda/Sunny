#ifndef PSX_H
#define PSX_H

#include "allocator.h"
#if 0
struct cpu_state;
struct gpu_state;
struct spu_state;
struct dma_state;
struct joypad_state;
struct cdrom_state;
struct root_counter;

struct tick_event;

struct psx_state
{
    struct cpu_state *cpu;
    struct root_counter *timers[3];

    struct dma_state *dma;
    struct gpu_state *gpu;
    struct spu_state *spu;
    struct joypad_state *pad;
    struct cdrom_state *cdrom;
    u32 pending_cycles;
};
#endif
void psx_init(struct memory_arena *arena, void *bios);
void psx_run(void);

#endif /* PSX_H */
