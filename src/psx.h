#ifndef PSX_H
#define PSX_H

#include "allocator.h"
#include "config.h"

enum system_state
{
    SYSTEM_STATE_STOPPED = 0,
    SYSTEM_STATE_PAUSED,
    SYSTEM_STATE_RUNNING
};

extern enum system_state g_state;

b8 psx_load_image(const char *path);
void psx_reset(void);

b8 psx_can_boot(void);

void psx_init(struct memory_arena *arena, void *bios);
b32 psx_run(void);
void psx_step(void);

#endif /* PSX_H */
