#include "cdrom.h"
#include "cpu.h"
#include "event.h"
#include "debug.h"

#define CDROM_SECTOR_SIZE 2352

#define CDROM_STAT_PLAYING   (1 << 7)
#define CDROM_STAT_SEEKING   (1 << 6)
#define CDROM_STAT_READING   (1 << 5)
#define CDROM_STAT_SHELLOPEN (1 << 4)
#define CDROM_STAT_IDERR     (1 << 3)
#define CDROM_STAT_SEEKERR   (1 << 2)
#define CDROM_STAT_MOTORON   (1 << 1)
#define CDROM_STAT_ERROR     (1 << 0)

#define CDROM_MODE_SPEED      (1 << 7)
#define CDROM_MODE_XAADPCM    (1 << 6)
#define CDROM_MODE_SECTORSIZE (1 << 5)
#define CDROM_MODE_IGNOREBIT  (1 << 4)
#define CDROM_MODE_XAFILTER   (1 << 3)
#define CDROM_MODE_REPORT     (1 << 2)
#define CDROM_MODE_AUTOPAUSE  (1 << 1)
#define CDROM_MODE_CDDA       (1 << 0)

#define INT_DELAY 50401
#define READ_DELAY 451584
#define PAUSE_DELAY 2500000

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

void cdrom_init(platform_file disk)
{
    g_cdrom.status = CDR_STATUS_PARAM_FIFO_EMPTY | CDR_STATUS_PARAM_FIFO_NOT_FULL; // param fifo empty, param fifo not full
    
    g_cdrom.sector_size = 0x800;
    g_cdrom.sector_offset = 24;

    if (!file_is_valid(disk))
    {
        g_cdrom.stat |= CDROM_STAT_SHELLOPEN;
    }
    else
    {
        g_cdrom.disk = disk;
    }
    g_cdrom.stat |= CDROM_STAT_SHELLOPEN;
}

static void cdrom_response_event(u32 param, s32 cycles_late)
{
    g_cdrom.status |= 0x20; // response fifo not empty
   // cpu->cdrom.status &= 0x7f; // unset busy bit
    g_cdrom.interrupt_flag |= g_cdrom.queued_response.cause;
    g_cdrom.response_fifo_count = g_cdrom.queued_response.response_count;

    memcpy(g_cdrom.response_fifo, g_cdrom.queued_response.response, 16);
#if 1
    if (g_cdrom.is_reading)
    {
        //SY_ASSERT(g_cdrom.sector_offset == 24);
        //read_file(g_cdrom.disk, g_cdrom.target + g_cdrom.sector_offset, g_cdrom.sector, g_cdrom.sector_size);
        read_file(g_cdrom.disk, g_cdrom.loc, g_cdrom.sector, 2352);
        g_cdrom.loc += 2352;
        //g_cdrom.status |= CDR_STATUS_DATA_FIFO_NOT_EMPTY;
        g_cdrom.pending_response = 1;
        g_cdrom.queued_response.cause = 0x1;
        g_cdrom.queued_response.response[0] = g_cdrom.stat | CDROM_STAT_MOTORON | CDROM_STAT_READING; // NOTE: flags uneeded
        g_cdrom.queued_response.response_count = 1;
        g_cdrom.response_delay_cycles = READ_DELAY;
    }
    
#endif
}

static void cdrom_queue_response(u8 response)
{
    g_cdrom.response_fifo[0] = response;
    g_cdrom.response_fifo_count = 1;
    g_cdrom.interrupt_flag |= 0x3;
    g_cdrom.status |= 0x20;
    schedule_event(set_interrupt, INTERRUPT_CDROM, INT_DELAY);
}

static void cdrom_queue_error(u8 error) 
{
    g_cdrom.response_fifo[0] = g_cdrom.stat | CDROM_STAT_ERROR;
    g_cdrom.response_fifo[1] = error;
    g_cdrom.response_fifo_count = 2;
    g_cdrom.interrupt_flag |= 0x5;
    g_cdrom.status |= 0x20;
    schedule_event(set_interrupt, INTERRUPT_CDROM, INT_DELAY);
}

static inline b8 is_valid_bcd(u8 value)
{
    return ((value & 0xf) < 0xa) && ((value & 0xf0) < 0xa0);
}

static inline u8 bcd_to_decimal(u8 bcd)
{
    return bcd - 0x6 * (bcd >> 4);
}

static void cdrom_command(u8 cmd)
{
    //SY_ASSERT(g_cdrom.response_fifo_count == 0);
    g_cdrom.response_delay_cycles = INT_DELAY; // TODO: remove
    //g_cdrom.status &= 0x7f;
    switch ((enum cdrom_command)cmd)
    {
    case Getstat:
        g_cdrom.response_fifo[0] = g_cdrom.stat;
        g_cdrom.response_fifo_count = 1;
        
        g_cdrom.interrupt_flag |= 0x3;
        g_cdrom.status |= 0x20;
        schedule_event(set_interrupt, INTERRUPT_CDROM, INT_DELAY);
        break;
    case Setloc:
    {
        if (g_cdrom.param_fifo_count != 3) {
            cdrom_queue_error(0x20);
            return;
        }

        u8 amm = g_cdrom.param_fifo[0];
        u8 ass = g_cdrom.param_fifo[1];
        u8 asect = g_cdrom.param_fifo[2];

        if (!is_valid_bcd(amm) || !is_valid_bcd(ass) || !is_valid_bcd(asect)) {
            cdrom_queue_error(0x10);
            return;
        }

        if (amm > 0x73 || ass > 0x59 || asect > 0x74) {
            cdrom_queue_error(0x10);
            return;
        }

        //SY_ASSERT(ass >= 2);
        u32 lba = (((bcd_to_decimal(amm) * 60) + bcd_to_decimal(ass)) * 75 + bcd_to_decimal(asect)) - 150;

        u32 offset = lba * 2352;
        /*
        u32 offset = 2352 * bcd_to_decimal(asect);
        offset += 2352 * 75 * bcd_to_decimal(ass - 2);
        offset += 2352 * 75 * 60 * bcd_to_decimal(amm);
        */
        g_cdrom.target = offset;

        //char buf[1024];
        //read_file(g_cdrom.disk, offset, buf, 1024);

        g_cdrom.response_fifo[0] = g_cdrom.stat | 0x2;
        g_cdrom.response_fifo_count = 1;
        g_cdrom.interrupt_flag |= 0x3;
        g_cdrom.status |= 0x20;
        schedule_event(set_interrupt, INTERRUPT_CDROM, INT_DELAY);
        break;
    }
    case ReadN:
    {
        g_cdrom.stat |= CDROM_STAT_MOTORON;
        g_cdrom.response_fifo[0] = g_cdrom.stat;
        g_cdrom.response_fifo_count = 1;
        g_cdrom.interrupt_flag |= 0x3;
        g_cdrom.status |= 0x20;

        schedule_event(set_interrupt, INTERRUPT_CDROM, INT_DELAY);

        g_cdrom.stat |= CDROM_STAT_READING;
        g_cdrom.pending_response = 1;
        g_cdrom.queued_response.cause = 0x1;
        g_cdrom.queued_response.response[0] = g_cdrom.stat;
        g_cdrom.queued_response.response_count = 1;
        g_cdrom.response_delay_cycles = READ_DELAY;
        g_cdrom.is_reading = 1;
        g_cdrom.loc = g_cdrom.target;
        break;
    }
    case Pause:
    {
        g_cdrom.response_fifo[0] = g_cdrom.stat;
        g_cdrom.response_fifo_count = 1;
        g_cdrom.interrupt_flag |= 0x3;
        g_cdrom.status |= CDR_STATUS_RESPONSE_FIFO_NOT_EMPTY;

        g_cdrom.is_reading = 0;

        schedule_event(set_interrupt, INTERRUPT_CDROM, INT_DELAY);

        g_cdrom.stat &= ~(CDROM_STAT_READING | CDROM_STAT_PLAYING);
        g_cdrom.pending_response = 1;
        g_cdrom.queued_response.cause = 0x2;
        g_cdrom.queued_response.response[0] = g_cdrom.stat;
        g_cdrom.queued_response.response_count = 1;
        //g_cdrom.response_delay_cycles = PAUSE_DELAY;

        break;
    }
    case Init:
    {
        if (g_cdrom.param_fifo_count) 
        {
            cdrom_queue_error(0x20);
            #if 0
            g_cdrom.pending_response = 1;
            g_cdrom.queued_response.cause = 0x3;
            g_cdrom.queued_response.response_count = 1;
            g_cdrom.queued_response.response[0] = g_cdrom.stat | 0x2;
            #endif
            return;
        }
        g_cdrom.sector_size = 0x924;
        g_cdrom.sector_offset = 12;
        g_cdrom.mode = 0x20;

        g_cdrom.stat |= CDROM_STAT_MOTORON;
        g_cdrom.response_fifo[0] = g_cdrom.stat;
        g_cdrom.interrupt_flag |= 0x3;
        g_cdrom.status |= 0x20;
        g_cdrom.response_fifo_count = 1;
        #if 0
        g_cdrom.queued_interrupt = 0x2;
        g_cdrom.queued_response[0] = g_cdrom.stat;
        g_cdrom.queued_response_count = 1;
        #else
        g_cdrom.pending_response = 1;
        g_cdrom.queued_response.cause = 0x2;
        g_cdrom.queued_response.response[0] = g_cdrom.stat;
        g_cdrom.queued_response.response_count = 1;
        #endif
        g_cdrom.response_delay_cycles = 2000000;

        schedule_event(set_interrupt, INTERRUPT_CDROM, INT_DELAY);
        // ref: abort all commands?
        break;
    }
    case Demute:
    {
        g_cdrom.response_fifo[0] = g_cdrom.stat;
        g_cdrom.interrupt_flag |= 0x3;
        g_cdrom.status |= CDR_STATUS_RESPONSE_FIFO_NOT_EMPTY;
        g_cdrom.response_fifo_count = 1;
        schedule_event(set_interrupt, INTERRUPT_CDROM, INT_DELAY);
        break;
    }
    case Setmode:
    {
        if (g_cdrom.param_fifo_count != 1) {
            cdrom_queue_error(0x20);
            return;
        }
        g_cdrom.mode = g_cdrom.param_fifo[0];
        
        if (!(g_cdrom.mode & (1 << 4))) {
            if (g_cdrom.mode & (1 << 5)) {
                g_cdrom.sector_size = 0x924;
                g_cdrom.sector_offset = 12;
            }
            else {
                g_cdrom.sector_size = 0x800;
                g_cdrom.sector_offset = 24;
            }
        }
        
        g_cdrom.response_fifo[0] = g_cdrom.stat | 0x2;
        g_cdrom.response_fifo_count = 1;
        
        g_cdrom.interrupt_flag |= 0x3;
        g_cdrom.status |= 0x20;
        schedule_event(set_interrupt, INTERRUPT_CDROM, INT_DELAY);
        break;
    }
    case GetlocL:
    {
        if (!g_cdrom.is_reading) {
            cdrom_queue_error(0x80);
            return;
        }

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
            if (g_cdrom.param_fifo_count != 1) {
                cdrom_queue_error(0x20);
                return;
            }

            g_cdrom.response_fifo[0] = 0x94;
            g_cdrom.response_fifo[1] = 0x09;
            g_cdrom.response_fifo[2] = 0x19;
            g_cdrom.response_fifo[3] = 0xc0;
            g_cdrom.response_fifo_count = 4;

            g_cdrom.interrupt_flag |= 0x3;
            g_cdrom.status |= 0x20;
            //schedule_event(cpu, cdrom_ack, DATA_PARAM(0x3), INT_DELAY, EVENT_ID_CDROM_CAUSE);
            schedule_event(set_interrupt, INTERRUPT_CDROM, INT_DELAY);
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
        g_cdrom.is_reading = 0; // TODO: seeks supposedly stop any pending reads
        g_cdrom.loc = g_cdrom.target;
        g_cdrom.response_fifo[0] = g_cdrom.stat | CDROM_STAT_MOTORON;
        g_cdrom.response_fifo_count = 1;
        g_cdrom.interrupt_flag |= 0x3;
        g_cdrom.status |= 0x20;

        schedule_event(set_interrupt, INTERRUPT_CDROM, INT_DELAY);

        g_cdrom.pending_response = 1;
        g_cdrom.queued_response.cause = 0x2;
        g_cdrom.queued_response.response[0] = g_cdrom.stat | CDROM_STAT_MOTORON | CDROM_STAT_SEEKING;
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
            g_cdrom.status |= 0x20;
            schedule_event(set_interrupt, INTERRUPT_CDROM, INT_DELAY);
            return;
        }
        g_cdrom.stat |= 0x2;
        g_cdrom.response_fifo[0] = g_cdrom.stat;
        g_cdrom.response_fifo_count = 1;
        g_cdrom.interrupt_flag |= 0x3;
        g_cdrom.status |= 0x20;
        //schedule_event(cpu, cdrom_ack, DATA_PARAM(0x3), INT_DELAY, EVENT_ID_CDROM_CAUSE);
        schedule_event(set_interrupt, INTERRUPT_CDROM, INT_DELAY);

        g_cdrom.pending_response = 1;
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
    case 0: // reg 0 is always the status
        result = g_cdrom.status;
        debug_log("[CDROM] read status register -> 0x%02x\n", result);
        break;
    case 1: // reg 1 is always the response fifo
        // TODO: clear to zero every reset?
        u8 value = g_cdrom.response_fifo[g_cdrom.response_fifo_current++];
        g_cdrom.response_fifo_current &= 0xf;
        result = value;
        if (g_cdrom.response_fifo_current == g_cdrom.response_fifo_count)
        {
            g_cdrom.status &= ~(0x20);
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
    case 2: // reg 2 is always the data fifo
        if (g_cdrom.data_fifo_end)
        {
            result = g_cdrom.sector[g_cdrom.data_fifo_index++];
            if (g_cdrom.data_fifo_index >= g_cdrom.data_fifo_end)
            {
                g_cdrom.status &= ~CDR_STATUS_DATA_FIFO_NOT_EMPTY;
                g_cdrom.data_fifo_end = 0;
            }
        }
        debug_log("[CDROM] read data fifo\n");
        break;
    case 3:
        if (g_cdrom.status & 0x1) // index 1 or 3 = interrupt flag reg
        {
            result = g_cdrom.interrupt_flag |= ~0x1f;
            debug_log("[CDROM] read interrupt flag register: 0x%02x\n", g_cdrom.interrupt_flag);
        }
        else // 0 or 2 = interrupt enable register
        {
            result = g_cdrom.interrupt_enable |= ~0x1f;
            debug_log("[CDROM] read interrupt enable register\n");
        }
        break;
    INVALID_CASE;
    }
    return result;
}

void cdrom_store(u32 offset, u8 value)
{
    switch (offset + ((g_cdrom.status & 0x3) * 4))
    {
    case 0:
    case 4:
    case 8:
    case 12: // status reg (0-1 writable)
        g_cdrom.status = (g_cdrom.status & ~0x3) | (value & 0x3); // bits 0-1 writable
        //debug_log("[CDROM] status index <- %hhu\n", value);
        break;
    case 1: // command reg
        //SY_ASSERT(!(g_cdrom.interrupt_flag & 0x1f)); // interrupts must be acknowledged before sending a new command
        debug_log("[CDROM] send command <- 0x%02x\n", value);
        g_cdrom.status |= 0x8; // param fifo empty
        cdrom_command(value);
        g_cdrom.param_fifo_count = 0;
        g_cdrom.command_timestamp = g_cycles_elapsed;
        break;
    case 2: // param fifo
        SY_ASSERT(g_cdrom.param_fifo_count < 16);
        g_cdrom.param_fifo[g_cdrom.param_fifo_count++] = value;
        if (g_cdrom.param_fifo_count == 16) {
            g_cdrom.status &= ~(0x10);
        }
        g_cdrom.status &= ~(0x8);
        debug_log("[CDROM] send parameter: <- 0x%02x\n", value);
        break;
    case 3: // request reg
        // NOTE: we only consider mode bit 5 when it is requested, not during transfers
        if (value & 0x80) 
        {
            g_cdrom.data_fifo_index = g_cdrom.sector_offset;
            g_cdrom.data_fifo_end = g_cdrom.data_fifo_index + g_cdrom.sector_size;
            g_cdrom.status |= CDR_STATUS_DATA_FIFO_NOT_EMPTY;
        }
        else
        {
            g_cdrom.data_fifo_end = 0;
            g_cdrom.status &= ~CDR_STATUS_DATA_FIFO_NOT_EMPTY;
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
                g_cdrom.pending_response = 0;
                //g_cdrom.interrupt_flag |= g_cdrom.queued_interrupt;
                //g_cdrom.response_fifo_count = g_cdrom.queued_response_count;
                //memcpy(g_cdrom.response_fifo, g_cdrom.queued_response, g_cdrom.queued_response_count);
#if 1
                u64 cycles_elapsed = g_cycles_elapsed - g_cdrom.command_timestamp;
                if ((u32)cycles_elapsed > g_cdrom.response_delay_cycles)
                {
                    g_cdrom.response_delay_cycles = 17000;
                }
#endif
                schedule_event(cdrom_response_event, 0, g_cdrom.response_delay_cycles);
                schedule_event(set_interrupt, INTERRUPT_CDROM, g_cdrom.response_delay_cycles);
                
            }
            #if 0
            else
            {
                g_cdrom.response_fifo_count = 0;
            }
            #endif
            g_cdrom.response_fifo_current = 0;
        }
        if (value & 0x40) // ref: psx-spx
        {
            g_cdrom.param_fifo_count = 0;
            g_cdrom.status |= 0x8; // bit 3 set when param_fifo empty
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
#if 1
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
        return 0;
    }
}
#endif
