#ifndef CDROM_H
#define CDROM_H

#include "common.h"

union CDROM_STATUS
{
    struct
    {
        u8 index : 2;
        u8 adpcm_fifo_empty : 1;
        u8 param_fifo_empty : 1;
        u8 param_fifo_full : 1;
        u8 response_fifo_empty : 1;
        u8 data_fifo_empty : 1;
        u8 transmission_busy : 1;
    };
    u8 value;
};

enum cdrom_command_state
{
    CDROM_COMMAND_STATE_PENDING,
    CDROM_COMMAND_STATE_TRANSFER,
    CDROM_COMMAND_STATE_EXECUTE
};

struct cdrom_response
{
    u8 cause;
    u8 response_count;
    u8 response[16];
};

struct cdrom_state
{
    u8 status;
    u8 interrupt_enable;
    u8 interrupt_flag;
    u8 request_reg;

    u16 response_fifo_current;
    u16 response_fifo_count;
    u8 response_fifo[16];

    b8 command_issued_during_int;
    u8 pending_command;
    #if 0
    u8 queued_interrupt;
    u8 queued_response[16];
    u16 queued_response_count;
    #endif
    b8 pending_response;
    struct cdrom_response queued_response;
    u32 response_delay_cycles;
    u64 command_timestamp_cycles; // cycle count when we sent the command
    
    u16 param_fifo_count;
    u8 param_fifo[16];

    u8 stat;
};

void cdrom_init(struct cdrom_state *cdrom);

#endif
