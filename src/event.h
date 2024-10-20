#ifndef EVENT_H
#define EVENT_H

#include "allocator.h"

typedef void (*event_callback)(u32 param, s32 cycles_late);

extern u64 g_cycles_elapsed;

void scheduler_init(struct memory_arena *arena);

void remove_event(u64 id);
u64 schedule_event(event_callback callback, u32 param, s32 cycles_until_event);
void tick_events(u64 tick_count);
u64 get_tick_count(void);

#endif /* EVENT_H */
