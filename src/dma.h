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