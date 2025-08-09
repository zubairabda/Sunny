#ifndef EVENT_H
#define EVENT_H

#include "allocator.h"

typedef void (*event_callback)(u32 param);

typedef struct tick_event
{
    struct tick_event *next;
    struct tick_event *prev;
    event_callback callback;
    u32 param;
    s32 period;
    u64 system_cycles_at_event;
    u64 id;
    b8 active;
    u8 pad[3];
    u32 next_run;
} tick_event;

extern u64 g_cycles_elapsed;
extern u64 g_target_cycles;

void scheduler_init(struct memory_arena *arena);
void scheduler_reset(void);

void remove_event(u64 id);
u64 schedule_event(event_callback callback, u32 param, s32 cycles_until_event, s32 period);
tick_event *get_event(u64 id);
void tick_events(void);

#endif /* EVENT_H */
