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
    b8 is_xa_sector;
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
    u64 response_event;
    u64 first_resp_event; // TODO: remove
    u64 active_event;
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

    u8 xa_file;
    u8 xa_channel;
    u16 sector_size;
    u32 track_end; // for autopause
    u8 sector_offset;
    b8 pending_speed_switch_delay;
    b8 should_pause;
    u8 read_error;
    u32 seek_target;
    u32 loc;
    //disk_image *disk;
    u8 sector_index; // sector ring buffer contains the previously read (buffered) sector and the newest sector, index points to the newest sector
    u8 sector_buffer[2][2352];

    s16 xa_prev_left[2];
    s16 xa_prev_right[2];
    s16 xa_decode_buffer[4032];
};

extern struct cdrom_context g_cdrom;

void cdrom_reset(void);
//void cdrom_load_disk(disk_image *disk);
u8 cdrom_read(u32 offset);
void cdrom_write(u32 offset, u8 value);

#endif /* CDROM_H */
