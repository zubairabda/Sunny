static inline u32 timer_read(struct cpu_state* cpu, u32 offset)
{
    u32 timer_offset = offset & 0xf;
    u32 timer_index = (offset >> 4) & 0x3;
    struct root_counter* timer = &cpu->timers[timer_index];
    u32 result = 0;

    switch (timer_offset)
    {
    case 0x0:
    {
        s32 ticks_to_add = 0;
        if (timer_index < 2)
        {
            TimerSyncs sync = get_timer_syncs(&cpu->gpu);
            if (timer->mode.sync_enable)
            {
                switch (timer->mode.sync_mode)
                {
                case 0:
                    if (sync.xblank[timer_index])
                    {
                        ticks_to_add = 0;
                        goto temp;
                    }
                    break;
                case 1:
                    if (sync.xblank[timer_index])
                    {
                        ticks_to_add = 0;
                        goto temp; // temp until we compress this
                    }
                    break;
                case 2:
                    break;
                case 3:
                    break;
                }
            }
            if (timer->mode.clock_source & 0x1)
            {
                if (timer_index)
                {
                    // hblank
                }
                else
                {
                    // dotclock
                }
            }
            else // sysclk
            {
                ticks_to_add = timer->ticks;
            }
        }
        else
        {
            if (timer->mode.sync_enable)
            {
                switch (timer->mode.sync_mode)
                {
                case 0:
                case 3:
                    goto temp;
                    break;
                case 1:
                case 2:
                    break;
                }
            }
            if (timer->mode.clock_source >> 1)
            {
                // sysclk / 8
                ticks_to_add = (timer->ticks / 8); // NOTE: does not account for remainder
            }
            else
            {
                // sysclk
                ticks_to_add = timer->ticks;
            }
        }

temp:
        timer->value += ticks_to_add;
#if 1
        if (timer->value > timer->target)
        {
            timer->mode.reached_target = 1;
            if (timer->mode.reset_mode)
                timer->value = 0;
        }
        else if (timer->value > 0xffff)
        {
            timer->mode.reached_overflow = 1;
            timer->value = 0;
        }
#endif
        //timer->prev_value = timer->value;
        timer->ticks = 0;
        result = timer->value;
    }   break;
    case 0x4:
        result = timer->mode.value;
        timer->mode.reached_target = 0;
        timer->mode.reached_overflow = 0;
        break;
    case 0x8:
        result = timer->target;
        break;
    }
    return result;
}

static inline s32 get_timer_ticks_until_interrupt(u32 timer_index)
{
    return 0;
}

static inline void timer_store(struct cpu_state* cpu, u32 offset, u32 value)
{
    u32 timer_offset = offset & 0xf;
    u32 timer_index = (offset >> 4) & 0x3;
    SY_ASSERT(timer_index < 3);

    struct root_counter* timer = &cpu->timers[timer_index];
    //timer->prev_value = timer->value;

    switch (timer_offset)
    {
    case 0x0:
        timer->value = value;
        break;
    case 0x4:
        timer->mode.value = value;
        //timer->clock_delay = 1;
        timer->value = 0;
        timer->ticks = 0;

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
        timer->target = value;
        break;
    default:
        printf("Unexpected offset in timer write: %02xh\n", offset);
        break;
    }

    
}