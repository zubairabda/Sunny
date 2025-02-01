#ifndef COUNTERS_H
#define COUNTERS_H

#include "common.h"

typedef union 
{
    struct
    {
        u32 sync_enable : 1;
        u32 sync_mode : 2;
        u32 reset_after_target : 1;
        u32 irq_on_target : 1;
        u32 irq_on_overflow : 1;
        u32 irq_repeat_mode : 1;
        u32 irq_toggle_mode : 1;
        u32 clock_source : 2;
        u32 irq : 1;
        u32 reached_target : 1;
        u32 reached_overflow : 1;
    };
    u32 value;
} counter_mode;

struct root_counter
{
    u32 value;
    u16 target;
    counter_mode mode;
    u32 remainder;
    u64 prev_cycle_count;
    u64 pause_ticks;
    u64 timestamp; // event timestamp ex: enter vblank
    u64 interrupt_event_id;
    b8 sync;
    //b8 clock_delay; // ref: psx-spx, when resetting by writing to mode or writing the current value, timer pauses for 2 clks
    //b8 paused;
    b8 is_write_above_target;
};

extern struct root_counter g_counters[3];

u32 counters_read(u32 offset);
void counters_store(u32 offset, u32 value);

#endif /* COUNTERS_H */
