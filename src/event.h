#ifndef EVENT_H
#define EVENT_H

#include "allocator.h"

#define MAX_EVENT_COUNT 16

typedef void (*event_callback)(u32 param, s32 ticks_late);

typedef struct tick_event
{
    u32 callback_id;
    u32 param;
    u64 timestamp;
    u64 id;
} tick_event;

struct event_list_node
{
    struct event_list_node *next;
    struct event_list_node *prev;
    tick_event data;
};

struct scheduler_state
{
    struct memory_pool event_pool;
    struct event_list_node *sentinel;
    u64 id_count;
    u32 event_count;
    u32 callback_count;
    event_callback callback_list[MAX_EVENT_COUNT];
};

extern struct scheduler_state g_scheduler;

extern u64 g_cycles_elapsed;
extern u64 g_target_cycles;

void scheduler_reset(struct memory_arena *arena);
u32 register_callback(event_callback callback);
void clear_events(void);
void remove_event(u64 id);
u64 schedule_event(u32 callback_id, u32 param, s32 cycles_until_event);
void insert_event(struct event_list_node *event);
void tick_events(void);

#endif /* EVENT_H */
