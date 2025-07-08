#ifndef MDEC_H
#define MDEC_H

#include "common.h"

enum
{
    MDEC_ENABLE_DATAOUT = (1 << 29),
    MDEC_ENABLE_DATAIN  = (1 << 30),
    MDEC_RESET          = (1 << 31)
};

void mdec_reset(u32 flags);
void mdec_command(u32 word);
b8 mdecout_on_dma(b8 dir_from_ram, s8 step, u32 size, u32 *paddr);
u32 mdec_getstat(void);
u32 mdec_read(void);

#endif /* MDEC_H */
