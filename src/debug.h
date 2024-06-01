#ifndef DEBUG_H
#define DEBUG_H

#include "common.h"

#include <stdarg.h>
#include <stdio.h>

struct debug_state
{
    FILE *output;
    int last_char;
    u8 *loaded_exe;
    b8 show_disasm;
};

extern struct debug_state g_debug;

inline void log_write(int level, char* msg, ...)
{
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
#define log_init() 
#define debug_log(...) log_write(0, __VA_ARGS__)
#define debug_putchar(char) log_char(char)
#else
#define log_init()
#define debug_log(...)
#define debug_putchar(char)
#endif

#endif
