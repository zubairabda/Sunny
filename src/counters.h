#ifndef COUNTERS_H
#define COUNTERS_H

#include "common.h"
#include "event.h"

typedef union 
{
    struct
    {
        u32 sync_enable : 1;
        u32 sync_mode : 2;
        u32 reset_after_target : 1;
        u32 irq_on_target : 1;
        u32 irq_on_max : 1;
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
    b8 gate;
    counter_mode mode;
    u32 remainder;
    u64 begin_ticks;
    u64 pause_ticks; // ticks that the timer was not active for
    u64 timestamp; // event timestamp ex: enter vblank
    u64 interrupt_event;
};

extern struct root_counter g_counters[3];

u32 counters_read(u32 offset);
void counters_store(u32 offset, u32 value);
void tick_counter(u32 index);

#endif /* COUNTERS_H */
