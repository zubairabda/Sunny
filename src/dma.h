#ifndef DMA_H
#define DMA_H

#include "common.h"

typedef union
{
    struct
    {
        u32 transfer_from_ram : 1;
        u32 step_backward : 1;
        u32 : 6;
        u32 chopping_enable : 1;
        u32 sync_mode : 2;
        u32 : 5;
        u32 chopping_dma_window : 3;
        u32 : 1;
        u32 chopping_cpu_window : 3;
        u32 : 1;
        u32 start_busy : 1;
        u32 : 3;
        u32 start_trigger : 1;
        u32 : 3;
    };
    u32 value;
} DHCR;

struct dma_port
{
    u32 madr;
    u16 b1;
    u16 b2;
    u32 control;
    u32 pad;
};

struct dma_state
{
    struct dma_port ports[7];
    u32 control;
    u32 interrupt;
};

enum dma_channel
{
    CH_MDECIN,
    CH_MDECOUT,
    CH_GPU,
    CH_CDROM,
    CH_SPU,
    CH_PIO,
    CH_OTC
};

extern struct dma_state g_dma;

void dma_init(void);
u32 dma_read(u32 offset);
void dma_write(u32 offset, u32 value);
void dma_set_interrupt(u32 channel);

#endif /* DMA_H */