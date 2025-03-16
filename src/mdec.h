#ifndef MDEC_H
#define MDEC_H

#include "common.h"

enum mdec_reset_flags
{
    MDEC_ENABLE_DATAOUT = (1 << 29),
    MDEC_ENABLE_DATAIN  = (1 << 30),
    MDEC_RESET          = (1 << 31)
};

void mdec_reset(u32 flags);
void mdec_command(u32 word);
void mdec_on_dma(void);
u32 mdec_getstat(void);
u32 mdec_read(void);

#endif /* MDEC_H */
