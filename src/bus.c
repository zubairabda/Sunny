static void set_interrupt(struct cpu_state* cpu, s32 interrupt)
{
    cpu->i_stat |= interrupt;
}

static inline void gpu_tick(struct cpu_state* cpu, u32 count)
{
    cpu->gpu.video_cycles += count * (11.0f / 7.0f);
    if (cpu->gpu.video_cycles >= cpu->gpu.horizontal_timing)
    {
        cpu->gpu.video_cycles = 0.0f;
        ++cpu->gpu.scanline;
        if (cpu->gpu.scanline >= cpu->gpu.vertical_timing)
        {
            cpu->gpu.scanline = 0;
#if SOFTWARE_RENDERING
            RECT screen;
            GetClientRect(g_app.window, &screen);
            StretchDIBits(g_app.hdc, 0, 0, screen.right, screen.bottom, 0, 0, VRAM_WIDTH, VRAM_HEIGHT, g_debug.psx->gpu.vram, &g_app.bitmap_info, DIB_RGB_COLORS, SRCCOPY);
#else
            g_renderer->render_frame();
#endif
            cpu->gpu.stat.value ^= 0x80000000; // bit 31 toggled per frame
            cpu->i_stat |= INTERRUPT_VBLANK;
        }
    }
}

typedef struct
{
    union
    {
        struct
        {
            b8 in_hblank;
            b8 in_vblank;
        };
        b8 xblank[2];
    };
} TimerSyncs;

#define MAX_EVENT_COUNT 8
static u32 event_count;

typedef void (*event_callback)(struct cpu_state*, s32 data); // should realistically take a void*

struct tick_event
{
    struct tick_event* next;
    struct tick_event* prev;
    event_callback callback;
    s32 system_cycles_at_event;
    s32 data;
};

static inline void remove_event(struct tick_event* event)
{
    printf("Removing event...\n");
    event->next->prev = event->prev;
    event->prev->next = event->next;
}

static inline void schedule_event(struct cpu_state* cpu, event_callback callback, s32 data, u32 ticks_until_event)
{
    struct tick_event* new_event = pool_alloc(&cpu->event_pool);

    new_event->prev = cpu->sentinel_event;
    new_event->next = cpu->sentinel_event->next;

    new_event->next->prev = new_event;
    new_event->prev->next = new_event;

    new_event->system_cycles_at_event = cpu->system_tick_count + ticks_until_event;
    new_event->callback = callback;
    new_event->data = data;
}

static inline void tick_events(struct cpu_state* cpu, u32 count)
{
    if (cpu->sentinel_event->next == cpu->sentinel_event)
    {
        cpu->system_tick_count = 0;
    }
    else
    {
        ++cpu->system_tick_count;
        const s32 ticks = cpu->system_tick_count;
        for (struct tick_event* event = cpu->sentinel_event->next; event != cpu->sentinel_event; event = event->next)
        {
            if (ticks >= event->system_cycles_at_event)
            {
                printf("Executing event....\n");
                event->callback(cpu, event->data);
                remove_event(event);
                pool_dealloc(&cpu->event_pool, event);
            }
        }
    }

}

static inline TimerSyncs get_timer_syncs(struct gpu_state* gpu)
{
    TimerSyncs result;
    result.in_hblank = (gpu->video_cycles < gpu->horizontal_display_x1 || gpu->video_cycles > gpu->horizontal_display_x2);
    result.in_vblank = (gpu->scanline < gpu->vertical_display_y1 || gpu->scanline > gpu->vertical_display_y2);
    return result;
}