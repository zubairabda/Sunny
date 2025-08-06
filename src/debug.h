#ifndef DEBUG_H
#define DEBUG_H

#include "allocator.h"

#include <stdarg.h>
#include <stdio.h>

#define MAX_BREAKPOINTS 32

struct debug_breakpoint
{
    u32 pc;
    u8 active;
    u8 hash_psl;
    u16 hit_count;
};

struct debug_state
{
    FILE *output;
    int last_char;
    int log_level;
    u32 log_mask;
    b8 recording;
    u16 *sound_buffer;
    u32 sound_buffer_len;
    b8 breakpoints_enabled;
    b8 breakpoint_was_hit;
    u8 breakpoint_count;
    u8 max_psl;
    struct debug_breakpoint bp[MAX_BREAKPOINTS];
    b8 pause_on_vblank;
    b8 pause;
    u64 timestamp;
    u64 cpu_ticks;
};

extern struct debug_state g_debug;

enum debug_log_level
{
    LOG_ERROR,
    LOG_WARN,
    LOG_INFO,
    LOG_DEBUG
};

struct debug_breakpoint *breakpoint_get(u32 addr);
void breakpoint_set(u32 addr);
void breakpoint_remove(u32 addr);

inline void log_write(enum debug_log_level level, char* msg, ...)
{
    if (level > g_debug.log_level)
        return;
    va_list args;
    va_start(args, msg);
    vfprintf(g_debug.output, msg, args);
    va_end(args);
}

inline void log_char(int c)
{
    if (g_debug.last_char == 27) {
        // handle escape sequence
        if (c == '[') {

        }
    }
    fputc(c, g_debug.output);
    g_debug.last_char = c;
}

#if SY_DEBUG
#define debug_error(...) log_write(LOG_ERROR, __VA_ARGS__)
#define debug_warn(...) log_write(LOG_WARN, __VA_ARGS__)
#define debug_info(...) log_write(LOG_INFO, __VA_ARGS__)
#define debug_log(...) log_write(LOG_DEBUG, __VA_ARGS__)
#define debug_putchar(char) log_char(char)
#else
#define debug_error(...)
#define debug_warn(...)
#define debug_info(...)
#define debug_log(...)
#define debug_putchar(char)
#endif

#endif /* DEBUG_H */
