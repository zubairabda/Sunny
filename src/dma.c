#include "dma.h"
#include "cpu.h"
#include "spu.h"
#include "gpu.h"
#include "cdrom.h"
#include "mdec.h"
#include "debug.h"
#include "memory.h"

struct dma_state g_dma;

static void process_dma(u32 channel)
{
    struct dma_port *port = &g_dma.ports[channel];
    port->control &= ~(0x1000000);

    s32 step = (channel == CH_OTC || port->control & 0x2) ? -4 : 4;
    u32 syncmode = channel == CH_OTC ? 0 : ((port->control >> 9) & 0x3);

    switch (syncmode)
    {
    case 0: // manual mode
    {
        SY_ASSERT(channel == CH_CDROM || channel == CH_OTC);
        SY_ASSERT(!(port->control & 0x1)); // we only expect transfers from the device to RAM in this mode
#if 1
        if (port->control & 0x10000000)
        {
            port->control &= ~(0x10000000);
        }
        else
        {
            port->control |= 0x1000000;
            return;
        }
#endif
        u32 addr = port->madr & 0xffffff;
        SY_ASSERT((addr & 0x3) == 0);
        
        u32 size = port->b1 ? port->b1 : 0x10000;

        if (channel == CH_OTC)
        {
            u32 word;
            while (size--)
            {
                word = addr;
                addr += step;
                store32(word, addr);
            }
            // store OTC end marker
            store32(word, 0xffffff);
        }
        else
        {
            SY_ASSERT(g_cdrom.data_fifo_end);
            
            u32 fifo_len = g_cdrom.data_fifo_end - g_cdrom.data_fifo_index;
            u16 rem = fifo_len & 0x3;
#if 1
            u32 sz_bytes = size << 2;

            SY_ASSERT(sz_bytes <= fifo_len);

            if (sz_bytes == fifo_len)
                g_cdrom.status.DRQSTS = false;
            
            struct cdrom_sector *sector = &g_cdrom.buffered_sector;
            memcpy((g_ram + addr), &sector->data[g_cdrom.data_fifo_index], sz_bytes);
            g_cdrom.data_fifo_index += sz_bytes;
            
#else
            if (fifo_len <= (size << 2))
            {
                memcpy(g_ram + addr, &g_cdrom.sector[g_cdrom.data_fifo_index], fifo_len);
                g_cdrom.status &= ~(CDR_STATUS_DATA_FIFO_NOT_EMPTY);
            }
            else
            {
                SY_ASSERT(0);
            }
#endif           
        }

        break;
    }
    case 1: // sync mode
    {
        // TODO: can maybe be optimized by using the length of the copy and just memcpying
        // would have to check for wrapping, and if the specific port doesnt need delayed transfers
        u32 addr = port->madr & 0xfffffc;
        u32 word;
        u32 size = port->b1 * port->b2;
        u32 dir = port->control & 0x1;
        while (size--)
        {
            if (dir == 0) // Device to RAM
            {
                switch (channel)
                {
                case CH_GPU:
                    U32FromPtr(g_ram + addr) = gpuread();
                    break;
                default:
                    debug_log("Unhandled DMA device transfer to RAM\n");
                    SY_ASSERT(0);
                    break;
                }
            }
            else // RAM to Device
            {
                word = U32FromPtr(g_ram + addr);
                switch (channel)
                {
                case CH_MDECIN:
                    mdec_command(word); // TODO: can set interrupt again?
                    break;
                case CH_GPU:
                    SY_ASSERT(port->b1 <= 0x10);
                    execute_gp0_command(word);
                    break;
                case CH_SPU:
                    SY_ASSERT(port->b1 <= 0x10);
                    // NOTE: this is probably super broken but wtv
                    U32FromPtr(g_spu.dram + g_spu.current_transfer_addr) = word;
                    g_spu.current_transfer_addr += 4;
                    break;
                default:
                    debug_log("Unhandled DMA RAM transfer to device\n");
                    SY_ASSERT(0);
                    break;
                }
            }
            addr += step;
        }
        port->madr = addr; // update address in syncmode=1
        break;
    }
    case 2: // linked-list
    {
        SY_ASSERT(channel == CH_GPU); // we don't expect another channel in this transfer mode
        // TODO: wrong mask?
        u32 addr = port->madr & 0x1ffffc;
        u32 entry;
        u32 num_words;
        //u32 consumed = 1;
        u32 cmd;
        do
        {
            entry = U32FromPtr(g_ram + addr);
            num_words = entry >> 24;
            while (num_words--)
            {
                addr = (addr + 4) & 0x1ffffc;
                cmd = U32FromPtr(g_ram + addr);
                execute_gp0_command(cmd);
            }
            addr = entry & 0xffffff;
        } while (!(addr & 0x800000));
        break;
    }
    }

    dma_set_interrupt(channel);
}

void dma_set_interrupt(u32 channel)
{
    if (g_dma.interrupt & ((1 << channel) << 16))
    {
        g_dma.interrupt |= ((1 << channel) << 24);
        if (g_dma.interrupt & (1 << 23))
        {
            g_dma.interrupt |= (1 << 31);
            g_cpu.i_stat |= INTERRUPT_DMA;
        }
    }
}

static inline void dma_check_port(u32 channel)
{
    b8 channel_enabled = (g_dma.control & (0x8 << (4 * channel))) != 0;
    if (channel_enabled && (g_dma.ports[channel].control & 0x1000000)) // for now, we do instant DMA transfers
    {
        process_dma(channel);
    }
}

void dma_init(void)
{
    g_dma.control = 0x07654321; // inital value of control register
}
#include "event.h"
void mdec_event_handler(u32 param, s32 cycles_late)
{
    mdec_on_dma();
}

void dma_write(u32 offset, u32 value)
{
    u32 channel = offset >> 4;
    switch (channel)
    {
    case 0x7: // DICR/DPCR
        if (offset & 0x4)
        {
            // not entirely sure on the behavior when bit 15 is set
            SY_ASSERT(!(value & (1 << 15)));
            g_dma.interrupt = value & 0x7fff803f;
            for (int i = 0; i < 7; ++i)
            {
                // writing 1 to irq flags acks them
                u32 pos = 0x1000000 << i;
                if (g_dma.interrupt & pos)
                {
                    g_dma.interrupt &= ~pos;
                }
            }
        }
        else
        {
            g_dma.control = value;
#if 0
            for (int i = 0; i < 7; ++i)
            {
                dma_check_port(i);
            }
#endif
        }
        break;
    default: // channels 0-6
        SY_ASSERT(channel != 5);
        switch (offset & 0xf)
        {
        case 0x0:
            // NOTE: Apparently, transfers dont happen to RAM when bit 23 is set, which makes sense, so we can
            // I guess do the transfer, then just skip words when the address has that bit set
            g_dma.ports[channel].madr = value & 0xffffff;
            //debug_log("[DMA] set MADR for ch %d to %08x\n", channel, g_dma.ports[channel].madr);
            break;
        case 0x4:
            *(((u32 *)(g_dma.ports + channel)) + 1) = value;
            break;
        case 0x8:
            if (channel == CH_OTC)
            {
                g_dma.ports[channel].control = (value | 0x2) & 0x51000002;
            }
            else
            {
                g_dma.ports[channel].control = value & 0x71770703; // write mask
            }

            if (value & 0x1000000 && (g_dma.control & (0x8 << (4 * channel))))
            {
                if (channel == CH_MDECOUT)
                {
                    //g_dma.ports[channel].control &= ~(0x1000000);
                    mdec_on_dma();
                }
                else
                {
                    process_dma(channel);
                }
            }
            //dma_check_port(channel);
            break;
        INVALID_CASE;
        }
        break;
    }
}

u32 dma_read(u32 offset)
{
    u32 result = 0;
    u32 channel = offset >> 4;
    switch (channel)
    {
    case 0x7:
        if (offset & 0x4)
        {
            result = g_dma.interrupt;
        }
        else
        {
            result = g_dma.control;
        }     
        break;
    default:
        switch (offset & 0xf)
        {
        case 0x0:
            result = g_dma.ports[channel].madr;
            break;
        case 0x4:
            result = *(((u32*)(g_dma.ports + channel)) + 1);
            break;
        case 0x8:
            result = g_dma.ports[channel].control;
            break;
        INVALID_CASE;
        }
        break;
    }
    return result;
}
