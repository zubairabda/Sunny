#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

typedef uint8_t     u8;
typedef uint16_t    u16;
typedef uint32_t    u32;
typedef uint64_t    u64;
typedef int8_t      s8;
typedef int16_t     s16;
typedef int32_t     s32;
typedef int64_t     s64;
typedef float       f32;
typedef double      f64;
typedef _Bool       b8;
typedef s16         b16;
typedef s32         b32;

#if SY_DEBUG
#define SY_ASSERT(expr) do { if (!(expr)) { printf("[ASSERT]: expression: '%s' at %s:%d, did not evaluate\n", #expr, __FILE__, __LINE__); volatile int *p = 0; *p = 0; } } while(0)
#define INVALID_CASE default: SY_ASSERT(0); break
#else
#define SY_ASSERT(expr)
#define INVALID_CASE
#endif
#define ARRAYCOUNT(arr) (sizeof(arr) / sizeof(arr[0]))

#define MEMBERSIZE(type, element) sizeof(((type*)0)->element)

#define NoImplementation

#define KILOBYTES(val) ((val) * 1024u)
#define MEGABYTES(val) (KILOBYTES(val) * 1024u)

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

inline s32 clamp16(s32 val)
{
    if (val > 32767) {
        val = 32767;
    }
    else if (val < -32768) {
        val = -32768;
    }
    return val;
}

inline s32 clamp(s32 v, s32 min, s32 max)
{
    return v < min ? min : v > max ? max : v;
}

inline u32 safe_truncate32(u64 v)
{
    SY_ASSERT(v <= 0xffffffff);
    return (u32)v;
}

#define U32FromPtr(ptr) (*(u32 *)(ptr))
#define U16FromPtr(ptr) (*(u16 *)(ptr))
#define U8FromPtr(ptr) (*(u8 *)(ptr))

#endif /* COMMON_H */
