#include "event.h"
#include "allocator.h"
#include "cpu.h"

u64 g_cycles_elapsed;

static struct memory_pool s_event_pool;
static struct tick_event *s_sentinel_event;

#define MIN_TICK_COUNT 384

void scheduler_init(struct memory_arena *arena)
{
    s_event_pool = allocate_pool(arena, sizeof(struct tick_event), MAX_EVENT_COUNT);
    s_sentinel_event = pool_alloc(&s_event_pool);
    s_sentinel_event->next = s_sentinel_event;
    s_sentinel_event->prev = s_sentinel_event;
    s_sentinel_event->system_cycles_at_event = 0;
}

struct tick_event *find_first_event_with_id(enum event_id id)
{
    for (struct tick_event *event = s_sentinel_event->next; event != s_sentinel_event; event = event->next)
    {
        if (event->id == id) {
            return event;
        }
    }
    return NULL;
}

void remove_event(struct tick_event *event)
{
    event->next->prev = event->prev;
    event->prev->next = event->next;
    pool_dealloc(&s_event_pool, event);
}

#if 0
b8 update_events_with_id(enum event_id id, s32 cycles_until_event)
{
    b8 event_with_id_found = 0;
    for (struct tick_event *event = s_sentinel_event->next; event != s_sentinel_event; event = event->next)
    {
        if (event->id == id)
        {
            event_with_id_found = 1;
            event->system_cycles_at_event = g_cycles_elapsed + cycles_until_event;
            if (scheduler->target_cycle_count > event->system_cycles_at_event)
            {
                scheduler->target_cycle_count = event->system_cycles_at_event;
            }
        }
    }
    return event_with_id_found;
}
#endif

void schedule_event(event_callback callback, void *data, u32 param, s32 cycles_until_event, enum event_id id)
{
    SY_ASSERT(callback);
    // TODO: event ID
    //debug_log("Enqueuing event...\n");
    struct tick_event *event = pool_alloc(&s_event_pool);
    event->callback = callback;
    event->system_cycles_at_event = g_cycles_elapsed + cycles_until_event;
    event->data = data;
    event->id = id;

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
#if 0
    // if an event is scheduled to happen before the cycles remaining, we need to stop ticking the cpu when this event occurs to process it
    //if ((cpu->cycles_to_run - (cpu->cycles_this_step + cpu->cycles_taken)) > cycles_until_event)
    if (scheduler->target_cycle_count > event->system_cycles_at_event)
    {
        // TODO: double check this
        //cpu->cycles_to_run = cycles_until_event + cpu->cycles_taken;
        scheduler->target_cycle_count = event->system_cycles_at_event;
    }
#endif
}

void tick_events(u64 tick_count)
{
    struct tick_event *current = s_sentinel_event->next;
    while (current != s_sentinel_event)
    {
        if (g_cycles_elapsed >= current->system_cycles_at_event)
        {
            s32 cycles_late = -(s32)(current->system_cycles_at_event - g_cycles_elapsed);
            current->callback(current->data, current->param, cycles_late);
            remove_event(current);
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

void set_interrupt(void *data, u32 param, s32 cycles_late)
{
    struct cpu_state *cpu = data;
    cpu->i_stat |= param;
}
