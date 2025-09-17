#include "event.h"

u64 g_cycles_elapsed;
u64 g_target_cycles;

struct scheduler_state g_scheduler;

#define MIN_TICK_COUNT 384

void scheduler_reset(struct memory_arena *arena)
{
    memset(&g_scheduler, 0, sizeof(struct scheduler_state));
    g_scheduler.event_pool = allocate_pool(arena, sizeof(struct event_list_node), MAX_EVENT_COUNT);
    g_scheduler.sentinel = pool_alloc(&g_scheduler.event_pool);
    g_scheduler.sentinel->next = g_scheduler.sentinel;
    g_scheduler.sentinel->prev = g_scheduler.sentinel;
    g_scheduler.sentinel->data.timestamp = 0;
    g_target_cycles = UINT64_MAX;//g_cycles_elapsed + MIN_TICK_COUNT;
}

void clear_events(void)
{
    pool_free_all(&g_scheduler.event_pool);
    g_scheduler.sentinel = pool_alloc(&g_scheduler.event_pool);
    g_scheduler.sentinel->next = g_scheduler.sentinel;
    g_scheduler.sentinel->prev = g_scheduler.sentinel;
    g_scheduler.event_count = 0;
    g_target_cycles = UINT64_MAX;
}

u32 register_callback(event_callback callback)
{
    u32 result = g_scheduler.callback_count;
    g_scheduler.callback_list[g_scheduler.callback_count++] = callback;
    return result;
}

static inline void event_dealloc(struct event_list_node *event)
{
    --g_scheduler.event_count;
    event->next->prev = event->prev;
    event->prev->next = event->next;
    pool_dealloc(&g_scheduler.event_pool, event);
}

void remove_event(u64 id)
{
    struct event_list_node *current = g_scheduler.sentinel->next;
    while (current != g_scheduler.sentinel)
    {
        if (current->data.id == id)
        {
            event_dealloc(current);
            return;
        }
        current = current->next;
    }
}

void insert_event(struct event_list_node *event)
{
    struct event_list_node *current = g_scheduler.sentinel->next;
    while (current != g_scheduler.sentinel)
    {
        if (current->data.timestamp > event->data.timestamp)
            break;
        current = current->next;
    }
    current->prev->next = event;
    event->prev = current->prev;
    current->prev = event;
    event->next = current;

    // update the target if the event is scheduled before the current target
    if (g_target_cycles > event->data.timestamp)
    {
        g_target_cycles = event->data.timestamp;
    }
}

u64 schedule_event(u32 callback_id, u32 param, s32 cycles_until_event)
{
    SY_ASSERT(callback_id < g_scheduler.callback_count);
    ++g_scheduler.event_count;
    struct event_list_node *event = pool_alloc(&g_scheduler.event_pool);
    event->data.callback_id = callback_id;
    event->data.timestamp = g_cycles_elapsed + cycles_until_event;
    event->data.id = ++g_scheduler.id_count;
    event->data.param = param;
    insert_event(event);

    return event->data.id;
}

void tick_events(void)
{
    u64 next_tick = UINT64_MAX;
    struct event_list_node *current = g_scheduler.sentinel->next;
    while (current != g_scheduler.sentinel)
    {
        if (g_cycles_elapsed >= current->data.timestamp)
        {
            s32 cycles_late = (s32)(g_cycles_elapsed - current->data.timestamp);
            event_callback callback = g_scheduler.callback_list[current->data.callback_id];
            callback(current->data.param, cycles_late);

            event_dealloc(current);
            current = g_scheduler.sentinel->next;
        }
        else
        {
            next_tick = current->data.timestamp;
            break;
        }
    }
    g_target_cycles = next_tick;
}
