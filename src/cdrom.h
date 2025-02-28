#ifndef CDROM_H
#define CDROM_H

#include "stream.h"

typedef union
{
    struct
    {
        u8 index : 2;
        u8 ADPBUSY : 1;
        u8 PRMEMPT : 1;
        u8 PRMWRDY : 1;
        u8 RSLRRDY : 1;
        u8 DRQSTS  : 1;
        u8 BUSYSTS : 1;
    };
    u8 value;
} cdrom_status;

struct cdrom_response
{
    u8 cause;
    u8 response_count;
    u8 response[16];
};

struct cdrom_state
{
    cdrom_status status;
    u8 interrupt_enable;
    u8 interrupt_flag;
    u8 stat;

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
    b8 pending_speed_switch_delay;
    u32 target;
    u32 loc;
    disk_image *disk;
    char sector[2352];
};

extern struct cdrom_state g_cdrom;

void cdrom_reset(void);
void cdrom_load_disk(disk_image *disk);
u8 cdrom_read(u32 offset);
void cdrom_store(u32 offset, u8 value);

#endif /* CDROM_H */
