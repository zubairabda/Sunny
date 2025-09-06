#include "cdrom.h"
#include "cpu.h"
#include "spu.h"
#include "event.h"
#include "debug.h"
#include "psx.h"

static u8 dec_to_bcd_table[] =
{
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
    0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
    0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
    0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99,
};

enum
{
    CDR_STAT_ERROR   = (1 << 0),
    CDR_STAT_MOTOR   = (1 << 1),
    CDR_STAT_SEEKERR = (1 << 2),
    CDR_STAT_IDERR   = (1 << 3),
    CDR_STAT_SHELL   = (1 << 4),
    CDR_STAT_READING = (1 << 5),
    CDR_STAT_SEEKING = (1 << 6),
    CDR_STAT_PLAYING = (1 << 7)
};

enum
{
    CDR_MODE_CDDA      = (1 << 0),
    CDR_MODE_AUTOPAUSE = (1 << 1),
    CDR_MODE_REPORT    = (1 << 2),
    CDR_MODE_XAFILTER  = (1 << 3),
    CDR_MODE_IGNORE    = (1 << 4),
    CDR_MODE_SIZE      = (1 << 5),
    CDR_MODE_XAADPCM   = (1 << 6),
    CDR_MODE_SPEED     = (1 << 7)
};

#define BFWR 0x40
#define BFRD 0x80

#define CYCLES_PER_USEC 34
#define INT_DELAY 50401
#define READ_DELAY 451584
#define READ_2X_DELAY (READ_DELAY >> 1)
#define PAUSE_DELAY 2300000
#define SPEED_SWITCH_DELAY 22014720

enum cdrom_command
{
    Getstat = 0x1,
    Setloc = 0x2,
    Play = 0x3,
    Forward = 0x4,
    Backward = 0x5,
    ReadN = 0x6,
    MotorOn = 0x7,
    Stop = 0x8,
    Pause = 0x9,
    Init = 0xA,
    Mute = 0xB,
    Demute = 0xC,
    Setfilter = 0xD,
    Setmode = 0xE,
    Getparam = 0xF,
    GetlocL = 0x10,
    GetlocP = 0x11,
    SetSession = 0x12,
    GetTN = 0x13,
    GetTD = 0x14,
    SeekL = 0x15,
    SeekP = 0x16,
    Test = 0x19,
    GetID = 0x1A,
    ReadS = 0x1B,
    Reset = 0x1C,
    GetQ = 0x1D,
    ReadTOC = 0x1E
};

struct cdrom_context g_cdrom;

static const char *cdrom_cmd_to_string(enum cdrom_command command)
{
    switch (command)
    {
    case Getstat:
        return "Getstat";
    case Setloc:
        return "Setloc";
    case Play:
        return "Play";
    case Forward:
        return "Forward";
    case Backward:
        return "Backward";
    case ReadN:
        return "ReadN";
    case MotorOn:
        return "MotorOn";
    case Stop:
        return "Stop";
    case Pause:
        return "Pause";
    case Init:
        return "Init";
    case Mute:
        return "Mute";
    case Demute:
        return "Demute";
    case Setfilter:
        return "Setfilter";
    case Setmode:
        return "Setmode";
    case Getparam:
        return "Getparam";
    case GetlocL:
        return "GetlocL";
    case GetlocP:
        return "GetlocP";
    case SetSession:
        return "SetSession";
    case GetTN:
        return "GetTN";
    case GetTD:
        return "GetTD";
    case SeekL:
        return "SeekL";
    case SeekP:
        return "SeekP";
    case Test:
        return "Test";
    case GetID:
        return "GetID";
    case ReadS:
        return "ReadS";
    case Reset:
        return "Reset";
    case GetQ:
        return "GetQ";
    case ReadTOC:
        return "ReadTOC";
    default:
        return "";
    }
}

void cdrom_reset(void)
{
    memset(&g_cdrom, 0, sizeof(struct cdrom_context));
    g_cdrom.status.value = 0x18; // param fifo not full, param fifo empty

    g_cdrom.sector_size = 0x800;
    g_cdrom.sector_offset = 24;

    g_cdrom.vol_LL = g_cdrom.pending_vol_LL = 0x80;
    g_cdrom.vol_RR = g_cdrom.pending_vol_RR = 0x80;
}

static u8 cdrom_get_stat(void)
{
    // TODO: send INT5 with error byte 0x8
    if (!g_psx.disk)
        return CDR_STAT_SHELL;

    enum cdrom_state state = g_cdrom.state;
    u8 result = 0;

    if (state != CDROM_STATE_STOPPED)
        result |= CDR_STAT_MOTOR;

    if (state == CDROM_STATE_READING)
        result |= CDR_STAT_READING;
    else if (state == CDROM_STATE_SEEKING)
        result |= CDR_STAT_SEEKING;
    else if (state == CDROM_STATE_PLAYING)
        result |= CDR_STAT_PLAYING;

    return result;
}

static inline b8 is_valid_bcd(u8 value)
{
    return ((value & 0xf) < 0xa) && ((value & 0xf0) < 0xa0);
}

static inline u8 bcd_to_decimal(u8 bcd)
{
    return (bcd - 0x6 * (bcd >> 4));
}

static inline u8 decimal_to_bcd(u8 dec)
{
    return dec_to_bcd_table[dec];
}

// ref: for zigzag interpolation across XA-ADPCM samples
static s16 zigzag_tables[7][29] = 
{
    {
        0x0000,  0x0000, 0x0000,  0x0000,  0x0000, -0x0002,  0x000A, -0x0022,
        0x0041, -0x0054, 0x0034,  0x0009, -0x010A,  0x0400, -0x0A78,  0x234C,
        0x6794, -0x1780, 0x0BCD, -0x0623,  0x0350, -0x016D,  0x006B,  0x000A,
        -0x0010,  0x0011, -0x0008,  0x0003, -0x0001
    },
    {
        0x0000,  0x0000, 0x0000, -0x0002, 0x0000, 0x0003, -0x0013, 0x003C,
        -0x004B,  0x00A2, -0x00E3, 0x0132, -0x0043, -0x0267, 0x0C9D, 0x74BB,
        -0x11B4, 0x09B8, -0x05BF, 0x0372, -0x01A8, 0x00A6, -0x001B, 0x0005,
        0x0006, -0x0008, 0x0003, -0x0001, 0x0000
    },
    {
        0x0000, 0x0000, -0x0001, 0x0003, -0x0002, -0x0005, 0x001F, -0x004A,
        0x00B3, -0x0192, 0x02B1, -0x039E, 0x04F8, -0x05A6, 0x7939, -0x05A6,
        0x04F8, -0x039E, 0x02B1, -0x0192, 0x00B3, -0x004A, 0x001F, -0x0005,
        -0x0002, 0x0003,-0x0001, 0x0000, 0x0000
    },
    {
        0x0000, -0x0001, 0x0003, -0x0008, 0x0006, 0x0005, -0x001B, 0x00A6,
        -0x01A8, 0x0372, -0x05BF, 0x09B8, -0x11B4, 0x74BB, 0x0C9D, -0x0267,
        -0x0043, 0x0132, -0x00E3, 0x00A2, -0x004B, 0x003C, -0x0013, 0x0003,
        0x0000, -0x0002, 0x0000, 0x0000, 0x0000
    },
    {
        -0x0001, 0x0003, -0x0008, 0x0011, -0x0010, 0x000A, 0x006B, -0x016D,
        0x0350, -0x0623, 0x0BCD, -0x1780, 0x6794, 0x234C, -0x0A78, 0x0400,
        -0x010A, 0x0009, 0x0034, -0x0054, 0x0041, -0x0022, 0x000A, -0x0001,
        0x0000, 0x0001, 0x0000, 0x0000, 0x0000
    },
    {
        0x0002, -0x0008, 0x0010, -0x0023, 0x002B, 0x001A, -0x00EB, 0x027B,
        -0x0548, 0x0AFA, -0x16FA, 0x53E0, 0x3C07, -0x1249, 0x080E, -0x0347,
        0x015B, -0x0044, -0x0017, 0x0046, -0x0023, 0x0011, -0x0005, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000
    },
    {
        -0x0005, 0x0011, -0x0023, 0x0046, -0x0017, -0x0044, 0x015B, -0x0347,
        0x080E, -0x1249, 0x3C07, 0x53E0, -0x16FA, 0x0AFA, -0x0548, 0x027B,
        -0x00EB, 0x001A, 0x002B, -0x0023, 0x0010, -0x0008, 0x0002, 0x0000,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0000
    }
};

static void decode_28_nibbles(u8 *src, int blk, int nibble, s16 *dest, s16 *prev)
{
    u8 header = src[4 + blk * 2 + nibble];
    u8 shift = 12 - (header & 0xf);
    u8 filter = (header >> 4) & 0x3;
    // XA only support filters 0-3
    int pos_adpcm_table[] = {0, 60, 115, 98};
    int neg_adpcm_table[] = {0, 0, -52, -55};
    int f0 = pos_adpcm_table[filter];
    int f1 = neg_adpcm_table[filter];

    for (int j = 0; j < 28; ++j)
    {
        s32 t = ((src[16 + blk + j * 4] >> (nibble * 4)) & 0xf);
        t <<= 28;
        t >>= 28;
        s32 s = (t << shift) + ((prev[0] * f0 + prev[1] * f1 + 32) / 64);
        s16 result = (s16)clamp16(s);
        dest[j * 2] = result;
        prev[1] = prev[0];
        prev[0] = result;
    }
}

static inline void push_cd_buffer(s16 sample)
{
    g_spu.cd_buffer[g_spu.cd_buffer_length++] = sample;
}

// TODO: remove
static int sixstep = 6;
static int p = 0;
static s16 zigzag_buffer[2][32];

static inline s16 zigzag_interpolate(int p, int channel, s16 *table)
{
    int sum = 0;
    for (int i = 0; i < 29; ++i)
    {
        sum += (((s32)zigzag_buffer[channel][(p - i) & 0x1f] * (s32)table[i]) >> 15);
    }
    return (s16)clamp16(sum);
}

static void decode_xa_adpcm(u8 *data, u8 coding_info)
{
    b8 is_stereo = coding_info & 0x1;
    b8 is_halfrate = (coding_info >> 2) & 0x1;
    b8 is_8bit = (coding_info >> 4) & 0x1;
    SY_ASSERT(!is_8bit);
    g_spu.cd_buffer_index = 0;
    g_spu.cd_buffer_length = 0;
    s16 *dst = &g_cdrom.xa_decode_buffer[0];
    u8 *src = data;

    // for each 128-byte portion
    for (int i = 0; i < 0x12; ++i)
    {
        if (is_stereo)
        {
            for (int j = 0; j < 4; ++j)
            {
                s16 *dst_left = dst;
                s16 *dst_right = dst_left + 1;
                decode_28_nibbles(src, j, 0, dst_left, g_cdrom.xa_prev_left);
                decode_28_nibbles(src, j, 1, dst_right, g_cdrom.xa_prev_right);
                dst += 56;
                //g_spu.cd_buffer_length += 56;
            }
        }
        else
        {
            SY_ASSERT(0);
        }
        src += 128;
    }

    int count = is_stereo ? 2016 : 4032;
    for (int i = 0; i < count; ++i)
    {
        if (is_stereo)
        {
            zigzag_buffer[0][p & 0x1f] = g_cdrom.xa_decode_buffer[i * 2];
            zigzag_buffer[1][p & 0x1f] = g_cdrom.xa_decode_buffer[i * 2 + 1];

            ++p;
            --sixstep;

            if (sixstep == 0)
            {
                sixstep = 6;
                for (int table = 0; table < 7; ++table)
                {
                    push_cd_buffer(zigzag_interpolate(p, 0, zigzag_tables[table]));
                    push_cd_buffer(zigzag_interpolate(p, 1, zigzag_tables[table]));
                }
                
            }
        }
    }
}

static u32 cdrom_read_next_sector(void)
{
    u32 next_int = 0;
    u8 *sector = g_cdrom.sector_buffer[g_cdrom.sector_index];

    // preload next sector
    if (read_disk_sector(g_psx.disk, g_cdrom.loc, sector))
    {
        u32 lba = g_cdrom.loc;
        MSF pos = lba_to_msf(lba);
        debug_log("[CDROM] Reading sector %d:%d:%d\n", pos.m, pos.s, pos.f);
        g_cdrom.is_xa_sector = false;
        char sync[12] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
        if (memcmp(sector, sync, 12) == 0)
        {
            ++g_cdrom.loc;
            next_int = g_cdrom.mode & CDR_MODE_SPEED ? READ_2X_DELAY : READ_DELAY;
            // ref: if xa-adpcm and/or xa-filter is enabled, INT1 is only generated for non-xa sectors
            u8 flags = g_cdrom.mode & (CDR_MODE_XAADPCM | CDR_MODE_XAFILTER);
            if (flags)
            {                
                u8 *at = sector + 12 + 4; // skip sync and header bytes
                u8 file = at[0];
                u8 channel = at[1];
                u8 submode = at[2];
                u8 coding_info = at[3];

                // I actually don't even know if this is a valid way to check for XA-ADPCM sectors
                if (submode & 0x4)
                {
                    g_cdrom.is_xa_sector = true; // lets us know in read_handler to not set INT1

                    if (g_cdrom.mode & CDR_MODE_XAFILTER)
                    {
                        if (file != g_cdrom.xa_file && channel != g_cdrom.xa_channel)
                            return next_int;
                    }

                    // we will only decode the XA if it is being sent to the SPU
                    if (flags & CDR_MODE_XAADPCM)
                    {
                        at += 4 + 4; // skip subheader and its copy
                        decode_xa_adpcm(at, coding_info);
                    }
                }
            }
        }
        else
        {
            g_cdrom.read_error = 0x4;
            next_int = CYCLES_PER_USEC * 4000000;
        }
    }
    else
    {
        g_cdrom.read_error = 0x10;
        next_int = CYCLES_PER_USEC * 600000;
    }

    return next_int;
}

static u32 cdrom_play_next_sector(void)
{
    u32 next_int = 0;
    //u8 *sector = g_cdrom.sector_buffer[g_cdrom.sector_index];
    u8 *buffer = (u8 *)g_spu.cd_buffer;
    if (read_disk_sector(g_psx.disk, g_cdrom.loc, buffer))
    {
        ++g_cdrom.loc;
        g_spu.cd_buffer_index = 0;
        g_spu.cd_buffer_length = DISK_SECTOR_SIZE;
        char sync[12] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
        if (memcmp(buffer, sync, 12) == 0)
        {
            debug_warn("[CDROM] Data sector during Play, returning silence...\n");
            memset(buffer, 0, 2352); // TODO: temp
        }
        next_int = g_cdrom.mode & CDR_MODE_SPEED ? READ_2X_DELAY : READ_DELAY;
    }
    else
    {
        // end of disk
        SY_ASSERT(0);
    }

    if (g_cdrom.mode & CDR_MODE_AUTOPAUSE)
    {
        if (g_cdrom.loc >= g_cdrom.track_end)
        {
            g_cdrom.should_pause = true;
        }
    }

    return next_int;
}

static u8 cdrom_get_current_track(void)
{
    disk_image *disk = g_psx.disk;
    u32 loc = g_cdrom.loc;

    for (u32 i = 0; i < disk->track_count; ++i)
    {
        struct disk_track *track = &disk->tracks[i];
        if (loc >= track->start && loc < track->end)
        {
            return (u8)(i + 1);
        }
    }

    return 0;
}

static b8 is_data_track(u32 lba)
{
    char sync[12] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
    u8 data[12];
    read_disk_data(g_psx.disk, lba, 12, data);
    return (memcmp(sync, data, 12) == 0);
}

static void cdrom_play_handler(u32 param, s32 ticks_late)
{
    // NOTE: I think I will merge the read and play functions, but I am still
    // thinking of the architecture I want to go for here, as I may fully commit
    // to having the _next_sector() functions return an 'error_time', where 0 means success
    if ((g_cdrom.interrupt_flag & 0x7) == 0 && (g_cdrom.mode & CDR_MODE_REPORT))
    {
        g_cdrom.status.RSLRRDY = true;
        debug_log("[CDROM] Report interrupt fired\n");
        set_interrupt(INTERRUPT_CDROM);
        if (g_cdrom.should_pause)
        {
            g_cdrom.should_pause = false;
            g_cdrom.state = CDROM_STATE_STOPPED;
            g_cdrom.interrupt_flag = 4;
            g_cdrom.response_fifo_count = 1;
            g_cdrom.response_fifo[0] = cdrom_get_stat();
        }
        else
        {
            u8 track = cdrom_get_current_track();
            struct disk_track *current_track = &g_psx.disk->tracks[track - 1];

            g_cdrom.interrupt_flag = 1;
            g_cdrom.response_fifo_count = 8;
            g_cdrom.response_fifo[0] = cdrom_get_stat();
            g_cdrom.response_fifo[1] = track;
            g_cdrom.response_fifo[2] = 1; // TODO: index
            // relative time within the track is returned if sector is 10h,30h,50h,70h
            MSF pos;
            u32 absolute_lba = g_cdrom.loc;
            if (pos.f & 0x10)
            {
                u32 track_start_lba = current_track->start;
                pos = lba_to_msf(absolute_lba - track_start_lba);
                pos.s |= 0x80;
            }
            else
            {
                pos = lba_to_msf(absolute_lba);
            }
            g_cdrom.response_fifo[3] = decimal_to_bcd(pos.m);
            g_cdrom.response_fifo[4] = decimal_to_bcd(pos.s);
            g_cdrom.response_fifo[5] = decimal_to_bcd(pos.f);
            g_cdrom.response_fifo[6] = 0;
            g_cdrom.response_fifo[7] = 0;
            //g_cdrom.sector_index = !g_cdrom.sector_index; // NOTE: not sure how sectors are buffered with audio
        }
    }

    if (g_cdrom.state == CDROM_STATE_PLAYING)
    {
        u32 next_int = cdrom_play_next_sector();
        g_cdrom.active_event = schedule_event(cdrom_play_handler, 0, next_int - ticks_late);
    }
}

static void cdrom_read_handler(u32 param, s32 ticks_late)
{
    if (g_cdrom.read_error != 0)
        g_cdrom.state = CDROM_STATE_IDLE;
    else
        g_cdrom.state = CDROM_STATE_READING;
#if SY_DEBUG    
    if ((g_cdrom.interrupt_flag & 0x7) != 0)
        debug_warn("[CDROM] Unacknowledged interrupt blocking INT1 from read.\n");
#endif
    if ((g_cdrom.interrupt_flag & 0x7) == 0 && !g_cdrom.is_xa_sector)
    {
        g_cdrom.status.RSLRRDY = true;
        debug_log("[CDROM] Read response interrupt fired\n");
        set_interrupt(INTERRUPT_CDROM);
        if (g_cdrom.read_error != 0)
        {
            g_cdrom.interrupt_flag = 5;
            g_cdrom.response_fifo_count = 2;
            g_cdrom.state = CDROM_STATE_IDLE;
            g_cdrom.response_fifo[0] = cdrom_get_stat() | CDR_STAT_SEEKERR;
            g_cdrom.response_fifo[1] = g_cdrom.read_error;
        }
        else
        {
            MSF pos = lba_to_msf(g_cdrom.loc - 1);
            debug_log("[CDROM] Copied Sector %d:%d:%d\n", pos.m, pos.s, pos.f);
            g_cdrom.interrupt_flag = 1;
            g_cdrom.response_fifo_count = 1;
            g_cdrom.response_fifo[0] = cdrom_get_stat();
            g_cdrom.sector_index = !g_cdrom.sector_index;
        }
    }

    if (!g_cdrom.read_error)
    {
        u32 next_int = cdrom_read_next_sector();
        g_cdrom.active_event = schedule_event(cdrom_read_handler, 0, next_int - ticks_late);
    }
}

static void cdrom_seek_event(u32 param, s32 ticks_late)
{
    g_cdrom.state = CDROM_STATE_IDLE;
}

static void cdrom_response_event(u32 param, s32 ticks_late)
{
    g_cdrom.response_event = 0;
    g_cdrom.status.RSLRRDY = true;

    debug_log("[CDROM] Second response interrupt fired\n");
    set_interrupt(INTERRUPT_CDROM);

    g_cdrom.interrupt_flag = g_cdrom.queued_response.cause;
    g_cdrom.response_fifo_count = g_cdrom.queued_response.count;
    memcpy(g_cdrom.response_fifo, g_cdrom.queued_response.fifo, 16);

    g_cdrom.timestamp = g_cycles_elapsed;
}

static void cdrom_first_response_event(u32 param, s32 ticks_late)
{
    g_cdrom.first_resp_event = 0;
    g_cdrom.status.RSLRRDY = true;
    debug_log("[CDROM] First response interrupt fired\n");
    set_interrupt(INTERRUPT_CDROM);
    g_cdrom.interrupt_flag = g_cdrom.first_response.cause;
    g_cdrom.response_fifo_count = g_cdrom.first_response.count;
    memcpy(g_cdrom.response_fifo, g_cdrom.first_response.fifo, 16);
}

static void cdrom_queue_error(u8 error) 
{
    g_cdrom.first_response.fifo[0] = cdrom_get_stat() | CDR_STAT_ERROR;
    g_cdrom.first_response.fifo[1] = error;
    g_cdrom.first_response.count = 2;
    g_cdrom.first_response.cause = 5;
}

static void cdrom_begin_read(void)
{
    g_cdrom.first_response.fifo[0] = cdrom_get_stat() | CDR_STAT_MOTOR;
    g_cdrom.first_response.count = 1;
    g_cdrom.first_response.cause = 3;

    //g_cdrom.state = CDROM_STATE_READING;

    u32 delay = 0;
    if (g_cdrom.pending_speed_switch_delay)
    {
        g_cdrom.pending_speed_switch_delay = false;
        delay += SPEED_SWITCH_DELAY;
    }
    
    if (g_cdrom.seek_pending)
    {
        // TODO: seek delay
        g_cdrom.seek_pending = false;
        g_cdrom.loc = g_cdrom.seek_target;
        //g_cdrom.state = CDROM_STATE_SEEKING;
    }

    g_cdrom.read_error = 0;

    u32 next_int = cdrom_read_next_sector();
    g_cdrom.active_event = schedule_event(cdrom_read_handler, 0, next_int + delay);
}

static void cdrom_command(u32 command, s32 ticks_late)
{
    //SY_ASSERT(g_cdrom.response_fifo_count == 0);
    g_cdrom.status.BUSYSTS = false;
    g_cdrom.command_pending = false;
    g_cdrom.response_pending = false;
    remove_event(g_cdrom.first_resp_event);
    g_cdrom.first_resp_event = 0;

    enum cdrom_command cmd = (enum cdrom_command)command;
    switch (cmd)
    {
    case Getstat:
    {
        if (g_cdrom.param_fifo_count)
        {
            cdrom_queue_error(0x20);
            break;
        }
        // TODO: implement shell-open bit reset behavior
        g_cdrom.first_response.fifo[0] = cdrom_get_stat();
        g_cdrom.first_response.count = 1;
        g_cdrom.first_response.cause = 3;
        break;
    }
    case Setloc:
    {
        if (g_cdrom.param_fifo_count != 3)
        {
            cdrom_queue_error(0x20);
            break;
        }

        u8 amm = g_cdrom.param_fifo[0];
        u8 ass = g_cdrom.param_fifo[1];
        u8 asect = g_cdrom.param_fifo[2];

        if (!is_valid_bcd(amm) || !is_valid_bcd(ass) || !is_valid_bcd(asect))
        {
            cdrom_queue_error(0x10);
            break;
        }

        if (ass > 0x59 || asect > 0x74)
        {
            cdrom_queue_error(0x10);
            break;
        }
        
        u32 lba = (((bcd_to_decimal(amm) * 60) + bcd_to_decimal(ass)) * 75 + bcd_to_decimal(asect)) - 150;

        g_cdrom.seek_target = lba;
        g_cdrom.seek_pending = true;

        g_cdrom.first_response.fifo[0] = cdrom_get_stat() | CDR_STAT_MOTOR;
        g_cdrom.first_response.count = 1;
        g_cdrom.first_response.cause = 3;
        break;
    }
    case Play:
    {
        if (g_cdrom.param_fifo_count > 1)
        {
            cdrom_queue_error(0x20);
            break;
        }

        g_cdrom.first_response.fifo[0] = cdrom_get_stat() | CDR_STAT_MOTOR;
        g_cdrom.first_response.count = 1;
        g_cdrom.first_response.cause = 3;

        g_cdrom.state = CDROM_STATE_PLAYING;

        u8 track = g_cdrom.param_fifo[0];
        if (g_cdrom.param_fifo_count && track)
        {
            struct disk_track *starting_track = &g_psx.disk->tracks[track - 1];
            g_cdrom.loc = starting_track->start;
            g_cdrom.track_end = starting_track->end;
        }
        else
        {
            if (g_cdrom.seek_pending)
            {
                g_cdrom.seek_pending = false;
                g_cdrom.loc = g_cdrom.seek_target;
            }
            struct disk_track *starting_track = &g_psx.disk->tracks[cdrom_get_current_track() - 1];
            g_cdrom.track_end = starting_track->end;
        }

        u32 next_int = cdrom_play_next_sector();
        g_cdrom.active_event = schedule_event(cdrom_play_handler, 0, next_int);

        break;
    }
    case ReadN:
    {
        cdrom_begin_read();
        break;
    }
    case MotorOn:
    {
        u8 stat = cdrom_get_stat();
        if (stat & CDR_STAT_MOTOR)
        {
            cdrom_queue_error(0x20);
            break;
        }
        else
        {
            g_cdrom.state = CDROM_STATE_IDLE;
        }

        g_cdrom.first_response.cause = 3;
        g_cdrom.first_response.count = 1;
        g_cdrom.first_response.fifo[0] = stat;

        g_cdrom.response_pending = true;
        g_cdrom.response_delay_cycles = INT_DELAY;
        g_cdrom.queued_response.cause = 2;
        g_cdrom.queued_response.count = 1;
        g_cdrom.queued_response.fifo[0] = cdrom_get_stat();
        break;
    }
    case Stop:
    {
        if (g_cdrom.param_fifo_count) 
        {
            cdrom_queue_error(0x20);
            break;
        }

        s32 delay = (g_cdrom.state != CDROM_STATE_STOPPED) ? INT_DELAY : 30000000;
        
        g_cdrom.state = CDROM_STATE_IDLE;
        g_cdrom.first_response.fifo[0] = cdrom_get_stat();
        g_cdrom.first_response.count = 1;
        g_cdrom.first_response.cause = 3;

        remove_event(g_cdrom.active_event);

        g_cdrom.loc = 150;

        g_cdrom.state = CDROM_STATE_STOPPED;
        g_cdrom.response_pending = true;
        g_cdrom.queued_response.cause = 2;
        g_cdrom.queued_response.fifo[0] = 0;
        g_cdrom.queued_response.count = 1;
        g_cdrom.response_delay_cycles = delay;
        break;
    }
    case Pause:
    {
        g_cdrom.first_response.fifo[0] = cdrom_get_stat();
        g_cdrom.first_response.count = 1;
        g_cdrom.first_response.cause = 3;

        u32 delay;
        if (g_cdrom.state != CDROM_STATE_READING)
        {
            delay = 7000;
        }
        else if (g_cdrom.mode & CDR_MODE_SPEED)
        {
            delay = (PAUSE_DELAY >> 1);
        }
        else
        {
            delay = PAUSE_DELAY;
        }

        g_cdrom.state = CDROM_STATE_IDLE;
        remove_event(g_cdrom.active_event);

        g_cdrom.response_pending = true;
        g_cdrom.queued_response.cause = 2;
        g_cdrom.queued_response.fifo[0] = cdrom_get_stat();
        g_cdrom.queued_response.count = 1;
        g_cdrom.response_delay_cycles = delay;

        break;
    }
    case Init:
    {
        if (g_cdrom.param_fifo_count) 
        {
            cdrom_queue_error(0x20);
            break;
        }

        g_cdrom.sector_size = 0x924;
        g_cdrom.sector_offset = 12;
        g_cdrom.mode = 0x20;

        s32 delay = (g_cdrom.state != CDROM_STATE_STOPPED) ? INT_DELAY : 2000000;
        
        u8 stat = cdrom_get_stat();
        g_cdrom.first_response.fifo[0] = stat;
        g_cdrom.first_response.count = 1;
        g_cdrom.first_response.cause = 3;

        remove_event(g_cdrom.active_event);

        g_cdrom.state = CDROM_STATE_IDLE;
        g_cdrom.response_pending = true;
        g_cdrom.queued_response.cause = 2;
        g_cdrom.queued_response.fifo[0] = CDR_STAT_MOTOR;
        g_cdrom.queued_response.count = 1;
        g_cdrom.response_delay_cycles = delay;
        // ref: abort all commands?
        break;
    }
    case Demute:
    {
        if (g_cdrom.param_fifo_count) 
        {
            cdrom_queue_error(0x20);
            break;
        }

        g_cdrom.first_response.cause = 3;
        g_cdrom.first_response.count = 1;
        g_cdrom.first_response.fifo[0] = cdrom_get_stat();

        break;
    }
    case Setfilter:
    {
        if (g_cdrom.param_fifo_count != 2)
        {
            cdrom_queue_error(0x20);
            break;
        }

        g_cdrom.xa_file = g_cdrom.param_fifo[0];
        g_cdrom.xa_channel = g_cdrom.param_fifo[1];

        g_cdrom.first_response.cause = 3;
        g_cdrom.first_response.count = 1;
        g_cdrom.first_response.fifo[0] = cdrom_get_stat();
        break;
    }
    case Setmode:
    {
        if (g_cdrom.param_fifo_count != 1)
        {
            cdrom_queue_error(0x20);
            break;
        }

        u8 prev = g_cdrom.mode & CDR_MODE_SPEED;

        u8 mode = g_cdrom.param_fifo[0];

        // speed switch delay is around 650ms
        if ((mode & CDR_MODE_SPEED) != prev)
        {
            g_cdrom.pending_speed_switch_delay = true;
        }
        
        if (!(mode & CDR_MODE_IGNORE))
        {
            if (mode & CDR_MODE_SIZE)
            {
                g_cdrom.sector_size = 0x924;
                g_cdrom.sector_offset = 12;
            }
            else
            {
                g_cdrom.sector_size = 0x800;
                g_cdrom.sector_offset = 24;
            }
        }

        g_cdrom.mode = mode;
        
        g_cdrom.first_response.fifo[0] = cdrom_get_stat();
        g_cdrom.first_response.count = 1;
        g_cdrom.first_response.cause = 3;
        break;
    }
    case GetlocL:
    {
        if (g_cdrom.state != CDROM_STATE_READING)
        {
            cdrom_queue_error(0x80);
            break;
        }

        break;
    }
    case GetTN:
    {
        if (g_cdrom.param_fifo_count)
        {
            cdrom_queue_error(0x20);
            break;
        }

        g_cdrom.first_response.cause = 3;
        g_cdrom.first_response.count = 3;
        g_cdrom.first_response.fifo[0] = cdrom_get_stat();
        g_cdrom.first_response.fifo[1] = 1;
        g_cdrom.first_response.fifo[2] = decimal_to_bcd((u8)g_psx.disk->track_count);
        break;
    }
    case GetTD:
    {
        if (g_cdrom.param_fifo_count != 1)
        {
            cdrom_queue_error(0x20);
            break;
        }

        u8 track_bcd = g_cdrom.param_fifo[0];

        if (!is_valid_bcd(track_bcd))
        {
            cdrom_queue_error(0x10);
            break;
        }

        u8 track_no = bcd_to_decimal(track_bcd);

        disk_image *disk = g_psx.disk;

        if (track_no > disk->track_count)
        {
            cdrom_queue_error(0x10);
            break;
        }

        MSF pos;

        if (track_no == 0)
        {
            // index 0 returns the end of the last track
            struct disk_track *track = &disk->tracks[disk->track_count - 1];
            pos = lba_to_msf(track->end);
        }
        else
        {
            struct disk_track *track = &disk->tracks[track_no - 1];
            u32 lba = track->start + track->pregap;
            pos = lba_to_msf(lba);
        }

        g_cdrom.first_response.cause = 3;
        g_cdrom.first_response.count = 3;
        g_cdrom.first_response.fifo[0] = cdrom_get_stat();
        g_cdrom.first_response.fifo[1] = decimal_to_bcd(pos.m);
        g_cdrom.first_response.fifo[2] = decimal_to_bcd(pos.s);
        
        break;
    }
    case Test:
    {
        if (!g_cdrom.param_fifo_count)
        {
            cdrom_queue_error(0x20);
            break;
        }
        switch (g_cdrom.param_fifo[0])
        {
        case 0x20:
            if (g_cdrom.param_fifo_count != 1)
            {
                cdrom_queue_error(0x20);
                break;
            }

            g_cdrom.first_response.fifo[0] = 0x94;
            g_cdrom.first_response.fifo[1] = 0x09;
            g_cdrom.first_response.fifo[2] = 0x19;
            g_cdrom.first_response.fifo[3] = 0xc0;
            g_cdrom.first_response.count = 4;
            g_cdrom.first_response.cause = 3;
            break;
        default:
            debug_warn("[CDROM] Test command unhandled subfunction: %02x\n", g_cdrom.param_fifo[0]);
            cdrom_queue_error(0x10);
            break;
        }
        break;
    }
    case SeekL:
    case SeekP:
    {
        if (g_cdrom.param_fifo_count)
        {
            // TODO: are reads still stopped on error?
            cdrom_queue_error(0x20);
            break;
        }

        g_cdrom.state = CDROM_STATE_IDLE;
        remove_event(g_cdrom.active_event); // ref: seeks cancel current/pending reads

        u8 stat = cdrom_get_stat();

        g_cdrom.seek_pending = false; // TODO: if this command is interrupted and the 2nd response is dropped, seek may not complete (ref?)
        g_cdrom.loc = g_cdrom.seek_target;
        g_cdrom.first_response.fifo[0] = stat;
        g_cdrom.first_response.count = 1;
        g_cdrom.first_response.cause = 3;

        u32 delay = 0;
        if (g_cdrom.pending_speed_switch_delay)
        {
            g_cdrom.pending_speed_switch_delay = false;
            delay = SPEED_SWITCH_DELAY;
        }
        else
        {
            delay = INT_DELAY;
        }

        g_cdrom.active_event = schedule_event(cdrom_seek_event, 0, delay + (CYCLES_PER_USEC * 1000));

        u8 seek_error = 0;
        SY_ASSERT(g_psx.disk);
        if (g_cdrom.loc > g_psx.disk->tracks[g_psx.disk->track_count - 1].end)
        {
            delay = 22000000;
            seek_error = 0x10;
        }
        else if (cmd == SeekL && !is_data_track(g_cdrom.loc))
        {
            delay = 150000000;
            seek_error = 0x4;
        }

        g_cdrom.response_pending = true;
        g_cdrom.response_delay_cycles = delay;
        if (seek_error != 0)
        {
            g_cdrom.queued_response.cause = 5;
            g_cdrom.queued_response.fifo[0] = stat | CDR_STAT_SEEKERR;
            g_cdrom.queued_response.fifo[1] = seek_error;
            g_cdrom.queued_response.count = 2;
        }
        else
        {
            g_cdrom.queued_response.cause = 2;
            g_cdrom.queued_response.fifo[0] = stat;
            g_cdrom.queued_response.count = 1;
        }
        g_cdrom.state = CDROM_STATE_SEEKING;
        break;
    }
    case GetID:
    {
        if (g_cdrom.param_fifo_count) 
        {
            cdrom_queue_error(0x20);
            break;
        }
        
        g_cdrom.first_response.fifo[0] = cdrom_get_stat();
        g_cdrom.first_response.count = 1;
        g_cdrom.first_response.cause = 3;

        g_cdrom.response_pending = true;
        g_cdrom.response_delay_cycles = INT_DELAY;
        g_cdrom.queued_response.cause = 2;
        g_cdrom.queued_response.count = 8;
        g_cdrom.queued_response.fifo[0] = 0x2;
        g_cdrom.queued_response.fifo[1] = 0x0;
        g_cdrom.queued_response.fifo[2] = 0x20;
        g_cdrom.queued_response.fifo[3] = 0x0;
        g_cdrom.queued_response.fifo[4] = 'S';
        g_cdrom.queued_response.fifo[5] = 'C';
        g_cdrom.queued_response.fifo[6] = 'E';
        g_cdrom.queued_response.fifo[7] = 'A';
        break;
    }
    case ReadS:
    {
        cdrom_begin_read();
        break;
    }
    default:
        debug_warn("[CDROM] Unhandled command: %02xh\n", cmd);
        break;
    }
    
    // if there was a pending response from a previous command, drop it
    if (g_cdrom.response_event)
    {
        remove_event(g_cdrom.response_event);
        g_cdrom.response_event = 0;
    }

    // empty the parameter fifo
    g_cdrom.status.PRMEMPT = true;
    g_cdrom.status.PRMWRDY = true;
    g_cdrom.param_fifo_count = 0;

    g_cdrom.response_fifo_current = 0; // TODO: lol

    if ((g_cdrom.interrupt_flag & 0x7) == 0)
    {
        memcpy(g_cdrom.response_fifo, g_cdrom.first_response.fifo, 16);
        g_cdrom.response_fifo_count = g_cdrom.first_response.count;
        g_cdrom.interrupt_flag = g_cdrom.first_response.cause;
        g_cdrom.status.RSLRRDY = true;
        debug_log("[CDROM] First response interrupt fired\n");
        set_interrupt(INTERRUPT_CDROM);
    }
    else
    {
        g_cdrom.command_pending = true;
    }
    
    g_cdrom.timestamp = g_cycles_elapsed;
}

u8 cdrom_read(u32 offset)
{
    u8 result = 0;
    switch (offset)
    {
    case 0:
    {
        result = g_cdrom.status.value;
        debug_log("[CDROM] read status register -> 0x%02x\n", result);
        break;
    }
    case 1:
    {
        u8 value = g_cdrom.response_fifo[g_cdrom.response_fifo_current++];
        g_cdrom.response_fifo_current &= 0xf;
        result = value;
        if (g_cdrom.response_fifo_current == g_cdrom.response_fifo_count)
        {
            g_cdrom.status.RSLRRDY = false;
        }
        else if (g_cdrom.response_fifo_current > g_cdrom.response_fifo_count)
        {
            result = 0;
        }
        debug_log("[CDROM] read response fifo: %02xh\n", value);
        break;
    }
    case 2:
    {
        if (g_cdrom.data_fifo_end)
        {
            u8 *sector = g_cdrom.sector_buffer[!g_cdrom.sector_index];
            result = sector[g_cdrom.data_fifo_index++];
            if (g_cdrom.data_fifo_index >= g_cdrom.data_fifo_end)
            {
                g_cdrom.status.DRQSTS = false;
                g_cdrom.data_fifo_end = 0;
            }
        }
        debug_log("[CDROM] read data fifo: %02xh\n", result);
        break;
    }
    case 3:
    {
        if (g_cdrom.status.value & 0x1)
        {
            result = g_cdrom.interrupt_flag |= ~0x1f;
            debug_log("[CDROM] read interrupt flag register: 0x%02x\n", g_cdrom.interrupt_flag);
        }
        else
        {
            result = g_cdrom.interrupt_enable |= ~0x1f;
            debug_log("[CDROM] read interrupt enable register\n");
        }
        break;
    }
    INVALID_CASE;
    }
    return result;
}

void cdrom_write(u32 offset, u8 value)
{
    u8 reg = offset + (g_cdrom.status.index) * 4;
    switch (reg)
    {
    case 0:
    case 4:
    case 8:
    case 12:
        g_cdrom.status.index = value;
        break;
    case 1:
        debug_log("[CDROM] send command <- %s\n", cdrom_cmd_to_string((enum cdrom_command)value));
        SY_ASSERT(!g_cdrom.status.BUSYSTS);
        // TODO: INTs must be ack'd before command is sent
        g_cdrom.status.BUSYSTS = true;
        schedule_event(cdrom_command, value, INT_DELAY);
        break;
    case 2: // param fifo
        SY_ASSERT(g_cdrom.param_fifo_count < 16);
        g_cdrom.param_fifo[g_cdrom.param_fifo_count++] = value;
        if (g_cdrom.param_fifo_count == 16)
        {
            g_cdrom.status.PRMWRDY = false;
        }
        g_cdrom.status.PRMEMPT = false;
        debug_log("[CDROM] send parameter: <- %02xh\n", value);
        break;
    case 3: // request reg
        // NOTE: we only consider mode bit 5 when it is requested, not during transfers
        // NOTE: this seems to behave more like a latch?
        if (value & BFRD)
        {
            // TODO: not sure on the behavior here, right now
            // we simply reset the data fifo every time BFRD is set
            // TODO: requested data should be locked- no further reads should overwrite it
            if (!g_cdrom.status.DRQSTS)
            {
                g_cdrom.data_fifo_index = g_cdrom.sector_offset;
                g_cdrom.data_fifo_end = g_cdrom.data_fifo_index + g_cdrom.sector_size;
                g_cdrom.status.DRQSTS = true;
            }
            else
                debug_warn("[CDROM] Software set BFRD with data remaining\n");
        }
        else
        {
            // data fifo is cleared when BFRD is not set
            g_cdrom.data_fifo_end = 0;
            g_cdrom.status.DRQSTS = false;
        }
        debug_log("[CDROM] request register <- 0x%02x\n", value);
        break;
    case 5:
        debug_log("[CDROM] sound map data out\n");
        break;
    case 6: // interrupt enable
        g_cdrom.interrupt_enable = value & 0x1f;
        debug_log("[CDROM] set interrupt enable register: %02x\n", value);
        break;
    case 7: // interrupt flag reg
        // ref: after acknowledge, pending cmd is sent and response fifo is cleared
        g_cdrom.interrupt_flag &= ~(value & 0x1f);
        if (value & 0x7)
        {
            SY_ASSERT((g_cdrom.interrupt_flag & 0x7) == 0);
            // TODO: handle pending command, note that if a command is sent after another without ack'ing the first response (eg. Pause, then Getstat),
            // we need to remove the pending response flag
            if (g_cdrom.command_pending)
            {
                g_cdrom.command_pending = false;
                g_cdrom.first_resp_event = schedule_event(cdrom_first_response_event, 0, 15000);
            }
            else if (g_cdrom.response_pending)
            {
                g_cdrom.response_pending = false;
                // if there is a pending response while a cause has not been ack'd yet, there is a fixed delay from the controller
                u64 cycles_elapsed = g_cycles_elapsed - g_cdrom.timestamp;
                if (cycles_elapsed > (u64)g_cdrom.response_delay_cycles)
                {
                    g_cdrom.response_delay_cycles = 15000;
                }
                g_cdrom.response_event = schedule_event(cdrom_response_event, 0, g_cdrom.response_delay_cycles);
            }
            g_cdrom.response_fifo_current = 0;
        }

        if (value & 0x40)
        {
            g_cdrom.param_fifo_count = 0;
            g_cdrom.status.PRMEMPT = true;
        }
        debug_log("[CDROM] acknowledge interrupts: %02x\n", value);
        break;
    case 9:
        debug_log("[CDROM] sound map coding info\n");
        break;
    case 10:
        g_cdrom.pending_vol_LL = value;
        debug_log("[CDROM] left-CD to left-SPU volume\n");
        break;
    case 11:
        g_cdrom.pending_vol_LR = value;
        debug_log("[CDROM] left-CD to right-SPU volume\n");
        break;
    case 13:
        g_cdrom.pending_vol_RR = value;
        debug_log("[CDROM] right-CD to right-SPU volume\n");
        break;
    case 14:
        g_cdrom.pending_vol_RL = value;
        debug_log("[CDROM] right-CD to left-SPU volume\n");
        break;
    case 15:
        g_cdrom.vol_LL = g_cdrom.pending_vol_LL;
        g_cdrom.vol_LR = g_cdrom.pending_vol_LR;
        g_cdrom.vol_RR = g_cdrom.pending_vol_RR;
        g_cdrom.vol_RL = g_cdrom.pending_vol_RL;
        debug_log("[CDROM] audio volume apply changes\n");
        break;
    INVALID_CASE;
    }
}
