#include "timers.h"
#include "gpu.h"
#include "event.h"
#include "debug.h"

static void timer_tick(struct psx_state *psx, u32 timer_index)
{
    struct root_counter *timer = psx->timers[timer_index];
    if (!timer->paused)
    {
        switch (timer_index)
        {
        case 0:
        {
            if (timer->mode.clock_source & 0x1)
            {
                // dotclock
            }
            else
            {
                s32 ticks_to_add = safe_truncate32(g_cycles_elapsed - timer->prev_cycle_count);
                timer->prev_cycle_count = g_cycles_elapsed;
                ticks_to_add -= timer->sync_ticks;
                timer->value += ticks_to_add;
                
            }
            break;
        }
        case 1:
        {
            if (timer->mode.clock_source & 0x1)
            {
                // hblank
                u64 hblank_count = gpu_tick(psx->gpu);
                timer->value += hblank_count;
            }
            else
            {
                s32 ticks_to_add = safe_truncate32(g_cycles_elapsed - timer->prev_cycle_count);
                timer->prev_cycle_count = g_cycles_elapsed;
                timer->value += ticks_to_add;
                timer->value -= timer->pause_ticks;
                timer->pause_ticks = 0;
            }
            break;
        }
        case 2:
        {
            if (timer->mode.sync_enable && (timer->mode.sync_mode == 0 || timer->mode.sync_mode == 3)) {
                return;
            }
            s32 ticks_to_add = safe_truncate32(g_cycles_elapsed - timer->prev_cycle_count);
            if (timer->mode.clock_source & 0x2) // sysclk / 8
            {
                timer->remainder += ticks_to_add & 0x7;
                ticks_to_add >>= 3;
                if (timer->remainder >= 8) {
                    timer->remainder -= 8;
                    ++ticks_to_add;
                }
            }
            timer->prev_cycle_count = g_cycles_elapsed;
            timer->value += ticks_to_add;
            break;
        }
        SY_INVALID_CASE;
        }
    }
#if 0
    if (timer->clock_delay)
    {
        timer->clock_delay = 0;
        --timer->value;
    }
#endif
    if (timer->value >= timer->target)
    {
        timer->mode.reached_target = 1;
        if (timer->mode.reset_after_target && timer->value > timer->target)
        {
            g_debug.show_disasm = 1;
            // if the timer resets by reaching the target value, it will stay at 0 for 2 cycles
            u32 wrap_count = (timer->value + 1) / (timer->target + 2);

            timer->value %= (timer->target + 1);
            
            if (timer->value > 0)
                timer->value -= wrap_count;
        }
    }
    if (timer->value >= 0xffff)
    {
        timer->mode.reached_overflow = 1;
    }
    timer->value &= 0xffff;
}

u32 timers_read(struct psx_state *psx, u32 offset)
{
    u32 timer_offset = offset & 0xf;
    u32 timer_index = (offset >> 4) & 0x3;
    struct root_counter *timer = psx->timers[timer_index];
    u32 result = 0;

    switch (timer_offset)
    {
    case 0x0:
    {
        timer_tick(psx, timer_index);
        result = timer->value;
    }   break;
    case 0x4:
        if (timer_index == 1) {
            int a = 5;
        }
        timer_tick(psx, timer_index);
        result = timer->mode.value;
        timer->mode.reached_target = 0;
        timer->mode.reached_overflow = 0;
        break;
    case 0x8:
        result = timer->target;
        break;
    SY_INVALID_CASE;
    }
    //debug_log("TIMER %u READ -> %u\n", timer_index, result);
    return result;
}

void timers_store(struct psx_state *psx, u32 offset, u32 value)
{
    u32 timer_offset = offset & 0xf;
    u32 timer_index = (offset >> 4) & 0x3;
    //debug_log("TIMER %u STORE <- %u\n", timer_index, value);
    SY_ASSERT(timer_index < 3);

    struct root_counter *timer = psx->timers[timer_index];
    //timer->prev_value = timer->value;

    switch (timer_offset)
    {
    case 0x0:
        timer->value = value & 0xffff;
        if (value > timer->target)
        {
            // NOTE: hmm, don't think there are any tests for this
            timer->is_write_above_target = 1;
        }
        timer->clock_delay = 1;
        // NOTE: there is also an overflow check delay, but we won't handle that for now
        break;
    case 0x4:
        // timer mode register
        u32 old_value = timer->mode.value;
        timer->mode.value = value & 0x3ff; // bit 10 11 12 are readonly
        timer->mode.irq = 1;
        timer->clock_delay = 1;
        timer->value = 0;
        timer->prev_cycle_count = g_cycles_elapsed;
        timer->sync_ticks = 0;
        timer->sync = 0;
        timer->timestamp = 0;
        timer->remainder = 0;

        u32 change = old_value ^ timer->mode.value;
        if (change & 0x6) // sync mode changed
        {
            if (timer->mode.sync_enable)
            {
                switch (timer_index)
                {
                case 1:
                {
                    switch (timer->mode.sync_mode)
                    {
                    case 0:
                        // NOTE: im not sure of the behavior in these edge cases, if the timer modes are set
                        // during xblank, im assuming we wait for the next time we 'enter' it
                        if (in_vblank(psx->gpu))
                        {
                            timer->timestamp = g_cycles_elapsed;
                        }
                        break;
                    case 2:

                        break;
                    case 3:
                        timer->paused = 1;
                        break;
                    default:
                        break;
                    }
                    break;
                }
                default:
                    break;
                }
            }
        }

        if (change & (0x3 << 8)) // clock source changed
        {
            if (timer_index == 1)
            {
                if (timer->mode.clock_source & 0x1)
                {

                    psx->gpu->timestamp = g_cycles_elapsed;
                }
            }
        }

        // we dont handle timer irqs right now
        //SY_ASSERT(!(timer->mode.value & (0x3 << 4)));
        switch ((timer->mode.value >> 4) & 0x3)
        {
        case 1:
            //update_event(cpu, (enum interrupt_code)(0x10 << timer_index), get_timer_ticks_until_interrupt());
            break;
        case 2:
            break;
        case 3:
            break;
        }

        break;
    case 0x8:
        // TODO: tick timer here!
        timer->target = value & 0xffff;
        break;
    SY_INVALID_CASE;
    }
}
