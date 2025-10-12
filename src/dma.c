#include "dma.h"
#include "cpu.h"
#include "spu.h"
#include "gpu.h"
#include "cdrom.h"
#include "mdec.h"
#include "debug.h"
#include "event.h"
#include "memory.h"

struct dma_state g_dma;
static u32 dma_callback_id;

#define CHCR_CHOPPING_ENABLE 0x100

#define RAM_SIZE_MASK 0x1ffffc

b8 mdecin_on_dma(b8 from_ram, s8 step, u32 size, u32 *paddr)
{
    u32 addr = *paddr;
    u32 word;
    while (size--)
    {
        word = U32FromPtr(g_ram + addr);
        mdec_command(word);
        addr += step;
    }
    *paddr = addr;

    return true;
}

b8 gpu_on_dma(b8 from_ram, s8 step, u32 size, u32 *paddr)
{
    u32 addr = *paddr;
    u32 word;

    while (size--)
    {
        if (from_ram)
        {
            word = U32FromPtr(g_ram + addr);   
            execute_gp0_command(word);
        }
        else
        {
            U32FromPtr(g_ram + addr) = gpuread();
        }
        addr += step;
    }
    *paddr = addr;

    return true;
}

b8 cdrom_on_dma(b8 from_ram, s8 step, u32 size, u32 *paddr)
{   
    if (g_cdrom.status.DRQSTS)
    {
        u32 addr = *paddr;
        u32 fifo_len = g_cdrom.data_fifo_end - g_cdrom.data_fifo_index;
        u32 rem = fifo_len & 0x3;
        SY_ASSERT(!rem);

        u32 sz_bytes = size * 4;

        SY_ASSERT(sz_bytes <= fifo_len);
    
        u8 *sector = g_cdrom.sector_buffer[!g_cdrom.sector_index];
        //u32 left = size;
        while (size--)
        {
            u32 *dst = (u32 *)(g_ram + addr);
            u32 *src = (u32 *)&sector[g_cdrom.data_fifo_index];
            *dst = *src;
            g_cdrom.data_fifo_index += 4;
            addr += step;
        }
        *paddr = addr;

        if (g_cdrom.data_fifo_index == g_cdrom.data_fifo_end)
            g_cdrom.status.DRQSTS = false;

        return true;
    }
    return false;
}

b8 spu_on_dma(b8 from_ram, s8 step, u32 size, u32 *paddr)
{
    u32 addr = *paddr;
    u32 word;
    while (size--)
    {
        if (from_ram)
        {
            word = U32FromPtr(g_ram + addr);
            U32FromPtr(g_dram + g_spu.current_transfer_addr) = word;
        }
        else
        {
            word = U32FromPtr(g_dram + g_spu.current_transfer_addr);
            U32FromPtr(g_ram + addr) = word;
        }
        g_spu.current_transfer_addr += 4;
        addr += step;
    }
    *paddr = addr;
    return true;
}

b8 otc_on_dma(b8 from_ram, s8 step, u32 size, u32 *paddr)
{
    u32 addr = *paddr;
    u32 word;
    while (size--)
    {
        word = addr;
        addr = (addr - 4) & 0xfffffc;
        store32(word, addr);
    }
    store32(word, 0xffffff);
    *paddr = addr;

    return true;
}

typedef b8 (*dma_transfer_func)(b8 from_ram, s8 step, u32 size, u32 *paddr);

dma_transfer_func dma_on_transfer[CH_COUNT] = 
{
    mdecin_on_dma,
    mdecout_on_dma,
    gpu_on_dma,
    cdrom_on_dma,
    spu_on_dma,
    NULL,
    otc_on_dma,
};

static void dma_set_interrupt(u32 channel)
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

static void dma_expand_priorities(void)
{
    // to avoid needing to compare channel numbers when selecting a channel, we will expand
    // the priority values to take into account channel numbers
    // note that we currently ignore 'CPU memory access priority'
    for (s32 i = 0; i < CH_COUNT; ++i)
    {
        struct dma_transfer *transfer = &g_dma.transfers[i];
        u32 dma_priority = (g_dma.control >> (4 * i)) & 0x7;
        transfer->priority = 8 + (dma_priority * 8) - i;
    }
}

static s32 dma_select_channel(void)
{
    s32 selected = -1;
    u32 lowest = 0xffffffff;
    for (s32 i = 0; i < CH_COUNT; ++i)
    {
        struct dma_transfer *transfer = &g_dma.transfers[i];
        if (transfer->in_progress && !transfer->pending)
        {
            if (transfer->priority < lowest)
            {
                lowest = transfer->priority;
                selected = i;
            }
        }
    }
    return selected;
}

static void dma_handle_next_transfer(u32 channel, s32 ticks_late)
{
    struct dma_port *port = &g_dma.ports[channel];
    struct dma_transfer *transfer = &g_dma.transfers[channel];

    g_dma.active_channel = channel;
    
    u32 transfer_size = 0;
    u32 entry;

    u32 delay = 0;
    u32 next_run = 0;
    u32 mode = transfer->transfer_mode;
    switch (mode)
    {
    case DMA_MODE_BURST:
    {
        if (port->control & CHCR_CHOPPING_ENABLE)
        {
            u32 dma_window = 1 << ((port->control >> 16) & 0x7);
            u32 cpu_window = 1 << ((port->control >> 20) & 0x7);

            transfer_size = dma_window;
            next_run = cpu_window;
        }
        else
        {
            transfer_size = transfer->words_left;
            delay = channel == CH_OTC ? transfer_size : 24 * transfer_size;
        }

        break;
    }
    case DMA_MODE_SLICE:
    {
        transfer_size = port->b1;
        next_run = 20; // TODO: timings for each device
        break;
    }
    case DMA_MODE_LINKEDLIST:
    {
        SY_ASSERT(channel == CH_GPU); // we don't expect another channel in this transfer mode for now
        SY_ASSERT(transfer->step == 4);
#if 0
        entry = U32FromPtr(g_ram + transfer->current_addr);
        transfer_size = entry >> 24;
        transfer->current_addr += 4;
#endif
        // NOTE: This is a workaround to implement the DMAC halting between linked-list nodes. It seems breaking after each node
        // as suggested by the docs results in a timing issue in the shell where it overwrites polygon entries in the OT
        // being drawn by the GPU, expecting the transfer to be done at that point?
        // So instead, we will break linked-list transfers only when a certain number of words have been transferred, and skip over
        // empty entries. This can obviously result in hanging on empty infinite lists, but we won't worry about that for now :)
        u32 total_words = 0;
        for (;;)
        {
            entry = U32FromPtr(g_ram + transfer->current_addr);
            transfer_size = entry >> 24;
            transfer->current_addr += 4; // skip node header
            if (transfer_size)
            {
                // TODO: check for incomplete transfers
                dma_on_transfer[channel](true, transfer->step, transfer_size, &transfer->current_addr);
                total_words += transfer_size;
            }

            g_cycles_elapsed += transfer_size + 4;

            transfer->current_addr = entry & 0xffffff;
            if (transfer->current_addr & 0x800000)
            {
                port->madr = transfer->current_addr;

                //debug_log("[DMA] linked-list transfer complete for channel %u\n", channel);

                g_dma.ports[channel].control &= ~(0x1000000);
                dma_set_interrupt(channel);

                g_dma.active_channel = -1;
                g_dma.event = 0;
                transfer->in_progress = false;

                s32 next_channel = dma_select_channel();
                if (next_channel >= 0)
                {
                    g_dma.event = schedule_event(dma_callback_id, next_channel, 0);
                }
                return;
            }
            
            if (total_words > 32)
                break;
        }

        g_dma.event = schedule_event(dma_callback_id, channel, 20);
        return;
    }
    }

    if (dma_on_transfer[channel](transfer->is_from_ram, transfer->step, transfer_size, &transfer->current_addr))
    {
        g_cycles_elapsed += delay ? delay : transfer_size;
        transfer->words_left -= transfer_size;
        if (transfer->words_left)
        {
            //debug_log("[DMA] scheduling next block in %u ticks\n", next_run);
            port->madr = transfer->current_addr; // update MADR every slice, also probably needed when interrupted by a higher priority channel
            g_dma.event = schedule_event(dma_callback_id, channel, next_run);
        }
        else
        {
            //debug_log("[DMA] finished transfer for channel %u\n", channel);
            port->control &= ~(0x1000000);
            dma_set_interrupt(channel);

            g_dma.active_channel = -1;
            g_dma.event = 0;
            transfer->in_progress = false;

            s32 next_channel = dma_select_channel();
            if (next_channel >= 0)
            {
                g_dma.event = schedule_event(dma_callback_id, next_channel, 0);
            }
        }
    }
    else
    {
        SY_ASSERT(channel == CH_MDECOUT); // we haven't experienced this with any other channel
        transfer->pending = true;
        transfer->pending_words = transfer_size;

        // NOTE: since some games interrupt an MDECin transfer with an MDECout one, we need to also handle channel switching here
        g_dma.active_channel = -1;
        g_dma.event = 0;
        s32 next_channel = dma_select_channel();
        if (next_channel >= 0)
        {
            g_dma.event = schedule_event(dma_callback_id, next_channel, 0);
        }
    }

    // NOTE: if priorities are changed during a transfer, it should be checked here
}

static void dma_start_transfer(u32 channel)
{
    struct dma_port *port = &g_dma.ports[channel];
    s32 step = (channel == CH_OTC || port->control & 0x2) ? -4 : 4;
    u32 mode = channel == CH_OTC ? 0 : ((port->control >> 9) & 0x3);
    b8 dir_from_ram = port->control & 0x1;
    u32 addr = port->madr & RAM_SIZE_MASK;

    struct dma_transfer *transfer = &g_dma.transfers[channel];
    transfer->step = step;
    transfer->transfer_mode = mode;
    transfer->current_addr = addr;
    transfer->is_from_ram = dir_from_ram;

    // TODO: fix
    if (mode == DMA_MODE_BURST)
    {
        if (port->control & 0x10000000)
            port->control &= ~(0x10000000);
        else
            SY_ASSERT(0);
    }

    if (mode == DMA_MODE_BURST)
        transfer->words_left = port->b1 ? port->b1 : 0x10000;
    else if (mode == DMA_MODE_SLICE)
        transfer->words_left = port->b1 * port->b2;

    debug_log("[DMA] Channel %u requesting transfer of %u words %s %08X.\n", channel, transfer->words_left, transfer->is_from_ram ? "from" : "to", addr);

    transfer->in_progress = true;
    s32 selected_channel = dma_select_channel();
    SY_ASSERT(selected_channel >= 0);
    if (selected_channel == g_dma.active_channel)
    {
        SY_ASSERT(g_dma.event);
        return;
    }
    else if (g_dma.event)
    {
        remove_event(g_dma.event);
        g_dma.event = 0;
    }
    dma_handle_next_transfer(selected_channel, 0);
}

void dma_reset(void)
{
    memset(&g_dma, 0, sizeof(struct dma_state));
    g_dma.control = 0x07654321; // inital value of control register
    g_dma.active_channel = -1;
    dma_expand_priorities();
    dma_callback_id = register_callback(dma_handle_next_transfer);
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
            g_dma.interrupt &= 0x7f000000;
            g_dma.interrupt ^= (value & g_dma.interrupt);
            g_dma.interrupt |= value & 0xff803f; // DICR write mask
        }
        else
        {
            u32 old = g_dma.control;
            g_dma.control = value;
            if (old ^ value)
            {
                SY_ASSERT(g_dma.active_channel == -1);
                dma_expand_priorities();
            }
        }
        break;
    default: // channels 0-6
        SY_ASSERT(channel != CH_PIO);
        switch (offset & 0xf)
        {
        case 0x0:
            g_dma.ports[channel].madr = value & 0xffffff;
            break;
        case 0x4:
            *((u32 *)&g_dma.ports[channel].b1) = value;
            break;
        case 0x8:
            // TODO: remove
            if (value & 0x1000000)
                SY_ASSERT(!(g_dma.ports[channel].control & 0x1000000));

            // TODO: remove
            if (channel == CH_OTC)
            {
                g_dma.ports[channel].control = (value | 0x2) & 0x51000002;
            }
            else
            {
                g_dma.ports[channel].control = value & 0x71770703; // write mask
            }
            
            if (g_dma.interrupt & 0x7f) // interrupt occurs after each slice/block
                SY_ASSERT(0);

            if (value & 0x1000000)
            {
                if ((g_dma.control & (0x8 << (4 * channel))))
                {
                    debug_log("[DMA] start transfer for channel %u\n", channel);
                    dma_start_transfer(channel);
                }
                else
                {
                    debug_warn("[DMA] start transfer bit set for channel %u, but channel is disabled\n");
                }
            }
            else
            {
                debug_warn("[DMA] stop transfer for channel %u\n", channel);
                if (g_dma.transfers[channel].in_progress)
                {
                    g_dma.transfers[channel].in_progress = false;
                    if ((s32)channel == g_dma.active_channel)
                    {
                        SY_ASSERT(g_dma.event);
                        remove_event(g_dma.event);
                        g_dma.event = 0;
                        // since the next channel to transfer relies on the current channel to re-select a channel,
                        // we need to do it here
                        g_dma.active_channel = -1;
                        s32 next_channel = dma_select_channel();
                        if (next_channel >= 0)
                        {
                            g_dma.event = schedule_event(dma_callback_id, next_channel, 0);
                        }
                    }
                }
            }
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
            result = g_dma.interrupt;
        else
            result = g_dma.control;  
        break;
    default:
        switch (offset & 0xf)
        {
        case 0x0:
            result = g_dma.ports[channel].madr;
            break;
        case 0x4:
            result = *((u32 *)&g_dma.ports[channel].b1);
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
