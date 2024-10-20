#ifndef GTE_H
#define GTE_H

#include "common.h"

void gte_command(u32 command);
u32 gte_read(u32 reg);
void gte_write(u32 reg, u32 value);

#endif /* GTE_H */
