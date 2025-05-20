#ifndef PSX_H
#define PSX_H

#include "allocator.h"
#include "config.h"
#include "stream.h"

b8 psx_load_image(const char *path);
void psx_reset(void);

b8 psx_can_boot(void);

void psx_init(struct memory_arena *arena, void *bios);
void psx_run(void);
void psx_step(void);

#endif /* PSX_H */
