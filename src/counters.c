#include "counters.h"
#include "gpu.h"
#include "event.h"
#include "debug.h"
#include "cpu.h"

struct root_counter g_counters[3];

static inline void add_sysclk_ticks(struct root_counter *counter)
{
    u64 ticks = g_cycles_elapsed - counter->prev_cycle_count;
    if (ticks > 0xffff) {
        ticks = 0xffff;
    }
    //u32 ticks_to_add = safe_truncate32(g_cycles_elapsed - counter->prev_cycle_count);
    counter->prev_cycle_count = g_cycles_elapsed;
    counter->value += ticks;//ticks_to_add;
}

static void tick_counter(u32 index)
{
    struct root_counter *counter = &g_counters[index];

    if (index < 2)
        gpu_hsync();

    switch (index)
    {
    case 0:
    {
        //////////////////////////////////////////////
        if (counter->mode.clock_source & 0x1)
        {
            // dotclock
            if (counter->mode.sync_enable)
            {
                switch (counter->mode.sync_mode)
                {

                }
            }
            else
            {
                u32 ticks_to_add = (u32)g_gpu.video_cycles;
                counter->value += ticks_to_add / g_gpu.dot_div;
                g_gpu.video_cycles = 0;
            }
        }
        else
        {
            // sysclk
            if (counter->mode.sync_enable)
            {
                switch (counter->mode.sync_mode)
                {
                case 0:
                    add_sysclk_ticks(counter);
                    counter->value -= counter->pause_ticks;
                    counter->pause_ticks = 0;
                    break;
                case 2:
                    break;
                case 3:
                    break;
                }
            }
            else
            {
                add_sysclk_ticks(counter);
            }
        }
        break;
    }
    case 1:
    {
        if (counter->mode.clock_source & 0x1)
        {
            if (counter->mode.sync_enable)
            {

            }
            else
            {
                counter->value += g_gpu.hblanks;
                g_gpu.hblanks = 0;
            }
        }
        else
        {
            u32 ticks_to_add = safe_truncate32(g_cycles_elapsed - counter->prev_cycle_count);
            counter->prev_cycle_count = g_cycles_elapsed;
            counter->value += ticks_to_add;
            counter->value -= counter->pause_ticks;
            if (counter->mode.sync_enable)
            {
                if (counter->mode.sync_mode == 0)
                {
                    if (in_vblank())
                    {
                        // if the timer was in vblank, then we havent added the pause ticks yet
                        counter->value -= safe_truncate32(g_cycles_elapsed - counter->timestamp);
                        // need to set the timestamp again, otherwise pause ticks will still contain the pause ticks
                        counter->timestamp = g_cycles_elapsed;
                    }
                }
            }
            counter->pause_ticks = 0;
        }
        break;
    }
    case 2:
    {
        if (counter->mode.sync_enable && (counter->mode.sync_mode == 0 || counter->mode.sync_mode == 3)) {
            return;
        }
        u32 ticks_to_add = safe_truncate32(g_cycles_elapsed - counter->prev_cycle_count);
        if (counter->mode.clock_source & 0x2) // sysclk / 8
        {
            ticks_to_add += counter->remainder;
            counter->remainder = ticks_to_add & 0x7;
            ticks_to_add >>= 3;
        }
        counter->prev_cycle_count = g_cycles_elapsed;
        counter->value += ticks_to_add;
        break;
    }
    INVALID_CASE;
    }
#if 0
    if (timer->clock_delay)
    {
        timer->clock_delay = 0;
        --timer->value;
    }
#endif
    if (counter->value >= counter->target)
    {
        counter->mode.reached_target = 1;
        if (counter->mode.reset_after_target && counter->value > counter->target)
        {
            // if the timer resets by reaching the target value, it will stay at 0 for 2 cycles
            u32 wrap_count = (counter->value + 1) / (counter->target + 2);

            counter->value %= (counter->target + 1);
            
            if (counter->value > 0)
                counter->value -= wrap_count;
        }
    }
    if (counter->value >= 0xffff)
    {
        counter->mode.reached_overflow = 1;
    }
    counter->value &= 0xffff;
}

u32 counters_read(u32 offset)
{
    u32 timer_offset = offset & 0xf;
    u32 timer_index = (offset >> 4) & 0x3;
    struct root_counter *counter = &g_counters[timer_index];
    u32 result = 0;

    switch (timer_offset)
    {
    case 0x0:
    {
        tick_counter(timer_index);
        result = counter->value;
    }   break;
    case 0x4:
        tick_counter(timer_index);
        result = counter->mode.value;
        counter->mode.reached_target = 0;
        counter->mode.reached_overflow = 0;
        break;
    case 0x8:
        result = counter->target;
        break;
    INVALID_CASE;
    }
    //debug_log("TIMER %u READ -> %u\n", timer_index, result);
    return result;
}

static u32 get_timer_ticks_until_interrupt(u32 timer_index)
{
    struct root_counter *counter = &g_counters[timer_index];
    switch (timer_index)
    {
    case 2:
    {
        if (!counter->mode.sync_enable || counter->mode.sync_mode == 1 || counter->mode.sync_mode == 2)
        {
            u32 tick_count = 0;
            if (counter->mode.irq_on_target) {
                tick_count = counter->target;
            }
            else if (counter->mode.irq_on_overflow) {
                tick_count = 0xffff;
            }

            if (counter->mode.clock_source & 0x2) {
                tick_count <<= 3;
            }

            return tick_count;
        }
        break;
    }
    default:
        break;
    }
    return 0;
}

static void timer_interrupt(u32 timer_index, s32 cycles_late)
{
    // TODO: currently timer interrupts break the shell
    g_cpu.i_stat |= ((u32)INTERRUPT_TIMER0) << timer_index;
    g_counters[timer_index].interrupt_event_id = schedule_event(timer_interrupt, timer_index, get_timer_ticks_until_interrupt(timer_index));
}

void counters_store(u32 offset, u32 value)
{
    u32 timer_offset = offset & 0xf;
    u32 timer_index = (offset >> 4) & 0x3;
    //debug_log("TIMER %u STORE <- %u\n", timer_index, value);
    SY_ASSERT(timer_index < 3);

    struct root_counter *counter = &g_counters[timer_index];
    //timer->prev_value = timer->value;

    switch (timer_offset)
    {
    case 0x0:
        tick_counter(timer_index);
        counter->value = value & 0xffff;
        if (value > counter->target)
        {
            // NOTE: hmm, don't think there are any tests for this
            counter->is_write_above_target = 1;
        }
        //counter->clock_delay = 1;
        // NOTE: there is also an overflow check delay, but we won't handle that for now
        break;
    case 0x4:
        // timer mode register
        if (timer_index < 2)
        {
            gpu_hsync();
            //timer->sync_ticks = psx->gpu->scanline_cycles;
        }

        u32 old_value = counter->mode.value;
        counter->mode.value = value & 0x3ff; // bit 10 11 12 are readonly
        counter->mode.irq = 1;
        //counter->clock_delay = 1;
        counter->value = 0;
        counter->prev_cycle_count = g_cycles_elapsed;
        counter->sync = 0;
        counter->pause_ticks = 0;
        counter->timestamp = g_cycles_elapsed; // TODO: remove?
        counter->remainder = 0;

        if (counter->mode.value & (0x3 << 4)) 
        {
            remove_event(counter->interrupt_event_id);
            counter->interrupt_event_id = schedule_event(timer_interrupt, timer_index, get_timer_ticks_until_interrupt(timer_index));
        }

        break;
    case 0x8:
        // TODO: tick timer here?
        counter->target = value & 0xffff;
        break;
    INVALID_CASE;
    }
}
