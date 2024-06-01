#ifndef DMA_H
#define DMA_H

#include "psx.h"

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

struct dma_channel
{
    u32 madr;
    u16 b1;
    u16 b2;
    u32 control;
    u32 pad;
};

struct dma_state
{
    struct dma_channel channels[7];
    u32 control;
    u32 interrupt;
};

u32 dma_read(struct dma_state *dma, u32 offset);
void dma_write(struct psx_state *psx, u32 offset, u32 value);

#endif