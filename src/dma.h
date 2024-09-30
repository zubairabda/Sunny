#ifndef DMA_H
#define DMA_H

#include "common.h"

void dma_init(void);
u32 dma_read(u32 offset);
void dma_write(u32 offset, u32 value);

#endif /* DMA_H */