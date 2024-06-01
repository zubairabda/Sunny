#include "cdrom.h"
#include "psx.h"
#include "cpu.h"
#include "event.h"
#include "debug.h"

#define CDROM_RESPONSE_COUNT(...) (sizeof((u8[]){__VA_ARGS__}) / sizeof(u8))
#define CDROM_PUSH_RESPONSE(cdrom, ...) do {  \
                                            cdrom->response_fifo_count = CDROM_RESPONSE_COUNT(__VA_ARGS__); \
                                            for (int _i = 0; _i < cdrom->response_fifo_count; ++_i) {  \
                                               cdrom->response_fifo[_i] = (u8[]){__VA_ARGS__}[_i]; \
                                            } \
                                        } while(0)

void cdrom_init(struct cdrom_state *cdrom)
{
    cdrom->status = 0x18; // param fifo empty, param fifo not full
}

static void cdrom_response_event(void *data, u32 param, s32 cycles_late)
{
    struct cdrom_state *cdrom = (struct cdrom_state *)data;
    cdrom->status |= 0x20; // response fifo not empty
   // cpu->cdrom.status &= 0x7f; // unset busy bit
    cdrom->interrupt_flag |= cdrom->queued_response.cause;
    cdrom->response_fifo_count = cdrom->queued_response.response_count;
    memcpy(cdrom->response_fifo, cdrom->queued_response.response, 16);
}

static void cdrom_queue_response(struct psx_state *psx, u8 response)
{
    struct cdrom_state *cdrom = psx->cdrom;
    cdrom->response_fifo[0] = response;
    cdrom->response_fifo_count = 1;
    cdrom->interrupt_flag |= 0x3;
    cdrom->status |= 0x20;
    schedule_event(set_interrupt, psx->cpu, INTERRUPT_CDROM, 50401, EVENT_ID_DEFAULT);
}

static void cdrom_queue_error(struct psx_state *psx, u8 error) 
{
    struct cdrom_state *cdrom = psx->cdrom;
    cdrom->response_fifo[0] = cdrom->stat | 0x3;
    cdrom->response_fifo[1] = error;
    cdrom->response_fifo_count = 2;
    cdrom->interrupt_flag |= 0x5;
    cdrom->status |= 0x20;
    schedule_event(set_interrupt, psx->cpu, INTERRUPT_CDROM, 50401, EVENT_ID_DEFAULT);
}

static inline b8 is_valid_bcd(u8 value)
{
    return ((value & 0xf) < 0xa) && ((value & 0xf0) < 0xa0);
}

static void cdrom_command(struct psx_state *psx, u8 cmd)
{
    struct cdrom_state *cdrom = psx->cdrom;
    //SY_ASSERT(cdrom->response_fifo_count == 0);
    cdrom->response_delay_cycles = 50401; // TODO: remove
    //cdrom->status &= 0x7f;
    switch (cmd)
    {
    case 0x1: // Getstat
        cdrom->response_fifo[0] = cdrom->stat;
        cdrom->response_fifo_count = 1;
        
        cdrom->interrupt_flag |= 0x3;
        cdrom->status |= 0x20;
        schedule_event(set_interrupt, psx->cpu, INTERRUPT_CDROM, 50401, EVENT_ID_DEFAULT);
        break;
    case 0x2: // Setloc
        if (cdrom->param_fifo_count != 3) {
            cdrom_queue_error(psx, 0x20);
            return;
        }

        u8 amm = cdrom->param_fifo[0];
        u8 ass = cdrom->param_fifo[1];
        u8 asect = cdrom->param_fifo[2];

        if (!is_valid_bcd(amm) || !is_valid_bcd(ass) || !is_valid_bcd(asect)) {
            cdrom_queue_error(psx, 0x10);
            return;
        }

        if (ass > 0x59 || asect > 0x74) {
            cdrom_queue_error(psx, 0x10);
            return;
        }

        cdrom->response_fifo[0] = cdrom->stat | 0x2;
        cdrom->response_fifo_count = 1;
        cdrom->interrupt_flag |= 0x3;
        cdrom->status |= 0x20;
        schedule_event(set_interrupt, psx->cpu, INTERRUPT_CDROM, 50401, EVENT_ID_DEFAULT);
        break;
    case 0xa: // Init
        if (cdrom->param_fifo_count) 
        {
            cdrom_queue_error(psx, 0x20);
            #if 0
            cdrom->pending_response = 1;
            cdrom->queued_response.cause = 0x3;
            cdrom->queued_response.response_count = 1;
            cdrom->queued_response.response[0] = cdrom->stat | 0x2;
            #endif
            return;
        }
        cdrom->response_fifo[0] = cdrom->stat | 0x2;
        cdrom->interrupt_flag |= 0x3;
        cdrom->status |= 0x20;
        cdrom->response_fifo_count = 1;
        #if 0
        cdrom->queued_interrupt = 0x2;
        cdrom->queued_response[0] = cdrom->stat;
        cdrom->queued_response_count = 1;
        #else
        cdrom->pending_response = 1;
        cdrom->queued_response.cause = 0x2;
        cdrom->queued_response.response[0] = cdrom->stat | 0x2;
        cdrom->queued_response.response_count = 1;
        #endif
        cdrom->response_delay_cycles = 2000000;

        schedule_event(set_interrupt, psx->cpu, INTERRUPT_CDROM, 50401, EVENT_ID_DEFAULT);
        // ref: abort all commands?
        break;
    case 0xe: // setmode
        if (cdrom->param_fifo_count != 1) {
            cdrom_queue_error(psx, 0x20);
            return;
        }
        u8 mode = cdrom->param_fifo[0];
        cdrom->response_fifo[0] = cdrom->stat | 0x2;
        cdrom->response_fifo_count = 1;
        
        cdrom->interrupt_flag |= 0x3;
        cdrom->status |= 0x20;
        schedule_event(set_interrupt, psx->cpu, INTERRUPT_CDROM, 50401, EVENT_ID_DEFAULT);
        break;
    case 0x19: // Test
        if (!cdrom->param_fifo_count)
        {
            cdrom_queue_error(psx, 0x20);
            return;
        }
        switch (cdrom->param_fifo[0])
        {
        case 0x20:
            if (cdrom->param_fifo_count != 1) {
                cdrom_queue_error(psx, 0x20);
                return;
            }
            cdrom->response_fifo[0] = 0x94;
            cdrom->response_fifo[1] = 0x09;
            cdrom->response_fifo[2] = 0x19;
            cdrom->response_fifo[3] = 0xc0;
            cdrom->response_fifo_count = 4;

            cdrom->interrupt_flag |= 0x3;
            cdrom->status |= 0x20;
            //schedule_event(cpu, cdrom_ack, DATA_PARAM(0x3), 50401, EVENT_ID_CDROM_CAUSE);
            schedule_event(set_interrupt, psx->cpu, INTERRUPT_CDROM, 50401, EVENT_ID_DEFAULT);
            break;
        
        default:
            debug_log("[CDROM] Test command unhandled subfunction: %02x\n", cdrom->param_fifo[0]);
            cdrom_queue_error(psx, 0x10);
            break;
        }
        break;
    case 0x1a: // getID
        if (cdrom->param_fifo_count) 
        {
            cdrom->response_fifo[0] = cdrom->stat | 0x3;
            cdrom->response_fifo[1] = 0x20;
            cdrom->response_fifo_count = 2;
            cdrom->interrupt_flag |= 0x5;
            cdrom->status |= 0x20;
            schedule_event(set_interrupt, psx->cpu, INTERRUPT_CDROM, 50401, EVENT_ID_DEFAULT);
            return;
        }
        cdrom->stat |= 0x2;
        cdrom->response_fifo[0] = cdrom->stat;
        cdrom->response_fifo_count = 1;
        cdrom->interrupt_flag |= 0x3;
        cdrom->status |= 0x20;
        //schedule_event(cpu, cdrom_ack, DATA_PARAM(0x3), 50401, EVENT_ID_CDROM_CAUSE);
        schedule_event(set_interrupt, psx->cpu, INTERRUPT_CDROM, 50401, EVENT_ID_DEFAULT);

        cdrom->pending_response = 1;
        cdrom->queued_response.cause = 0x2;
        cdrom->queued_response.response_count = 8;
        cdrom->queued_response.response[0] = 0x2;
        cdrom->queued_response.response[1] = 0x0;
        cdrom->queued_response.response[2] = 0x20;
        cdrom->queued_response.response[3] = 0x0;
        cdrom->queued_response.response[4] = 0x53;
        cdrom->queued_response.response[5] = 0x43;
        cdrom->queued_response.response[6] = 0x45;
        cdrom->queued_response.response[7] = 0x41;
        break;
    default:
        debug_log("[CDROM] Unhandled command: %02xh\n", cmd);
        break;
    }
}

static inline u8 cdrom_read(struct cdrom_state *cdrom, u32 offset)
{
    u8 result = 0;
    switch (offset)
    {
    case 0: // reg 0 is always the status
        result = cdrom->status;
        debug_log("[CDROM] read status register -> 0x%02x\n", result);
        #if 0
        debug_log("Index: %d, XA-ADPCM fifo empty?: %s, Parameter fifo empty?: %s, Parameter fifo full?: %s, Response fifo empty?: %s, Data fifo empty?: %s, Command/Parameter transmission busy?: %d",
            (result & 0x3), (result >> 2) & 0x1, (result >> 3) & 0x1, (result >> 4) & 0x1, (result >> 5) & 0x1, (result >> 6) & 0x1, (result >> 7) & 0x1);
        #endif
        break;
    case 1: // reg 1 is always the response fifo
        // TODO: clear to zero every reset?
        u8 value = cdrom->response_fifo[cdrom->response_fifo_current++];
        cdrom->response_fifo_current &= 0xf;
        result = value;
        if (cdrom->response_fifo_current == cdrom->response_fifo_count)
        {
            cdrom->status &= ~(0x20);
            //cdrom->response_fifo_count = 0;
            //cdrom->response_fifo_current = 0;
            debug_log("[CDROM] read last response byte\n");
        }
        else if (cdrom->response_fifo_current > cdrom->response_fifo_count)
        {
            result = 0;
        }
        debug_log("[CDROM] read response fifo: %02xh\n", value);
        break;
    case 2: // reg 2 is always the data fifo
        debug_log("[CDROM] read data fifo\n");
        break;
    case 3:
        if (cdrom->status & 0x1) // index 1 or 3 = interrupt flag reg
        {
            result = cdrom->interrupt_flag |= ~0x1f;
            debug_log("[CDROM] read interrupt flag register: 0x%02x\n", cdrom->interrupt_flag);
        }
        else // 0 or 2 = interrupt enable register
        {
            result = cdrom->interrupt_enable |= ~0x1f;
            debug_log("[CDROM] read interrupt enable register\n");
        }
        break;
    SY_INVALID_CASE;
    }
    return result;
}

void cdrom_store(struct psx_state *psx, u32 offset, u8 value)
{
    struct cdrom_state *cdrom = psx->cdrom;

    switch (offset + ((cdrom->status & 0x3) * 4))
    {
    case 0:
    case 4:
    case 8:
    case 12: // status reg (0-1 writable)
        cdrom->status = (cdrom->status & ~0x3) | (value & 0x3); // bits 0-1 writable
        debug_log("[CDROM] status index <- %hhu\n", value);
        break;
    case 1: // command reg
        //SY_ASSERT(!(cdrom->interrupt_flag & 0x1f)); // interrupts must be acknowledged before sending a new command
        debug_log("[CDROM] send command <- 0x%02x\n", value);
        cdrom->status |= 0x8; // param fifo empty
        cdrom_command(psx, value);
        cdrom->param_fifo_count = 0;
        cdrom->command_timestamp_cycles = g_cycles_elapsed;
        break;
    case 2: // param fifo
        SY_ASSERT(cdrom->param_fifo_count < 16);
        cdrom->param_fifo[cdrom->param_fifo_count++] = value;
        if (cdrom->param_fifo_count == 16) {
            cdrom->status &= ~(0x10);
        }
        cdrom->status &= ~(0x8);
        debug_log("[CDROM] send parameter: <- 0x%02x\n", value);
        break;
    case 3: // request reg
        // TODO: mask needed?
        cdrom->request_reg = value & 0xe;
        debug_log("[CDROM] request register <- 0x%02x\n", value);
        break;
    case 5:
        debug_log("[CDROM] sound map data out\n");
        break;
    case 6: // interrupt en
        cdrom->interrupt_enable = value & 0x1f;
        debug_log("[CDROM] set interrupt enable register: %02x\n", value);
        break;
    case 7: // interrupt flag reg
        // NOTE: response interrupts are queued
        // TODO: doc says after acknowledge, pending cmd is sent and response fifo is cleared?
        cdrom->interrupt_flag &= ~(value & 0x1f); // writing to bits resets them
        if (value & 0x7)
        {
            // TODO: handle pending command
            if (cdrom->pending_response)
            {
                //cdrom->interrupt_flag |= cdrom->queued_interrupt;
                //cdrom->response_fifo_count = cdrom->queued_response_count;
                //memcpy(cdrom->response_fifo, cdrom->queued_response, cdrom->queued_response_count);
#if 1
                u64 cycles_elapsed = g_cycles_elapsed - cdrom->command_timestamp_cycles;
                if ((u32)cycles_elapsed > cdrom->response_delay_cycles)
                {
                    cdrom->response_delay_cycles = 17000;
                }
#endif
                schedule_event(cdrom_response_event, cdrom, 0, cdrom->response_delay_cycles, EVENT_ID_DEFAULT);
                schedule_event(set_interrupt, psx->cpu, INTERRUPT_CDROM, cdrom->response_delay_cycles, EVENT_ID_DEFAULT);
                cdrom->pending_response = 0;
            }
            #if 0
            else
            {
                cdrom->response_fifo_count = 0;
            }
            #endif
            cdrom->response_fifo_current = 0;
        }
        if (value & 0x40) // ref: psx-spx
        {
            cdrom->param_fifo_count = 0;
            cdrom->status |= 0x8; // bit 3 set when param_fifo empty
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
    SY_INVALID_CASE;
    }
}
