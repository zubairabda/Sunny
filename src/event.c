#include "event.h"

#define MAX_EVENT_COUNT 16

u64 g_cycles_elapsed;
u64 g_target_cycles;

typedef struct tick_event
{
    struct tick_event *next;
    struct tick_event *prev;
    event_callback callback;
    u32 param;
    u32 reserved;
    u64 system_cycles_at_event;
    u64 id;
} tick_event;

static struct memory_pool s_event_pool;
static tick_event *s_sentinel_event;
static u64 id_count;

static u32 event_count; // TODO: remove

#define MIN_TICK_COUNT 384

void scheduler_reset(struct memory_arena *arena)
{
    event_count = 0;

    s_event_pool = allocate_pool(arena, sizeof(struct tick_event), MAX_EVENT_COUNT);
    s_sentinel_event = pool_alloc(&s_event_pool);
    s_sentinel_event->next = s_sentinel_event;
    s_sentinel_event->prev = s_sentinel_event;
    s_sentinel_event->system_cycles_at_event = 0;
    g_target_cycles = g_cycles_elapsed + MIN_TICK_COUNT;
}

static inline void event_dealloc(tick_event *event)
{
    event->next->prev = event->prev;
    event->prev->next = event->next;
    pool_dealloc(&s_event_pool, event);
}

void remove_event(u64 id)
{
    tick_event *current = s_sentinel_event->next;
    while (current != s_sentinel_event)
    {
        if (current->id == id)
        {
            event_dealloc(current);
            --event_count;
            return;
        }
        current = current->next;
    }
}

static void insert_event(tick_event *event)
{
    tick_event *current = s_sentinel_event->next;
    while (current != s_sentinel_event)
    {
        if (current->system_cycles_at_event > event->system_cycles_at_event)
            break;
        current = current->next;
    }
    current->prev->next = event;
    event->prev = current->prev;
    current->prev = event;
    event->next = current;

    // update the target if the event is scheduled before the current target
    if (g_target_cycles > event->system_cycles_at_event)
    {
        g_target_cycles = event->system_cycles_at_event;
    }
}

u64 schedule_event(event_callback callback, u32 param, s32 cycles_until_event)
{
    tick_event *event = pool_alloc(&s_event_pool);
    event->callback = callback;
    event->system_cycles_at_event = g_cycles_elapsed + cycles_until_event;
    event->id = ++id_count;
    event->param = param;
    if (event_count > 2)
        SY_ASSERT(1);
    insert_event(event);
    ++event_count;

    return event->id;
}

void tick_events(void)
{
    tick_event *current = s_sentinel_event->next;
    while (current != s_sentinel_event)
    {
        if (g_cycles_elapsed >= current->system_cycles_at_event)
        {
            current->next->prev = current->prev;
            current->prev->next = current->next;

            s32 cycles_late = (s32)(g_cycles_elapsed - current->system_cycles_at_event);
            current->callback(current->param, cycles_late);

            pool_dealloc(&s_event_pool, current);
            --event_count;
#if 0
            if (current->period)
            {
                s32 cycles_late = (s32)(g_cycles_elapsed - current->system_cycles_at_event);
                s32 next_run = current->period - cycles_late;
                current->system_cycles_at_event = g_cycles_elapsed + next_run;
                insert_event(current);
            }
            else
            {
                pool_dealloc(&s_event_pool, current);
            }
#endif
            current = s_sentinel_event->next;
        }
        else
        {
            break;
        }
    }

    if (s_sentinel_event->next != s_sentinel_event)
        g_target_cycles = s_sentinel_event->next->system_cycles_at_event;
    else
        g_target_cycles = g_cycles_elapsed + MIN_TICK_COUNT;
}
