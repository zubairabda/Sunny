#include "sio.h"
#include "cpu.h"
#include "event.h"
#include "debug.h"
#include "psx.h"

struct sio_context g_sio;
static u32 pad_callback_id;
static u32 tx_callback_id;

#define JOYCTRL_WRITE_MASK 0x3f2f
#define JOYMODE_WRITE_MASK 0x13f

static void pad_ack_callback(u32 param, s32 ticks_late)
{
    g_sio.stat.ack_is_low = 0;
}

static void pad_tx_event(u32 param, s32 ticks_late)
{
    g_sio.stat.ack_is_low = 1;

    if (g_sio.control & 0x1000)
    {
        g_cpu.i_stat |= INTERRUPT_CONTROLLER;
        g_sio.stat.irq = 1;
    }

    // /ACK signal width is about 2us
    schedule_event(pad_callback_id, 0, 100);
}

static void sio_cmd(u8 cmd)
{
    u8 port = (g_sio.control >> 13) & 0x1;

    switch (g_sio.state)
    {
    case SIO_STATE_NONE:
    {
        // CS pulled low, choose device address
        if (g_sio.control & 0x2)
        {
            if (cmd == 0x1)
            {
                g_sio.rx_buffer = 0xff;
                //g_sio.stat.rx_fifo_not_empty = 1;

                struct input_device_base *dev = g_psx.controllers[port];
                if (!dev)
                    break;

                switch (dev->type)
                {
                case INPUT_DEVICE_DIGITAL_PAD:
                    g_sio.sequence_len = 4;
                    break;
                case INPUT_DEVICE_ANALOG_PAD:
                    g_sio.sequence_len = 8;
                    break;
                default:
                    debug_warn("[SIO] unknown device type: %d\n", dev->type);
                    g_sio.stat.rx_fifo_not_empty = 1;
                    return;
                }

                //g_sio.stat.ack_is_low = 1;
                g_sio.state = SIO_STATE_READ_CONTROLLER;
                g_sio.sequence_index = 0;
                schedule_event(tx_callback_id, 0, g_sio.baud_reload * 8);
            }
            else if (cmd == 0x81)
            {
                g_sio.rx_buffer = 0xff;
                if (!g_psx.memcard.is_valid || port != 0)
                    break;
                g_sio.state = SIO_STATE_READ_MEMCARD;
                g_sio.sequence_index = 0;
                schedule_event(tx_callback_id, 0, g_sio.baud_reload * 8);
            }
            else
            {
                debug_log("[SIO] Unknown device address: %02xh\n", cmd);
                g_sio.rx_buffer = 0xff;
            }
        }
        break;
    }
    case SIO_STATE_READ_CONTROLLER:
    {
        struct input_device_base *dev = g_psx.controllers[port];
        switch (g_sio.sequence_index)
        {
        case 0:
            if (cmd == 0x42)
            {
                switch (dev->type) // TODO: make sure the port stays the same
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
                g_sio.state = SIO_STATE_NONE;
            }
            break;
        
        case 1:
            // TODO: check TAP byte
            g_sio.rx_buffer = 0x5A;
            break;
        case 2:
            dev->input_get_data(dev); // TODO: when is the controller data polled? Also, this is not saved/serialized
            g_sio.rx_buffer = dev->data[0];
            break;
        case 3:
            g_sio.rx_buffer = dev->data[1];
            break;
        case 4:
            g_sio.rx_buffer = dev->data[2];
            break;
        case 5:
            g_sio.rx_buffer = dev->data[3];
            break;
        case 6:
            g_sio.rx_buffer = dev->data[4];
            break;
        case 7:
            g_sio.rx_buffer = dev->data[5];
            break;
        }

        ++g_sio.sequence_index;

        if (g_sio.sequence_index == g_sio.sequence_len)
        {
            g_sio.state = SIO_STATE_NONE;
        }
        else
        {
            //g_sio.stat.ack_is_low = 1;
            schedule_event(tx_callback_id, 0, g_sio.baud_reload * 8);
        }
        break;
    }
    case SIO_STATE_READ_MEMCARD:
    {
        switch (g_sio.sequence_index)
        {
        case 0:
            g_sio.rx_buffer = g_sio.flag;

            if (cmd == 'R')
            {
                g_sio.sequence_len = 139; // 11 + 128
                g_sio.command = MCD_COMMAND_READ;
            }
            else if (cmd == 'W')
            {
                g_sio.sequence_len = 137; // 9 + 128
                g_sio.command = MCD_COMMAND_WRITE;
            }
            else if (cmd == 'S')
            {
                g_sio.sequence_len = 9;
                g_sio.command = MCD_COMMAND_ID;
            }
            else
            {
                debug_warn("[SIO] Unhandled memcard command: %02x\n", cmd);
                g_sio.sequence_len = 1;
            }
            break;
        case 1:
            g_sio.rx_buffer = 0x5A;
            break;
        case 2:
            g_sio.rx_buffer = 0x5D;
            break;
        default:
            switch (g_sio.command)
            {
            case MCD_COMMAND_READ:
                switch (g_sio.sequence_index)
                {
                case 3:
                    g_sio.rx_buffer = 0x0;
                    g_sio.mcd_addr = (u32)cmd << 8;
                    g_sio.mcd_checksum = cmd;
                    break;
                case 4:
                    g_sio.rx_buffer = g_sio.prev;
                    g_sio.mcd_addr |= cmd;
                    g_sio.mcd_checksum ^= cmd;
                    //debug_log("[MEMCARD] Read block: %u, sector: %u\n", (g_sio.mcd_addr >> 6) & 0xf, g_sio.mcd_addr & 0x3f);
                    platform_read_file(&g_psx.memcard, g_sio.mcd_addr * 128, g_sio.mcd_buffer, 128);
                    break;
                case 5:
                    g_sio.rx_buffer = 0x5C;
                    break;
                case 6:
                    g_sio.rx_buffer = 0x5D;
                    break;
                case 7:
                    g_sio.rx_buffer = (u8)(g_sio.mcd_addr >> 8);
                    break;
                case 8:
                    g_sio.rx_buffer = (u8)g_sio.mcd_addr;
                    break;
                default:
                    if (g_sio.sequence_index < 137)
                    {
                        // read memcard data
                        u32 index = g_sio.sequence_index - 9;
                        u8 data = g_sio.mcd_buffer[index];
                        g_sio.rx_buffer = data;
                        g_sio.mcd_checksum ^= data;
                    }
                    else if (g_sio.sequence_index == 137)
                    {
                        g_sio.rx_buffer = g_sio.mcd_checksum;
                    }
                    else
                    {
                        g_sio.rx_buffer = 0x47;
                    }
                    break;
                }
                break;
            case MCD_COMMAND_WRITE:
                switch (g_sio.sequence_index)
                {
                case 3:
                    g_sio.rx_buffer = 0x0;
                    g_sio.mcd_addr = (u32)cmd << 8;
                    g_sio.mcd_checksum = cmd;
                    break;
                case 4:
                    g_sio.rx_buffer = g_sio.prev;
                    g_sio.mcd_addr |= cmd;
                    g_sio.mcd_checksum ^= cmd;
                    //debug_log("[MEMCARD] Write block: %u, sector: %u\n", (g_sio.mcd_addr >> 6) & 0xf, g_sio.mcd_addr & 0x3f);
                    break;
                default:
                    if (g_sio.sequence_index < 133)
                    {
                        u32 index = g_sio.sequence_index - 5;
                        g_sio.mcd_buffer[index] = cmd;
                        g_sio.mcd_checksum ^= cmd;
                        g_sio.rx_buffer = g_sio.prev;
                    }
                    else
                    {
                        switch (g_sio.sequence_index)
                        {
                        case 133:
                            if (g_sio.mcd_checksum != cmd)
                                SY_ASSERT(0);//g_sio.error = 0x4e;
                            else
                                platform_write_file(&g_psx.memcard, g_sio.mcd_addr * 128, g_sio.mcd_buffer, 128);
                            g_sio.rx_buffer = g_sio.prev;
                            break;
                        case 134:
                            g_sio.rx_buffer = 0x5C;
                            break;
                        case 135:
                            g_sio.rx_buffer = 0x5D;
                            break;
                        case 136:
                            g_sio.rx_buffer = 0x47; // TODO: use error
                            g_sio.flag &= ~0x8;
                            break;
                        }
                    }
                    break;
                }
                break;
            case MCD_COMMAND_ID:
                switch (g_sio.sequence_index)
                {
                case 3:
                    g_sio.rx_buffer = 0x5C;
                    break;
                case 4:
                    g_sio.rx_buffer = 0x5D;
                    break;
                case 5:
                    g_sio.rx_buffer = 0x4;
                    break;
                case 6:
                    g_sio.rx_buffer = 0x0;
                    break;
                case 7:
                    g_sio.rx_buffer = 0x0;
                    break;
                case 8:
                    g_sio.rx_buffer = 0x80;
                    break;
                }
                break;
            }
            break;
        }

        //g_sio.stat.rx_fifo_not_empty = 1;

        ++g_sio.sequence_index;

        if (g_sio.sequence_index == g_sio.sequence_len)
        {
            g_sio.state = SIO_STATE_NONE;
        }
        else
        {
            schedule_event(tx_callback_id, 0, g_sio.baud_reload * 8);
        }

        break;
    }
    INVALID_CASE;
    }

    g_sio.prev = cmd;
    g_sio.stat.rx_fifo_not_empty = 1;
}

void sio_reset(void)
{
    memset(&g_sio, 0, sizeof(struct sio_context));
    g_sio.stat.tx_fifo_not_full = 1;
    g_sio.stat.tx_finished = 1;
    g_sio.flag = 0x8;
    pad_callback_id = register_callback(pad_ack_callback);
    tx_callback_id = register_callback(pad_tx_event);
}

u16 sio_read(u32 offset)
{
    u16 result = 0;
    switch (offset)
    {
    case 0x0:
        result = g_sio.rx_buffer;
        g_sio.rx_buffer = 0xff;
        g_sio.stat.rx_fifo_not_empty = 0;
        break;
    case 0x4:
        result = g_sio.stat.value;
        break;
    case 0x8:
        result = g_sio.mode;
        break;
    case 0xa:
        result = g_sio.control;
        break;
    case 0xe:
        result = g_sio.baud_reload;
        break;
    INVALID_CASE;
    }
    return result;
}

void sio_write(u32 offset, u16 value)
{
    switch (offset)
    {
    case 0x0:
        if (!(g_sio.control & 0x1))
            break;
        //debug_log("[SIO] send command byte <- %02x\n", (u8)value);
        sio_cmd((u8)value);
        break;
    case 0x8:
        g_sio.mode = value & JOYMODE_WRITE_MASK;
        break;
    case 0xa:
        if (!(value & 0x2))
        {
           g_sio.state = SIO_STATE_NONE;
        }
        if (value & 0x10)
        {
            g_sio.stat.irq = 0;
        }
        if (value & 0x40)
        {
            // TODO: not sure exactly what gets reset here
            g_sio.state = SIO_STATE_NONE;
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
