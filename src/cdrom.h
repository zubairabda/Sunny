#ifndef CDROM_H
#define CDROM_H

#include "platform/platform.h"

#define CDR_STATUS_TRANSMISSION_BUSY       (1 << 7)
#define CDR_STATUS_DATA_FIFO_NOT_EMPTY     (1 << 6)
#define CDR_STATUS_RESPONSE_FIFO_NOT_EMPTY (1 << 5)
#define CDR_STATUS_PARAM_FIFO_NOT_FULL     (1 << 4)
#define CDR_STATUS_PARAM_FIFO_EMPTY        (1 << 3)
#define CDR_STATUS_ADPCM_FIFO_NOT_EMPTY    (1 << 2)

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
    u8 stat;
    //u8 request_reg;

    u16 response_fifo_current;
    u16 response_fifo_count;
    u8 response_fifo[16];

    u8 mode;

    b8 pending_response;
    struct cdrom_response queued_response;
    u32 response_delay_cycles;
    u64 command_timestamp; // cycle count when we sent the command
    u64 response_event_id;
    u64 interrupt_event_id;
    u16 data_fifo_end;
    u16 data_fifo_index;
    u16 param_fifo_count;
    u8 param_fifo[16];

    u16 sector_size;
    u8 sector_offset;
    b8 is_reading;
    u32 target;
    u32 loc;
    platform_file disk; // TODO: temp
    char sector[2352];
};

extern struct cdrom_state g_cdrom;

void cdrom_reset(void);
void cdrom_load_disk(platform_file disk);
u8 cdrom_read(u32 offset);
void cdrom_store(u32 offset, u8 value);

#endif /* CDROM_H */
