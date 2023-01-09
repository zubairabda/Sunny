static inline void process_dma(struct cpu_state* cpu, struct dma_channel* channel, u32 port)
{
    channel->control &= ~(0x11000000); // bit 28 clears on begin, but transfers are instant for now
    s32 step = (channel->control & 0x2) ? -4 : 4;
    u32 dir = channel->control & 0x1;

    switch ((channel->control >> 9) & 0x3)
    {
    case 0: // manual mode
    {
        u32 addr = channel->madr & 0xffffff;
        SY_ASSERT((addr & 0x3) == 0);
        
        u32 word;
        u32 size = channel->b1;
        while (size--)
        {
            if (dir == 0)
            {
                word = addr;
                addr += step;
                store32(cpu, word, addr);
            }
            else
            {
                SY_ASSERT(0);
            }
        }
        if (port == 6)
        {
            store32(cpu, word, 0xffffff);
        }
    }   break;
    case 1: // sync mode
    {
        u32 addr = channel->madr & 0xffffff;
        SY_ASSERT((addr & 0x3) == 0);
        u32 word;
        u32 size = channel->b1 * channel->b2;
        while (size--)
        {
            if (dir == 0) // to RAM
            {
                switch (port)
                {
                case 2:
                    *(u32*)(cpu->ram + addr) = gpuread(&cpu->gpu);
                    break;
                default:
                    printf("Unhandled DMA direction\n");
                    SY_ASSERT(0); // TODO: handle this
                    break;
                }
            }
            else // from RAM
            {
                word = *(u32*)(cpu->ram + addr);
                switch (port)
                {
                case 2: // GPU
                    execute_gp0_command(&cpu->gpu, word);
                    break;
                default:
                    printf("Unhandled DMA destination\n");
                    SY_ASSERT(0);
                    break;
                }
            }
            addr += step;
            //printf("sync data: %08x\n", word);
        }
    }   break;
    case 2: // linked-list
    {
        SY_ASSERT(port == 2); // we don't expect another port on this transfer mode
        u32 addr = channel->madr & 0x1ffffc;
        u32 entry;
        u32 num_words;
        u32 consumed = 1;
        u32 cmd;
        do
        {
            entry = *(u32*)(cpu->ram + addr);
            num_words = entry >> 24;
            while (num_words--)
            {
                // TODO: make sure in this mode it is only ever incremented
                addr = (addr + 4) & 0x1ffffc;
                cmd = *(u32*)(cpu->ram + addr);
                execute_gp0_command(&cpu->gpu, cmd);
            }
            addr = entry & 0xffffff;
        } while (!(addr & 0x800000));
    }   break;
    }
}

static inline void dma_write(struct cpu_state* cpu, u32 offset, u32 value) // handle all DMA transfers
{
    struct dma_state* dma = &cpu->dma;

    u32 port = offset >> 4;
    switch (port)
    {
    case 0x7: // DICR/DPCR
        if (offset & 0x4)
        {
            dma->interrupt = value;
        }
        else
        {
            dma->control = value;
        }
        break;
    default: // channels 0-6
        SY_ASSERT(port == 2 || port == 6);
        switch (offset & 0xf)
        {
        case 0x0:
            dma->channels[port].madr = value;
            break;
        case 0x4: // TODO: this may be required to support 8 and 16-bit stores
            *(((u32*)(dma->channels + port)) + 1) = value;
            break;
        case 0x8:
            dma->channels[port].control = value & 0x71770703; // write mask
            if (dma->channels[port].control & 0x1000000) // for now, we do instant DMA transfers
            {
                process_dma(cpu, &dma->channels[port], port);
            }
            break;
        SY_INVALID_CASE;
        }
        break;
    }
}

static inline u32 dma_read(struct cpu_state* cpu, u32 offset)
{
    u32 result = 0;
    struct dma_state* dma = &cpu->dma;
    u32 port = offset >> 4;
    switch (port)
    {
    case 0x7:
        if (offset & 0x4)
        {
            result = dma->interrupt;
        }
        else
        {
            result = dma->control;
        }     
        break;
    default:
        switch (offset & 0xf)
        {
        case 0x0:
            result = dma->channels[port].madr;
            break;
        case 0x4:
            result = *(((u32*)(dma->channels + port)) + 1);
            break;
        case 0x8:
            result = dma->channels[port].control;
            break;
        SY_INVALID_CASE;
        }
        break;
    }
    return result;
}