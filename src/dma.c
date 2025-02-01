#include "dma.h"
#include "cpu.h"
#include "spu.h"
#include "gpu.h"
#include "cdrom.h"
#include "debug.h"
#include "memory.h"

typedef union
{
    struct
    {
        u32 transfer_from_ram : 1;
        u32 step_backward : 1;
        u32 : 6;
        u32 chopping_enable : 1;
        u32 sync_mode : 2;
        u32 : 5;
        u32 chopping_dma_window : 3;
        u32 : 1;
        u32 chopping_cpu_window : 3;
        u32 : 1;
        u32 start_busy : 1;
        u32 : 3;
        u32 start_trigger : 1;
        u32 : 3;
    };
    u32 value;
} DHCR;

struct dma_channel
{
    u32 madr;
    u16 b1;
    u16 b2;
    u32 control;
    u32 pad;
};

struct dma_state
{
    struct dma_channel channels[7];
    u32 control;
    u32 interrupt;
};

static struct dma_state dma;

static void process_dma(u32 port)
{
    struct dma_channel *channel = &dma.channels[port];
    channel->control &= ~(0x1000000);
    s32 step = (port == 6 || channel->control & 0x2) ? -4 : 4;
    u32 syncmode = port == 6 ? 0 : ((channel->control >> 9) & 0x3);

    switch (syncmode)
    {
    case 0: // manual mode
    {
        SY_ASSERT(port == 3 || port == 6);
        SY_ASSERT(!(channel->control & 0x1)); // we only expect transfers from the device to RAM in this mode
#if 1
        if (channel->control & 0x10000000)
        {
            channel->control &= ~(0x10000000);
        }
        else
        {
            channel->control |= 0x1000000;
            return;
        }
#endif
        u32 addr = channel->madr & 0xffffff;
        SY_ASSERT((addr & 0x3) == 0);
        
        u32 size = channel->b1 ? channel->b1 : 0x10000;

        if (port == 6)
        {
            u32 word;
            while (size--)
            {
                word = addr;
                addr += step;
                store32(word, addr);
            }
            /* store OTC end marker */
            store32(word, 0xffffff);
        }
        else
        {
            if (!g_cdrom.data_fifo_end)
            {
                SY_ASSERT(0);
            }
            u32 fifo_len = g_cdrom.data_fifo_end - g_cdrom.data_fifo_index;
            u16 rem = fifo_len & 0x3;
            if (fifo_len <= (size << 2))
            {
                memcpy(g_ram + addr, &g_cdrom.sector[g_cdrom.data_fifo_index], fifo_len);
                g_cdrom.status &= ~(CDR_STATUS_DATA_FIFO_NOT_EMPTY);
            }
            else
            {
                SY_ASSERT(0);
            }
                
        }

        break;
    }
    case 1: // sync mode
    {
        // TODO: can maybe be optimized by using the length of the copy and just memcpying
        // would have to check for wrapping, and if the specific port doesnt need delayed transfers
        u32 addr = channel->madr & 0xfffffc;
        u32 word;
        u32 size = channel->b1 * channel->b2;
        u32 dir = channel->control & 0x1;
        while (size--)
        {
            if (dir == 0) // to RAM
            {
                switch (port)
                {
                case 2:
                    U32FromPtr(g_ram + addr) = gpuread();
                    break;
                default:
                    debug_log("Unhandled DMA direction\n");
                    SY_ASSERT(0); // TODO: handle this
                    break;
                }
            }
            else // from RAM
            {
                word = U32FromPtr(g_ram + addr);
                switch (port)
                {
                case 2: // GPU
                    SY_ASSERT(channel->b1 <= 0x10);
                    execute_gp0_command(word);
                    break;
                case 4: // SPU
                    SY_ASSERT(channel->b1 <= 0x10);
                    // NOTE: this is probably super broken but wtv
                    U32FromPtr(g_spu.dram + g_spu.current_transfer_addr) = word;
                    g_spu.current_transfer_addr += 4;
                    break;
                default:
                    debug_log("Unhandled DMA destination\n");
                    SY_ASSERT(0);
                    break;
                }
            }
            addr += step;
            //printf("sync data: %08x\n", word);
        }

        break;
    }
    case 2: // linked-list
    {
        SY_ASSERT(port == 2); // we don't expect another port on this transfer mode
        // TODO: wrong mask?
        u32 addr = channel->madr & 0x1ffffc;
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

    if (dma.interrupt & ((1 << port) << 16)) {
        dma.interrupt |= ((1 << port) << 24);
        if (dma.interrupt & (1 << 23)) {
            dma.interrupt |= (1 << 31);
            g_cpu.i_stat |= INTERRUPT_DMA;
        }
    }
}

static inline void dma_check_port(u32 port)
{
    if ((dma.control & (0x8 << (4 * port))) && (dma.channels[port].control & 0x1000000)) // for now, we do instant DMA transfers
    {
        process_dma(port);
    }
}

void dma_init(void)
{
    dma.control = 0x07654321; // inital value of control register
}

void dma_write(u32 offset, u32 value)
{
    // handle all DMA transfers
    u32 port = offset >> 4;
    switch (port)
    {
    case 0x7: // DICR/DPCR
        if (offset & 0x4)
        {
            // not entirely sure on the behavior when bit 15 is set
            SY_ASSERT(!(value & (1 << 15)));
            dma.interrupt = value & 0x7fff803f;
            for (int i = 0; i < 7; ++i)
            {
                // writing 1 to irq flags acks them
                u32 pos = 0x1000000 << i;
                if (dma.interrupt & pos) {
                    dma.interrupt &= ~pos;
                }
            }
        }
        else
        {
            dma.control = value;
            for (int i = 0; i < 7; ++i) {
                dma_check_port(i);
            }
        }
        break;
    default: // channels 0-6
        SY_ASSERT(port == 2 || port == 3 || port == 4 || port == 6);
        switch (offset & 0xf)
        {
        case 0x0:
            // NOTE: Apparently, transfers dont happen to RAM when bit 23 is set, which makes sense, so we can
            // I guess do the transfer, then just skip words when the address has that bit set
            dma.channels[port].madr = value & 0xffffff;
            break;
        case 0x4:
            *(((u32 *)(dma.channels + port)) + 1) = value;
            break;
        case 0x8:
            if (port == 6)
            {
                dma.channels[port].control = (value | 0x2) & 0x51000002;
            }
            else
            {
                dma.channels[port].control = value & 0x71770703; // write mask
            }
            dma_check_port(port);
            break;
        INVALID_CASE;
        }
        break;
    }
}

u32 dma_read(u32 offset)
{
    u32 result = 0;
    u32 port = offset >> 4;
    switch (port)
    {
    case 0x7:
        if (offset & 0x4)
        {
            result = dma.interrupt;
        }
        else
        {
            result = dma.control;
        }     
        break;
    default:
        switch (offset & 0xf)
        {
        case 0x0:
            result = dma.channels[port].madr;
            break;
        case 0x4:
            result = *(((u32*)(dma.channels + port)) + 1);
            break;
        case 0x8:
            result = dma.channels[port].control;
            break;
        INVALID_CASE;
        }
        break;
    }
    return result;
}
