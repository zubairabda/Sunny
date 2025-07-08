#include "debug.h"
#include "allocator.h"

struct debug_state g_debug;

static inline u32 breakpoint_get_index(u32 addr)
{
    u32 hash = murmur3_mix32(addr);
    u32 index = hash & (MAX_BREAKPOINTS - 1);
    return index;
}

struct debug_breakpoint *breakpoint_get(u32 addr)
{
    u32 index = breakpoint_get_index(addr);

    u32 start = index;
    u32 probe_count = 0;
    do
    {
        struct debug_breakpoint *bp = &g_debug.bp[index];
        if (bp->active)
        {
            if (bp->pc == addr)
                return bp;
            else if (bp->hash_psl > probe_count)
                break;
        }
        else
        {
            break;
        }
        index = (index + 1) & (MAX_BREAKPOINTS - 1);
    } while (++probe_count < g_debug.max_psl);

    return NULL;
}

void breakpoint_set(u32 addr)
{
    // NOTE: no duplicate checks
    u32 index = breakpoint_get_index(addr);
    u32 start = index;

    u32 key = addr;

    u32 probe_count = 0;
    do
    {
        struct debug_breakpoint *bp = &g_debug.bp[index];
        if (!bp->active)
        {
            bp->active = true;
            bp->pc = key;
            bp->hash_psl = probe_count;
            ++g_debug.breakpoint_count;
            break;
        }
        else if (probe_count > bp->hash_psl)
        {
            u32 swap = key;
            key = bp->pc;
            probe_count = bp->hash_psl;
            bp->pc = swap;
        }
        ++probe_count;
        
        index = (index + 1) & (MAX_BREAKPOINTS - 1);
    } while (index != start);

    if (probe_count > g_debug.max_psl)
        g_debug.max_psl = probe_count;
}

void breakpoint_remove(u32 addr)
{
    struct debug_breakpoint *bp = breakpoint_get(addr);
    if (bp)
    {
        --g_debug.breakpoint_count;
        bp->active = false;

        ptrdiff_t diff = bp - &g_debug.bp[0];
        u32 index = (u32)diff;

        // backwards shift elements by one to fill the empty slot
        for (;;)
        {
            u32 last_index = index;
            index = (index + 1) & (MAX_BREAKPOINTS - 1);
            struct debug_breakpoint *prev = &g_debug.bp[last_index];
            struct debug_breakpoint *shift = &g_debug.bp[index];
            if (shift->active)
            {
                if (shift->hash_psl > 0)
                {
                    prev->hash_psl = shift->hash_psl - 1;
                    prev->pc = shift->pc;
                    prev->active = true;
                    shift->active = false;
                }
                else
                {
                    break;
                }
            }
            else
            {
                break;
            }
        }
    }
}
