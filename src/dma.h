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

enum dma_channel
{
    CH_MDECIN,
    CH_MDECOUT,
    CH_GPU,
    CH_CDROM,
    CH_SPU,
    CH_PIO,
    CH_OTC,
    CH_COUNT
};

enum dma_mode
{
    DMA_MODE_BURST = 0,
    DMA_MODE_SLICE = 1,
    DMA_MODE_LINKEDLIST = 2
};

struct dma_port
{
    u32 madr;
    u16 b1;
    u16 b2;
    u32 control;
    u32 pad;
};

struct dma_transfer
{
    u32 current_addr;
    u32 words_left;
    u32 priority;
    s8 step;
    u8 transfer_mode;
    //u8 dma_window;
    //u8 cpu_window;
    //u16 blocks_left;
    b8 is_from_ram;
    b8 in_progress;
    u32 pending_words;
    b8 pending;
    u8 pad[3];
};

struct dma_state
{
    struct dma_port ports[CH_COUNT];
    u32 control;
    u32 interrupt;
    struct dma_transfer transfers[CH_COUNT];
    s32 active_channel;
    u64 event;
};

extern struct dma_state g_dma;

void dma_init(void);
u32 dma_read(u32 offset);
void dma_write(u32 offset, u32 value);

#endif /* DMA_H */