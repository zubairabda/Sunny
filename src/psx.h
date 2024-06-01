#ifndef PSX_H
#define PSX_H

#include "allocator.h"

struct cpu_state;
struct gpu_state;
struct spu_state;
struct dma_state;
struct joypad_state;
struct cdrom_state;
struct root_counter;

struct psx_state
{
    struct cpu_state *cpu;
    struct root_counter *timers[3];

    u8 *bios;
    u8 *ram;
    u8 *scratch;
    u8 *peripheral;

    struct dma_state *dma;
    struct gpu_state *gpu;
    struct spu_state *spu;
    struct joypad_state *pad;
    struct cdrom_state *cdrom;
    u32 pending_cycles;
};

b8 psx_init(struct psx_state *psx, struct memory_arena *arena, void *bios);
void psx_run(struct psx_state *psx);

#endif
