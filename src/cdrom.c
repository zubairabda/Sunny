#include "cdrom.h"
#include "cpu.h"
#include "event.h"
#include "debug.h"

#define CDROM_SECTOR_SIZE 2352

enum cdrstat_flags
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

enum cdrmode_flags
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

#define CDROM_1US 34
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

struct cdrom_state g_cdrom;

static const char *cdrom_command_to_string(enum cdrom_command command);

void cdrom_reset(void)
{
    g_cdrom.is_reading = false;
    g_cdrom.pending_response = false;
    g_cdrom.mode = 0;
    g_cdrom.stat = g_cdrom.disk ? 0 : CDR_STAT_SHELL;
    g_cdrom.status.value = 0x18; // param fifo not full, param fifo empty
    g_cdrom.interrupt_flag = 0;

    g_cdrom.response_fifo_count = g_cdrom.response_fifo_current = 0;
    g_cdrom.data_fifo_end = g_cdrom.data_fifo_index = 0;
    g_cdrom.param_fifo_count = 0;

    g_cdrom.sector_size = 0x800;
    g_cdrom.sector_offset = 24;
}

void cdrom_load_disk(disk_image *disk)
{
    if (g_cdrom.disk)
    {
        close_disk(g_cdrom.disk);
        g_cdrom.disk = NULL;
    }
    if (disk)
    {
        g_cdrom.disk = disk;
        g_cdrom.stat &= ~CDR_STAT_SHELL;
    }
}

static inline void cdrom_queue_int(s32 delay)
{
    printf("interrupt queued in %d cycles.\n", delay);
    g_cdrom.interrupt_event_id = schedule_event(set_interrupt, INTERRUPT_CDROM, delay);
}

static void cdrom_response_event(u32 param, s32 cycles_late)
{
    g_cdrom.response_event_id = 0;
    g_cdrom.status.RSLRRDY = true;
    //cpu->cdrom.status &= 0x7f; // unset busy bit

    if (g_cdrom.is_reading)
    {
        if (read_disk_data(g_cdrom.disk, g_cdrom.loc, g_cdrom.sector))
        {
            char sync[12] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
            if (!memcmp(g_cdrom.sector, sync, 12))
            {
                g_cdrom.loc += 2352;
                g_cdrom.interrupt_flag = 0x1;
                g_cdrom.response_fifo_count = 1;
                g_cdrom.response_fifo[0] = g_cdrom.stat;
                g_cdrom.response_delay_cycles = g_cdrom.mode & CDR_MODE_SPEED ? READ_2X_DELAY : READ_DELAY;
                g_cdrom.pending_response = true;
                g_cdrom.status.DRQSTS = true;
            }
            else
            {
                g_cdrom.queued_response.cause = 0x5;
                g_cdrom.response_delay_cycles = 34 * 4000000;
                g_cdrom.is_reading = false;
            }
        }
        else
        {
            g_cdrom.queued_response.cause = 0x5;
            g_cdrom.response_delay_cycles = 34 * 600000;
            g_cdrom.is_reading = false;
        }
        //g_cdrom.queued_response.cause = 0x1;
        g_cdrom.queued_response.response[0] = g_cdrom.stat | CDR_STAT_MOTOR | CDR_STAT_READING; // NOTE: flags uneeded
        g_cdrom.queued_response.response_count = 1;
        
        g_cdrom.command_timestamp = g_cycles_elapsed;
    }
    else
    {
        g_cdrom.interrupt_flag |= g_cdrom.queued_response.cause;
        g_cdrom.response_fifo_count = g_cdrom.queued_response.response_count;
        memcpy(g_cdrom.response_fifo, g_cdrom.queued_response.response, 16);
    }
}

static void cdrom_queue_response(u8 response)
{
    g_cdrom.response_fifo[0] = response;
    g_cdrom.response_fifo_count = 1;
    g_cdrom.interrupt_flag |= 0x3;
    g_cdrom.status.RSLRRDY = true;
    cdrom_queue_int(INT_DELAY);
}

static void cdrom_queue_error(u8 error) 
{
    g_cdrom.response_fifo[0] = g_cdrom.stat | CDR_STAT_ERROR;
    g_cdrom.response_fifo[1] = error;
    g_cdrom.response_fifo_count = 2;
    g_cdrom.interrupt_flag |= 0x5;
    g_cdrom.status.RSLRRDY = true;
    cdrom_queue_int(INT_DELAY);
}

static inline b8 is_valid_bcd(u8 value)
{
    return ((value & 0xf) < 0xa) && ((value & 0xf0) < 0xa0);
}

static inline u8 bcd_to_decimal(u8 bcd)
{
    return (bcd - 0x6 * (bcd >> 4));
}

static void cdrom_command(u8 command)
{
    //SY_ASSERT(g_cdrom.response_fifo_count == 0);
    //g_cdrom.status &= 0x7f;
    enum cdrom_command cmd = (enum cdrom_command)command;
    switch (cmd)
    {
    case Getstat:
        g_cdrom.response_fifo[0] = g_cdrom.stat;
        g_cdrom.response_fifo_count = 1;
        
        g_cdrom.interrupt_flag |= 0x3;
        g_cdrom.status.RSLRRDY = true;
        cdrom_queue_int(INT_DELAY);
        break;
    case Setloc:
    {
        if (g_cdrom.param_fifo_count != 3)
        {
            cdrom_queue_error(0x20);
            return;
        }

        u8 amm = g_cdrom.param_fifo[0];
        u8 ass = g_cdrom.param_fifo[1];
        u8 asect = g_cdrom.param_fifo[2];

        if (!is_valid_bcd(amm) || !is_valid_bcd(ass) || !is_valid_bcd(asect))
        {
            cdrom_queue_error(0x10);
            return;
        }

        if (amm == 0x70)
        {
            printf("HEY!!\n");
        }

        if (ass > 0x59 || asect > 0x74)
        {
            cdrom_queue_error(0x10);
            return;
        }
        
        u32 lba = (((bcd_to_decimal(amm) * 60) + bcd_to_decimal(ass)) * 75 + bcd_to_decimal(asect)) - 150;

        u32 offset = lba * 2352;

        g_cdrom.target = offset;

        g_cdrom.response_fifo[0] = g_cdrom.stat | 0x2;
        g_cdrom.response_fifo_count = 1;
        g_cdrom.interrupt_flag |= 0x3;
        g_cdrom.status.RSLRRDY = true;
        cdrom_queue_int(INT_DELAY);
        break;
    }
    case ReadN:
    {
        g_cdrom.stat |= CDR_STAT_MOTOR;
        g_cdrom.response_fifo[0] = g_cdrom.stat;
        g_cdrom.response_fifo_count = 1;
        g_cdrom.interrupt_flag |= 0x3;
        g_cdrom.status.RSLRRDY = true;

        cdrom_queue_int(INT_DELAY);
#if 0
        g_cdrom.stat |= CDROM_STAT_READING;
        g_cdrom.pending_response = true;
        g_cdrom.queued_response.cause = 0x1;
        g_cdrom.queued_response.response[0] = g_cdrom.stat;
        g_cdrom.queued_response.response_count = 1;
        g_cdrom.response_delay_cycles = READ_DELAY;
#endif

        if (g_cdrom.pending_speed_switch_delay)
        {
            g_cdrom.response_delay_cycles += SPEED_SWITCH_DELAY;
            g_cdrom.pending_speed_switch_delay = false;
        }

        g_cdrom.is_reading = true;
        g_cdrom.loc = g_cdrom.target;
        
        break;
    }
    case Pause:
    {
        g_cdrom.response_fifo[0] = g_cdrom.stat;
        g_cdrom.response_fifo_count = 1;
        g_cdrom.interrupt_flag |= 0x3;
        g_cdrom.status.RSLRRDY = true;

        u32 delay;
        if (!g_cdrom.is_reading)
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

        g_cdrom.is_reading = false;

        remove_event(g_cdrom.interrupt_event_id);
        remove_event(g_cdrom.response_event_id); // any pending response (from a read for example) is removed so it cannot occur before this
        // TODO: can we just check during a read if there is a pending pause cmd?
        printf("Removed CDROM interrupts.\n");
        cdrom_queue_int(INT_DELAY);

        g_cdrom.stat &= ~(CDR_STAT_READING | CDR_STAT_PLAYING);
        g_cdrom.pending_response = true;
        g_cdrom.queued_response.cause = 0x2;
        g_cdrom.queued_response.response[0] = g_cdrom.stat;
        g_cdrom.queued_response.response_count = 1;
        g_cdrom.response_delay_cycles = delay;

        break;
    }
    case Init:
    {
        if (g_cdrom.param_fifo_count) 
        {
            cdrom_queue_error(0x20);
            return;
        }
        g_cdrom.sector_size = 0x924;
        g_cdrom.sector_offset = 12;
        g_cdrom.mode = 0x20;

        g_cdrom.stat |= CDR_STAT_MOTOR;
        g_cdrom.response_fifo[0] = g_cdrom.stat;
        g_cdrom.interrupt_flag |= 0x3;
        g_cdrom.status.RSLRRDY = true;
        g_cdrom.response_fifo_count = 1;

        g_cdrom.pending_response = true;
        g_cdrom.queued_response.cause = 0x2;
        g_cdrom.queued_response.response[0] = g_cdrom.stat;
        g_cdrom.queued_response.response_count = 1;
        g_cdrom.response_delay_cycles = 2000000;

        cdrom_queue_int(INT_DELAY);
        // ref: abort all commands?
        // TODO: disable reads
        break;
    }
    case Demute:
    {
        g_cdrom.response_fifo[0] = g_cdrom.stat;
        g_cdrom.interrupt_flag |= 0x3;
        g_cdrom.status.RSLRRDY = true;
        g_cdrom.response_fifo_count = 1;
        cdrom_queue_int(INT_DELAY);
        break;
    }
    case Setfilter:
    {
        break;
    }
    case Setmode:
    {
        if (g_cdrom.param_fifo_count != 1)
        {
            cdrom_queue_error(0x20);
            return;
        }

        u8 prev = g_cdrom.mode & CDR_MODE_SPEED;

        g_cdrom.mode = g_cdrom.param_fifo[0];

        // speed switch delay is around 650ms
        if ((g_cdrom.mode & CDR_MODE_SPEED) != prev)
        {
            g_cdrom.pending_speed_switch_delay = true;
        }
        
        if (!(g_cdrom.mode & CDR_MODE_IGNORE))
        {
            if (g_cdrom.mode & CDR_MODE_SIZE)
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
        
        g_cdrom.response_fifo[0] = g_cdrom.stat | 0x2;
        g_cdrom.response_fifo_count = 1;
        
        g_cdrom.interrupt_flag |= 0x3;
        g_cdrom.status.RSLRRDY = true;
        cdrom_queue_int(INT_DELAY);
        break;
    }
    case GetlocL:
    {
        if (!g_cdrom.is_reading)
        {
            cdrom_queue_error(0x80);
            return;
        }

        break;
    }
    case GetTN:
    {
        break;
    }
    case Test:
    {
        if (!g_cdrom.param_fifo_count)
        {
            cdrom_queue_error(0x20);
            return;
        }
        switch (g_cdrom.param_fifo[0])
        {
        case 0x20:
            if (g_cdrom.param_fifo_count != 1)
            {
                cdrom_queue_error(0x20);
                return;
            }

            g_cdrom.response_fifo[0] = 0x94;
            g_cdrom.response_fifo[1] = 0x09;
            g_cdrom.response_fifo[2] = 0x19;
            g_cdrom.response_fifo[3] = 0xc0;
            g_cdrom.response_fifo_count = 4;

            g_cdrom.interrupt_flag |= 0x3;
            g_cdrom.status.RSLRRDY = true;
            //schedule_event(cpu, cdrom_ack, DATA_PARAM(0x3), INT_DELAY, EVENT_ID_CDROM_CAUSE);
            cdrom_queue_int(INT_DELAY);
            break;
        
        default:
            debug_log("[CDROM] Test command unhandled subfunction: %02x\n", g_cdrom.param_fifo[0]);
            cdrom_queue_error(0x10);
            break;
        }
        break;
    }
    case SeekL:
    {
        g_cdrom.is_reading = false; // TODO: seeks supposedly stop any pending reads
        g_cdrom.loc = g_cdrom.target;
        g_cdrom.response_fifo[0] = g_cdrom.stat | CDR_STAT_MOTOR;
        g_cdrom.response_fifo_count = 1;
        g_cdrom.interrupt_flag |= 0x3;
        g_cdrom.status.RSLRRDY = true;

        cdrom_queue_int(INT_DELAY);

        g_cdrom.pending_response = true;
        g_cdrom.response_delay_cycles = INT_DELAY;
        g_cdrom.queued_response.cause = 0x2;
        g_cdrom.queued_response.response[0] = g_cdrom.stat | CDR_STAT_MOTOR | CDR_STAT_SEEKING;
        g_cdrom.queued_response.response_count = 1;
        break;
    }
    case GetID:
    {
        if (g_cdrom.param_fifo_count) 
        {
            g_cdrom.response_fifo[0] = g_cdrom.stat | 0x3;
            g_cdrom.response_fifo[1] = 0x20;
            g_cdrom.response_fifo_count = 2;
            g_cdrom.interrupt_flag |= 0x5;
            g_cdrom.status.RSLRRDY = true;
            cdrom_queue_int(INT_DELAY);
            return;
        }
        g_cdrom.stat |= 0x2;
        g_cdrom.response_fifo[0] = g_cdrom.stat;
        g_cdrom.response_fifo_count = 1;
        g_cdrom.interrupt_flag |= 0x3;
        g_cdrom.status.RSLRRDY = true;
        cdrom_queue_int(INT_DELAY);

        g_cdrom.pending_response = true;
        g_cdrom.response_delay_cycles = INT_DELAY;
        g_cdrom.queued_response.cause = 0x2;
        g_cdrom.queued_response.response_count = 8;
        g_cdrom.queued_response.response[0] = 0x2;
        g_cdrom.queued_response.response[1] = 0x0;
        g_cdrom.queued_response.response[2] = 0x20;
        g_cdrom.queued_response.response[3] = 0x0;
        g_cdrom.queued_response.response[4] = 0x53;
        g_cdrom.queued_response.response[5] = 0x43;
        g_cdrom.queued_response.response[6] = 0x45;
        g_cdrom.queued_response.response[7] = 0x41;
        break;
    }
    case ReadS:
    {
        break;
    }
    default:
        debug_log("[CDROM] Unhandled command: %02xh\n", cmd);
        break;
    }
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
        // TODO: clear to zero every reset?
        u8 value = g_cdrom.response_fifo[g_cdrom.response_fifo_current++];
        g_cdrom.response_fifo_current &= 0xf;
        result = value;
        if (g_cdrom.response_fifo_current == g_cdrom.response_fifo_count)
        {
            g_cdrom.status.RSLRRDY = false;
            //g_cdrom.response_fifo_count = 0;
            //g_cdrom.response_fifo_current = 0;
            debug_log("[CDROM] read last response byte\n");
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
            result = g_cdrom.sector[g_cdrom.data_fifo_index++];
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

void cdrom_store(u32 offset, u8 value)
{
    u8 reg = offset + (g_cdrom.status.index) * 4;
    switch (reg)
    {
    case 0:
    case 4:
    case 8:
    case 12:
        g_cdrom.status.index = value;
        //debug_log("[CDROM] status index <- %hhu\n", value);
        break;
    case 1:
        //SY_ASSERT(!(g_cdrom.interrupt_flag & 0x1f)); // interrupts must be acknowledged before sending a new command
        debug_log("[CDROM] send command <- %s\n", cdrom_command_to_string((enum cdrom_command)value));
        g_cdrom.status.PRMEMPT = true; // TODO: set this when the command begins
        cdrom_command(value);
        g_cdrom.param_fifo_count = 0;
        g_cdrom.command_timestamp = g_cycles_elapsed;
        break;
    case 2: // param fifo
        SY_ASSERT(g_cdrom.param_fifo_count < 16);
        g_cdrom.param_fifo[g_cdrom.param_fifo_count++] = value;
        if (g_cdrom.param_fifo_count == 16)
        {
            g_cdrom.status.PRMWRDY = false;
        }
        g_cdrom.status.PRMEMPT = false;
        debug_log("[CDROM] send parameter: <- 0x%02x\n", value);
        break;
    case 3: // request reg
        // NOTE: we only consider mode bit 5 when it is requested, not during transfers
        if (value & BFRD)
        {
            if (!g_cdrom.status.DRQSTS)
            {
                g_cdrom.data_fifo_index = g_cdrom.sector_offset;
                g_cdrom.data_fifo_end = g_cdrom.data_fifo_index + g_cdrom.sector_size;
                g_cdrom.status.DRQSTS = true;
            }
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
    case 6: // interrupt en
        g_cdrom.interrupt_enable = value & 0x1f;
        debug_log("[CDROM] set interrupt enable register: %02x\n", value);
        break;
    case 7: // interrupt flag reg
        // NOTE: response interrupts are queued
        // NOTE: spec says after acknowledge, pending cmd is sent and response fifo is cleared
        g_cdrom.interrupt_flag &= ~(value & 0x1f); // writing to bits resets them
        if (value & 0x7)
        {
            // TODO: handle pending command
            if (g_cdrom.pending_response)
            {
                g_cdrom.pending_response = false;
                // if there is a pending response while a cause has not been ack'd yet, there is a fixed delay from the controller
                u64 cycles_elapsed = g_cycles_elapsed - g_cdrom.command_timestamp;
                if ((u32)cycles_elapsed > g_cdrom.response_delay_cycles)
                {
                    g_cdrom.response_delay_cycles = 17000;
                }
                g_cdrom.response_event_id = schedule_event(cdrom_response_event, 0, g_cdrom.response_delay_cycles);
                cdrom_queue_int(g_cdrom.response_delay_cycles);
                
            }
            g_cdrom.response_fifo_current = 0;
        }
        if (value & 0x40) // ref: psx-spx
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
        debug_log("[CDROM] left-CD to left-SPU volume\n");
        break;
    case 11:
        debug_log("[CDROM] left-CD to right-SPU volume\n");
        break;
    case 13:
        debug_log("[CDROM] right-CD to right-SPU volume\n");
        break;
    case 14:
        debug_log("[CDROM] right-CD to left-SPU volume\n");
        break;
    case 15:
        debug_log("[CDROM] audio volume apply changes\n");
        break;
    INVALID_CASE;
    }
}

static const char *cdrom_command_to_string(enum cdrom_command command)
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
        return NULL;
    }
}
