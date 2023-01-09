struct fifo_queue
{
    u16 count;
    u8 data[16];
};

struct cdrom_state
{
    u8 status;
    u8 interrupt_enable;
    u8 interrupt_flag;

    u16 response_fifo_current;
    u16 response_fifo_count;
    u8 response_fifo[16];
    
    u16 param_fifo_count;
    u8 param_fifo[16];

    u32 remaining_cycles;
};