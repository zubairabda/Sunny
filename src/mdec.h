#ifndef MDEC_H
#define MDEC_H

#include "common.h"

#define DATA_FIFO_SIZE 32768

enum
{
    MDEC_ENABLE_DATAOUT = (1 << 29),
    MDEC_ENABLE_DATAIN  = (1 << 30),
    MDEC_RESET          = (1 << 31)
};

typedef union
{
    struct
    {
        u32 parameters_left : 16;
        u32 current_block : 3;
        u32 : 4;
        u32 depth_only : 1;
        u32 signed_output : 1;
        u32 output_depth : 2;
        u32 data_out_req : 1;
        u32 data_in_req : 1;
        u32 busy : 1;
        u32 data_in_fifo_full : 1;
        u32 data_out_fifo_empty : 1;
    };
    u32 value;
} mdec_status;

struct mdec_state
{
    mdec_status stat;
    u32 parameters_left;
    u32 parameter_fifo[65536];
    u32 parameter_fifo_count;
    u32 halfwords;
    u32 decode_index;
    u32 command;

    u32 data_fifo[DATA_FIFO_SIZE];
    u32 data_fifo_tail;  // 
    u32 data_fifo_head;  // measured in words
    u32 data_fifo_count; // 

    b8 dma_in_enabled;
    b8 dma_out_enabled;

    u8 iq_table[128]; // luminance + color
    s16 scale_table[64];
};

extern struct mdec_state g_mdec;

void mdec_reset(u32 flags);
void mdec_command(u32 word);
b8 mdecout_on_dma(b8 dir_from_ram, s8 step, u32 size, u32 *paddr);
u32 mdec_getstat(void);
u32 mdec_read(void);

#endif /* MDEC_H */
