typedef union 
{
    struct
    {
        u32 sync_enable : 1;
        u32 sync_mode : 2;
        u32 reset_mode : 1;
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
} TimerMode;

struct root_counter
{
    u32 value;
    u16 target;
    TimerMode mode;
    u32 cycles_elapsed; // TODO: pull this out?
    u32 prev_value;
    s32 prev_tick_count;
    s32 ticks;
    b8 clock_delay; // ref: psx-spx, when resetting by writing to mode or writing the current value, timer pauses for 2 clks
};