#include "pad.h"
#include "cpu.h"
#include "event.h"
#include "debug.h"

static u8 temp[] = {0xff, 0xff, 0x41, 0x5a, 0xff, 0xff, 0xff};

u16 joypad_read(struct joypad_state *pad, u32 offset)
{
    u16 result = 0;
    switch (offset)
    {
    case 0x0:
        result = pad->rx_buffer;
        pad->stat.rx_fifo_not_empty = 0;
        //debug_log("JOY_RX_DATA read: %04x\n", result);
        break;
    case 0x4:
        //result = 0x3;
        result = pad->stat.value;
        //debug_log("JOY_STAT read: %04x\n", result);
        break;
    case 0x8:
        result = pad->mode;
        //debug_log("JOY_MODE read: %04x\n", result);
        break;
    case 0xa:
        result = pad->control;
        //debug_log("JOY_CTRL read: %04x\n", result);
        break;
    case 0xe:
        result = pad->baud_reload;
        //debug_log("JOY_BAUD read: %04x\n", result);
        break;
    SY_INVALID_CASE;
    }
    return result;
}

#define JOYCTRL_WRITE_MASK 0x3f2f
#define JOYMODE_WRITE_MASK 0x13f
#define JOY_WRITE_DELAY 1088

static void pad_ack_callback(void *data, u32 param, s32 cycles_late)
{
    struct psx_state *psx = (struct psx_state *)data;
    struct joypad_state *pad = psx->pad;
    pad->stat.ack_input_level = param;
    // might not be accurate to check here
    if (pad->control & 0x1000)
    {
        psx->cpu->i_stat |= INTERRUPT_CONTROLLER;
    }
}

static void joypad_cmd(struct psx_state *psx, u8 cmd)
{
    struct joypad_state *pad = psx->pad;

    switch (pad->context)
    {
    case JOYPAD_CONTEXT_NONE:
    {
        switch (cmd)
        {
        case 0x1:
            pad->rx_buffer = 0xff;
            pad->stat.ack_input_level = 1;
            schedule_event(pad_ack_callback, psx, 0, JOY_WRITE_DELAY, EVENT_ID_DEFAULT);
            break;
        case 0x42:
            pad->context = JOYPAD_CONTEXT_READ;
            pad->command_index = 0;
            pad->rx_buffer = 0x41;
            schedule_event(pad_ack_callback, psx, 0, JOY_WRITE_DELAY, EVENT_ID_DEFAULT);
            break;
        default:
            debug_log("Unknown joypad command: %d\n", cmd);
            pad->rx_buffer = 0xff;
            break;
        }
    } break;
    case JOYPAD_CONTEXT_READ:
    {
        u8 responses[3] = {0x5a, (pad->buttons.value & 0xff), ((pad->buttons.value >> 8) & 0xff)};

        pad->rx_buffer = responses[pad->command_index++];
        // according to docs, ack doesnt get set on last byte read
        if (pad->command_index < ARRAYCOUNT(responses))
        {
            pad->stat.ack_input_level = 1;
            // /ack signal width is about 2usec?
            schedule_event(pad_ack_callback, psx, 0, JOY_WRITE_DELAY, EVENT_ID_DEFAULT);
        }
        else
        {
            pad->context = JOYPAD_CONTEXT_NONE;
        }
    } break;
    SY_INVALID_CASE;
    }

    pad->stat.rx_fifo_not_empty = 1;
}

void joypad_store(struct psx_state *psx, u32 offset, u16 value)
{
    struct joypad_state *pad = psx->pad;

    switch (offset)
    {
    case 0x0:
        //debug_log("JOY_TX_DATA store: %04x\n", value);
        pad->tx_buffer = value;
        if (pad->control & (1 << 13))
        {
            pad->rx_buffer = 0xff;
            pad->stat.rx_fifo_not_empty = 1;
            break;
        }
        joypad_cmd(psx, (u8)value);
        //schedule_event(cpu, )
        // begins transfer?
        break;
    case 0x8:
        //debug_log("JOY_MODE store: %04x\n", value);
        pad->mode = value & JOYMODE_WRITE_MASK;
        break;
    case 0xa:
        //debug_log("JOY_CTRL store: %04x\n", value);
        if (value & 0x10)
        {
            pad->stat.irq = 0;
            pad->stat.rx_parity_error = 0;
        }
        if (value & 0x40)
        {
            // TODO: not sure exactly what gets reset here
            pad->mode = 0;
            pad->baud_reload = 0;
        }

        pad->control = value & JOYCTRL_WRITE_MASK;
        break;
    case 0xe:
        u8 factor;
        pad->baud_reload = value;
        switch (pad->mode & 0x3)
        {
        case 0x0:
        case 0x1:
            factor = 1;
            break;
        case 0x2:
            factor = 16;
            break;
        case 0x3:
            factor = 64;
            break;
        }
        pad->stat.timer = (value * factor) / 2;
        //pad->cycles_at_baud_store = cpu->system_tick_count;
        //debug_log("JOY_BAUD store: %04x\n", value);
        break;
    SY_INVALID_CASE;
    }
}
