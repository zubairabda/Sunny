static inline void fifo_push(struct fifo_queue* fifo, u8* data, u32 size)
{
    for (u32 i = 0; i < size; ++i)
    {
        fifo->data[fifo->count++] = data[i];
    }
}

static void cdrom_ack(struct cpu_state* cpu, s32 flag)
{
    cpu->cdrom.status |= 0x20;
    cpu->cdrom.interrupt_flag |= flag;
}

static inline void cdrom_command(struct cpu_state* cpu, u8 cmd)
{
    struct cdrom_state* cdrom = &cpu->cdrom;

    switch (cmd)
    {
    case 0x1: // Getstat
        cdrom->response_fifo[cdrom->response_fifo_count] = 0xff; // temp
        cdrom->response_fifo_count += 1;
        //cdrom->status |= 0x20;
        //cdrom->interrupt_flag |= 0x3;
        schedule_event(cpu, cdrom_ack, 0x3, 10000);
        schedule_event(cpu, set_interrupt, INTERRUPT_CDROM, 50401);
        break;
    case 0x19: // Test
        switch (cdrom->param_fifo[0])
        {
        case 0x20:
            u8 revision[] = {0x94, 0x09, 0x19, 0xc0};
            memcpy(&cdrom->response_fifo[cdrom->response_fifo_count], revision, sizeof(revision));
            cdrom->response_fifo_count += 4;
            //cdrom->status |= 0x20; // response fifo set to non-empty
            //cdrom->interrupt_flag |= 0x3;
            schedule_event(cpu, cdrom_ack, 0x3, 10000);
            schedule_event(cpu, set_interrupt, INTERRUPT_CDROM, 50401);
            break;
        
        default:
            printf("Test command unhandled subfunction: %02x\n", cdrom->param_fifo[0]);
            break;
        }
        break;
    default:
        printf("Unhandled CDROM command: %02xh\n", cmd);
        break;
    }
}

static inline u8 cdrom_read(struct cdrom_state* cdrom, u32 offset)
{
    u8 result = 0;
    switch (offset)
    {
    case 0: // reg 0 is always the status
        result = cdrom->status;
        printf("CDROM read status register\n");
        break;
    case 1: // reg 1 is always the response fifo
        // TODO: clear to zero every reset?
        u8 value = cdrom->response_fifo[cdrom->response_fifo_current++];
        result = value;
        if (cdrom->response_fifo_current == cdrom->response_fifo_count)
        {
            cdrom->status &= ~(0x20);
            cdrom->response_fifo_count = 0;
            cdrom->response_fifo_current = 0;
            printf("CDROM read last response byte\n");
        }
        printf("CDROM read response fifo: %02xh\n", value);
        break;
    case 2: // reg 2 is always the data fifo
        printf("CDROM read data fifo\n");
        break;
    case 3:
        if (cdrom->status & 0x1) // index 1 or 3 = interrupt flag reg
        {
            result = cdrom->interrupt_flag |= ~0x1f;
            printf("CDROM read interrupt flag register: %08x\n", cdrom->interrupt_flag);
        }
        else // 0 or 2 = interrupt enable register
        {
            result = cdrom->interrupt_enable |= ~0x1f;
            printf("CDROM read interrupt enable register\n");
        }
        break;
    SY_INVALID_CASE;
    }
    return result;
}

static inline void cdrom_store(struct cpu_state* cpu, u32 offset, u8 value)
{
    struct cdrom_state* cdrom = &cpu->cdrom;

    switch (offset + ((cdrom->status & 0x3) * 4))
    {
    case 0:
    case 4:
    case 8:
    case 12:
        cdrom->status = (cdrom->status & ~0x3) | (value & 0x3); // bits 0-1 writable
        printf("CDROM store status register: %02x\n", value);
        break;
    case 1:
        SY_ASSERT(!(cdrom->interrupt_flag & 0x1f)); // interrupts must be acknowledged before sending a new command
        printf("CDROM send command: %02x\n", value);
        cdrom_command(cpu, value);
        break;
    case 2:
        cdrom->param_fifo[cdrom->param_fifo_count++] = value;
        cdrom->status &= ~(0x8);
        printf("CDROM send parameter: %02x\n", value);
        break;
    case 3:
        printf("CDROM request register\n");
        break;
    case 5:
        printf("CDROM sound map data out\n");
        break;
    case 6:
        cdrom->interrupt_enable = value & 0x1f;
        printf("CDROM set interrupt enable register: %02x\n", value);
        break;
    case 7:
        // NOTE: response interrupts are queued
        cdrom->interrupt_flag &= ~(value & 0x1f); // writing to bits resets them
        if (value & 0x40) // ref: psx-spx
        {
            cdrom->param_fifo_count = 0;
            cdrom->status |= 0x8; // bit 3 set when param_fifo empty
        }
        printf("CDROM acknowledge interrupts: %02x\n", value);
        break;
    case 9:
        printf("CDROM sound map coding info\n");
        break;
    case 10:
        printf("CDROM left-CD to left-SPU volume\n");
        break;
    case 11:
        printf("CDROM left-CD to right-SPU volume\n");
        break;
    case 13:
        printf("CDROM right-CD to right-SPU volume\n");
        break;
    case 14:
        printf("CDROM right-CD to left-SPU volume\n");
        break;
    case 15:
        printf("CDROM audio volume apply changes\n");
        break;
    SY_INVALID_CASE;
    }
}