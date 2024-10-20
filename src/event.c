#include "event.h"
#include "allocator.h"
#include "cpu.h"

#define MAX_EVENT_COUNT 16

struct tick_event
{
    struct tick_event *next;
    struct tick_event *prev;
    event_callback callback;
    u64 system_cycles_at_event;
    u64 id;
    //s32 cycles_until_event;
    //void *data;
    u32 param;
};

u64 g_cycles_elapsed;

static struct memory_pool s_event_pool;
static struct tick_event *s_sentinel_event;
static u64 id_count;

#define MIN_TICK_COUNT 384

void scheduler_init(struct memory_arena *arena)
{
    s_event_pool = allocate_pool(arena, sizeof(struct tick_event), MAX_EVENT_COUNT);
    s_sentinel_event = pool_alloc(&s_event_pool);
    s_sentinel_event->next = s_sentinel_event;
    s_sentinel_event->prev = s_sentinel_event;
    s_sentinel_event->system_cycles_at_event = 0;
}

static inline void event_dealloc(struct tick_event *event)
{
    event->next->prev = event->prev;
    event->prev->next = event->next;
    pool_dealloc(&s_event_pool, event);
}

void remove_event(u64 id)
{
    if (!id) 
    {
        return;
    }

    struct tick_event *current = s_sentinel_event->next;
    while (current != s_sentinel_event)
    {
        if (current->id == id)
        {
            event_dealloc(current);
            return;
        }
        current = current->next;
    }
}

u64 schedule_event(event_callback callback, u32 param, s32 cycles_until_event)
{
    //SY_ASSERT(callback);
    // TODO: event ID
    //debug_log("Enqueuing event...\n");
    struct tick_event *event = pool_alloc(&s_event_pool);
    event->callback = callback;
    event->system_cycles_at_event = g_cycles_elapsed + cycles_until_event;
    //event->data = data;
    event->param = param;
    event->id = ++id_count;
    
    struct tick_event *current = s_sentinel_event->next;
    while (current != s_sentinel_event)
    {
        if (current->system_cycles_at_event >= event->system_cycles_at_event)
            break;
        current = current->next;
    }
    current->prev->next = event;
    event->prev = current->prev;
    current->prev = event;
    event->next = current;
    return event->id;
}

u64 reschedule_event(event_callback callback, s32 param, u64 event_id, s32 cycles_until_event)
{
    struct tick_event *current = s_sentinel_event->next;
    while (current != s_sentinel_event) {
        if (current->id == event_id) {
            current->system_cycles_at_event = g_cycles_elapsed + cycles_until_event;
            return event_id;
        }
    }
    return schedule_event(callback, param, cycles_until_event);
}

void tick_events(u64 tick_count)
{
    struct tick_event *current = s_sentinel_event->next;
    while (current != s_sentinel_event)
    {
        if (g_cycles_elapsed >= current->system_cycles_at_event)
        {
            s32 cycles_late = -(s32)(current->system_cycles_at_event - g_cycles_elapsed);
            current->callback(current->param, cycles_late);
            event_dealloc(current);
            current = s_sentinel_event->next;
        }
        else
        {
            break;
        }
    }
}

u64 get_tick_count(void)
{
    if (s_sentinel_event->next != s_sentinel_event)
    {
        SY_ASSERT(s_sentinel_event->next->system_cycles_at_event > g_cycles_elapsed);
        return s_sentinel_event->next->system_cycles_at_event - g_cycles_elapsed;
    }
    else
    {
        return MIN_TICK_COUNT;
    }
}
