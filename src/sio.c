#include "sio.h"
#include "cpu.h"
#include "event.h"
#include "debug.h"

struct sio_context g_sio;

u16 sio_read(u32 offset)
{
    u16 result = 0;
    switch (offset)
    {
    case 0x0:
        result = g_sio.rx_buffer;
        g_sio.stat.rx_fifo_not_empty = 0;
        //debug_log("JOY_RX_DATA read: %04x\n", result);
        break;
    case 0x4:
        //result = 0x3;
        result = g_sio.stat.value;
        //debug_log("JOY_STAT read: %04x\n", result);
        break;
    case 0x8:
        result = g_sio.mode;
        //debug_log("JOY_MODE read: %04x\n", result);
        break;
    case 0xa:
        result = g_sio.control;
        //debug_log("JOY_CTRL read: %04x\n", result);
        break;
    case 0xe:
        result = g_sio.baud_reload;
        //debug_log("JOY_BAUD read: %04x\n", result);
        break;
    INVALID_CASE;
    }
    return result;
}

#define JOYCTRL_WRITE_MASK 0x3f2f
#define JOYMODE_WRITE_MASK 0x13f
#define JOY_WRITE_DELAY 1088

static void pad_ack_callback(u32 param, s32 cycles_late)
{
    g_sio.stat.ack_is_low = param;
    // might not be accurate to check here
    /* DSR interrupt enable bit */
    if (g_sio.control & 0x1000)
    {
        g_cpu.i_stat |= INTERRUPT_CONTROLLER;
    }
}

static void sio_cmd(u8 cmd)
{
    u8 port = (g_sio.control >> 13) & 0x1;
    // TODO: check if controller is connected
    if (port) // TODO: temp, 2nd port is always disconnected
    {
        //SY_ASSERT(0);
        g_sio.rx_buffer = 0xff;
        g_sio.stat.rx_fifo_not_empty = 1;
        return;
    }

    switch (g_sio.state)
    {
    case SIO_STATE_NONE:
    {
        if (g_sio.control & 0x2) /* CS pulled low, choose device address */
        {
            if (cmd == 0x1)
            {
                /* controller */
                g_sio.rx_buffer = 0xff;
                g_sio.stat.rx_fifo_not_empty = 1;

                struct input_device_base *dev = g_sio.devices[port];
                if (!dev) {
                    return;
                }

                switch (dev->type)
                {
                case INPUT_DEVICE_DIGITAL_PAD:
                    g_sio.sequence_len = 4;
                    break;
                case INPUT_DEVICE_ANALOG_PAD:
                    g_sio.sequence_len = 8;
                    break;
                INVALID_CASE;
                }

                g_sio.stat.ack_is_low = 1;
                g_sio.rx_has_data = 1;
                g_sio.state = SIO_STATE_READ_CONTROLLER;
                g_sio.sequence_index = 0;
                schedule_event(pad_ack_callback, 0, JOY_WRITE_DELAY);
            }
            else
            {
                debug_log("Unknown joypad device address: %02xh\n", cmd);
                g_sio.rx_buffer = 0xff;
                g_sio.stat.rx_fifo_not_empty = 1;
            }
        }
    } break;
    case SIO_STATE_READ_CONTROLLER:
    {
        switch (g_sio.sequence_index)
        {
        case 0:
            if (cmd == 0x42) 
            {
                switch (g_sio.devices[port]->type) // TODO: make sure the port stays the same
                {
                case INPUT_DEVICE_DIGITAL_PAD:
                    g_sio.rx_buffer = 0x41;
                    break;
                case INPUT_DEVICE_ANALOG_PAD:
                    g_sio.rx_buffer = 0x79;
                    break;
                INVALID_CASE;
                }
            }
            else
            {
                g_sio.rx_buffer = 0xff;
                g_sio.stat.rx_fifo_not_empty = 1;
                g_sio.state = SIO_STATE_NONE;
            }
            break;
        
        case 1:
            // TODO: check TAP byte
            SY_ASSERT(cmd == 0x0);
            g_sio.rx_buffer = 0x5A;
            g_sio.stat.rx_fifo_not_empty = 1;
            break;
        case 2:
            g_sio.devices[port]->input_get_data(g_sio.devices[port]); // TODO: when is the controller data polled?

            g_sio.rx_buffer = g_sio.devices[port]->data[0];
            g_sio.stat.rx_fifo_not_empty = 1;
            break;
        case 3:
            g_sio.rx_buffer = g_sio.devices[port]->data[1];
            g_sio.stat.rx_fifo_not_empty = 1;
            break;
        case 4:
            g_sio.rx_buffer = g_sio.devices[port]->data[2];
            g_sio.stat.rx_fifo_not_empty = 1;
            break;
        case 5:
            g_sio.rx_buffer = g_sio.devices[port]->data[3];
            g_sio.stat.rx_fifo_not_empty = 1;
            break;
        case 6:
            g_sio.rx_buffer = g_sio.devices[port]->data[4];
            g_sio.stat.rx_fifo_not_empty = 1;
            break;
        case 7:
            g_sio.rx_buffer = g_sio.devices[port]->data[5];
            g_sio.stat.rx_fifo_not_empty = 1;
            break;
        }

        ++g_sio.sequence_index;

        if (g_sio.sequence_index == g_sio.sequence_len)
        {
            g_sio.state = SIO_STATE_NONE;
        }
        else
        {
            g_sio.stat.ack_is_low = 1;
            // /ACK signal width is about 2usec?
            schedule_event(pad_ack_callback, 0, JOY_WRITE_DELAY);
        }
    } break;
    INVALID_CASE;
    }

    g_sio.stat.rx_fifo_not_empty = 1;
}

void sio_store(u32 offset, u16 value)
{
    switch (offset)
    {
    case 0x0:
        //debug_log("JOY_TX_DATA store: %04x\n", value);
        if (!(g_sio.control & 0x1)) {
            break;
        }
        g_sio.tx_buffer = value;
#if 0
        if (g_pad.control & (1 << 13))
        {
            
            break;
        }
#endif
        sio_cmd((u8)value);
        //schedule_event(cpu, )
        // begins transfer?
        break;
    case 0x8:
        //debug_log("JOY_MODE store: %04x\n", value);
        g_sio.mode = value & JOYMODE_WRITE_MASK;
        break;
    case 0xa:
        //debug_log("JOY_CTRL store: %04x\n", value);
        if (value & 0x10)
        {
            g_sio.stat.irq = 0;
            g_sio.stat.rx_parity_error = 0;
        }
        if (value & 0x40)
        {
            // TODO: not sure exactly what gets reset here
            g_sio.mode = 0;
            g_sio.baud_reload = 0;
        }

        g_sio.control = value & JOYCTRL_WRITE_MASK;
        break;
    case 0xe:
        u8 factor;
        g_sio.baud_reload = value;
        switch (g_sio.mode & 0x3)
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
        g_sio.stat.timer = (value * factor) / 2;
        break;
    INVALID_CASE;
    }
}
