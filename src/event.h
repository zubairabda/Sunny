#ifndef EVENT_H
#define EVENT_H

#include "allocator.h"

#define MAX_EVENT_COUNT 16

enum event_id
{
    EVENT_ID_DEFAULT,
    EVENT_ID_GPU_HBLANK_BEGIN,
    EVENT_ID_GPU_HBLANK_END,
    EVENT_ID_GPU_HBLANK,
    EVENT_ID_GPU_SCANLINE_COMPLETE,
    EVENT_ID_CDROM_CAUSE
};

typedef void (*event_callback)(void *data, u32 param, s32 cycles_late);

struct tick_event
{
    struct tick_event *next;
    struct tick_event *prev;
    event_callback callback;
    //s32 cycles_until_event;
    void *data;
    u32 param;
    enum event_id id;
    u64 system_cycles_at_event;
};

extern u64 g_cycles_elapsed;

void scheduler_init(struct memory_arena *arena);

void set_interrupt(void *cpu, u32 interrupt, s32 cycles_late);

struct tick_event *find_first_event_with_id(enum event_id id);

void remove_event(struct tick_event *event);
void schedule_event(event_callback callback, void *data, u32 param, s32 cycles_until_event, enum event_id id);
void tick_events(u64 tick_count);
u64 get_tick_count(void);

#endif
