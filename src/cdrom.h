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
    u8 count;
    u8 fifo[16];
};

struct cdrom_sector
{
    u32 pos;
    u8 data[2352];
};

enum cdrom_state
{
    CDROM_STATE_STOPPED,
    CDROM_STATE_IDLE,
    CDROM_STATE_READING,
    CDROM_STATE_SEEKING,
    CDROM_STATE_PLAYING
};

struct cdrom_context
{
    cdrom_status status;
    u8 interrupt_enable;
    u8 interrupt_flag;

    enum cdrom_state state;
    u16 response_fifo_current;
    u16 response_fifo_count;
    u8 response_fifo[16];

    u8 mode;
    b8 seek_pending;
    b8 command_pending;
    b8 response_pending;
    struct cdrom_response first_response;
    struct cdrom_response queued_response;
    u32 response_delay_cycles;
    u64 timestamp;
    u64 response_event_id;
    u64 first_resp_event_id; // TODO: remove
    u64 read_event_id; // TODO: rename
    u16 data_fifo_end;
    u16 data_fifo_index;
    u16 param_fifo_count;
    u8 param_fifo[16];

    u8 pending_vol_LL;
    u8 pending_vol_LR;
    u8 pending_vol_RR;
    u8 pending_vol_RL;

    u8 vol_LL;
    u8 vol_LR;
    u8 vol_RR;
    u8 vol_RL;

    u16 sector_size;
    u8 sector_offset;
    b8 pending_speed_switch_delay;
    u8 next_sector_index;
    b8 read_error;
    u32 seek_target;
    u32 loc;
    disk_image *disk;
    struct cdrom_sector newest_sector;
    struct cdrom_sector buffered_sector;
};

extern struct cdrom_context g_cdrom;

void cdrom_reset(void);
void cdrom_load_disk(disk_image *disk);
u8 cdrom_read(u32 offset);
void cdrom_store(u32 offset, u8 value);

#endif /* CDROM_H */
