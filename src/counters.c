#include "counters.h"
#include "gpu.h"
#include "debug.h"
#include "cpu.h"

struct root_counter g_counters[3];
static u32 timer_callback_id;

static inline void add_sysclk_ticks(struct root_counter *counter)
{
    u64 ticks = g_cycles_elapsed - counter->begin_ticks;
    if (ticks > 0xffff)
    {
        ticks = 0xffff;
    }
    //u32 ticks_to_add = safe_truncate32(g_cycles_elapsed - counter->prev_cycle_count);
    counter->begin_ticks = g_cycles_elapsed;
    counter->value += ticks;//ticks_to_add;
}

void tick_counter(u32 index)
{
    struct root_counter *counter = &g_counters[index];

    //if (index < 2)
    //    gpu_hsync();

    switch (index)
    {
    case 0:
    {
        if (counter->mode.clock_source & 0x1)
        {

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
#if 1
        u64 ticks_added = 0;
        if (counter->mode.sync_enable)
        {
            switch (counter->mode.sync_mode)
            {
            case 0:
            {
                ticks_added = g_cycles_elapsed - counter->begin_ticks;
                ticks_added -= counter->pause_ticks;
                counter->pause_ticks = 0;
                if (in_vblank())
                {
                    // pause ticks haven't been added yet since its not end of vblank, so add them here
                    ticks_added -= g_cycles_elapsed - counter->timestamp;
                    counter->timestamp = g_cycles_elapsed;
                }
                break;
            }
            case 1:
            {
                ticks_added = g_cycles_elapsed - counter->timestamp;
                counter->timestamp = g_cycles_elapsed;
                break;
            }
            case 2:
            {
                if (in_vblank())
                {
                    ticks_added = g_cycles_elapsed - counter->timestamp;
                    counter->timestamp = g_cycles_elapsed;
                }
                break;
            }
            case 3:
            {
                if (counter->gate)
                    ticks_added = g_cycles_elapsed - counter->begin_ticks;
                break;
            }
            }
        }
        else
        {
            ticks_added = g_cycles_elapsed - counter->begin_ticks;
        }

        counter->begin_ticks = g_cycles_elapsed;

        if (counter->mode.clock_source & 0x1)
        {
            // convert CPU cycles to hblanks (roughly)
            ticks_added *= 715909;
            ticks_added += counter->remainder;
            u64 hblanks = ticks_added / (451584 * NTSC_VIDEO_CYCLES_PER_SCANLINE);
            counter->remainder = ticks_added % (451584 * NTSC_VIDEO_CYCLES_PER_SCANLINE);
            ticks_added = hblanks;
        }

        counter->value += safe_truncate32(ticks_added);
#endif
        break;
    }
    case 2:
    {
        if (counter->mode.sync_enable && (counter->mode.sync_mode == 0 || counter->mode.sync_mode == 3))
        {
            return;
        }
        u32 ticks_to_add = safe_truncate32(g_cycles_elapsed - counter->begin_ticks);
        if (counter->mode.clock_source & 0x2) // sysclk / 8
        {
            ticks_to_add += counter->remainder;
            counter->remainder = ticks_to_add & 0x7;
            ticks_to_add >>= 3;
        }
        counter->begin_ticks = g_cycles_elapsed;
        counter->value += ticks_to_add;
        break;
    }
    INVALID_CASE;
    }

    if (counter->value >= counter->target)
    {
        counter->mode.reached_target = 1;
        if (counter->mode.reset_after_target && counter->value > counter->target)
        {
            // if the timer resets by reaching the target value, it will stay at 0 for 2 cycles
            //u32 wrap_count = (counter->value + 1) / (counter->target + 2);
            //u32 wrap_count = counter->value / (counter->target + 1);

            counter->value %= (counter->target + 1);
            
            //if (counter->value > 0)
            //    counter->value -= wrap_count;
        }
    }
    if (counter->value >= 0xffff)
    {
        counter->mode.reached_overflow = 1;
    }
    counter->value &= 0xffff;
}

static s32 get_timer_ticks_until_interrupt(u32 timer_index)
{
    struct root_counter *counter = &g_counters[timer_index];
    switch (timer_index)
    {
    case 2:
    {
        if (!counter->mode.sync_enable || counter->mode.sync_mode == 1 || counter->mode.sync_mode == 2)
        {
            s32 tick_count = 0;

            if (counter->mode.irq_on_target)
            {
                tick_count = ((0x10000 - counter->value) + counter->target) & 0xffff;
                if (!tick_count)
                    tick_count = counter->target + 1; // tick count being 0 means were at the target already, next int in target + 1
            }
            else if (counter->mode.irq_on_max)
            {
                tick_count = 0xffff - counter->value;
                if (!tick_count)
                    tick_count = 0x10000;
            }
            
            if (counter->mode.clock_source & 0x2)
            {
                tick_count <<= 3;
            }

            return tick_count;
        }
        else
            SY_ASSERT(0);
        break;
    }
    default:
        break;
    }
    return 0;
}

static void timer_interrupt(u32 timer_index, s32 ticks_late)
{
    struct root_counter *counter = &g_counters[timer_index];
    tick_counter(timer_index);

    if (counter->mode.irq_repeat_mode)
    {
        if (counter->mode.irq_toggle_mode)
        {
            counter->mode.irq ^= 1;
        }
        else
        {
            counter->mode.irq = 0; // TODO: temp
        }

        counter->interrupt_event = schedule_event(timer_callback_id, timer_index, get_timer_ticks_until_interrupt(timer_index));
    }
    else
    {
        counter->mode.irq = 0;
    }

    if (counter->mode.irq == 0)
        g_cpu.i_stat |= (u32)INTERRUPT_TIMER0 << timer_index;
}

void counters_reset(void)
{
    for (int i = 0; i < 3; ++i)
        memset(&g_counters[i], 0, sizeof(struct root_counter));
    timer_callback_id = register_callback(timer_interrupt);
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
        tick_counter(timer_index);
        result = counter->value;
        break;
    case 0x4:
        tick_counter(timer_index);
        counter->mode.irq = 1; // NOTE: temp hack
        result = counter->mode.value;
        counter->mode.reached_target = 0;
        counter->mode.reached_overflow = 0;
        break;
    case 0x8:
        result = counter->target;
        break;
    INVALID_CASE;
    }
    return result;
}

void counters_store(u32 offset, u32 value)
{
    u32 timer_offset = offset & 0xf;
    u32 timer_index = (offset >> 4) & 0x3;
    SY_ASSERT(timer_index < 3);

    struct root_counter *counter = &g_counters[timer_index];
    //timer->prev_value = timer->value;

    switch (timer_offset)
    {
    case 0x0:
        counter->value = value & 0xffff;
        if (counter->interrupt_event)
        {
            remove_event(counter->interrupt_event);
            counter->interrupt_event = schedule_event(timer_callback_id, timer_index, get_timer_ticks_until_interrupt(timer_index));
        }
        break;
    case 0x4:
        // timer mode register
        if (timer_index < 2)
        {
            //gpu_hsync();
            //timer->sync_ticks = psx->gpu->scanline_cycles;
        }

        counter->mode.value = value & 0x3ff; // bit 10 11 12 are readonly
        counter->mode.irq = 1;
        //counter->clock_delay = 1;
        counter->value = 0;
        counter->begin_ticks = g_cycles_elapsed;
        counter->pause_ticks = 0;
        counter->timestamp = g_cycles_elapsed; // TODO: remove?
        counter->remainder = 0;
        counter->gate = false;
        remove_event(counter->interrupt_event);
        counter->interrupt_event = 0;
        // check bit 4-5 for IRQ mode
        if (counter->mode.value & (0x3 << 4)) 
        {
            SY_ASSERT(timer_index == 2);
            counter->interrupt_event = schedule_event(timer_callback_id, timer_index, get_timer_ticks_until_interrupt(timer_index));
        }
        break;
    case 0x8:
        // TODO: tick timer here?
        counter->target = value & 0xffff;
        break;
    INVALID_CASE;
    }
}
