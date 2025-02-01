#ifndef SY_MATH_H
#define SY_MATH_H

typedef struct rect2
{
    s32 left;
    s32 top;
    s32 right;
    s32 bottom;
} rect2;

typedef union vec2i
{
    struct {s32 x, y;};
    s32 data[2];
} vec2i;

typedef union vec2
{
    struct {f32 x, y;};
    f32 data[2];
} vec2;

typedef union vec2u
{
    struct {u32 x, y;};
    u32 data[2];
} vec2u;

typedef union vec3u
{
    struct {u32 x, y, z;};
    u32 data[3];
} vec3u;

typedef union vec4
{
    struct {f32 x, y, z, w;};
    struct {f32 r, g, b, a;};
    f32 data[4];
} vec4;

typedef union vec4u
{
    struct {u32 x, y, z, w;};
    u32 data[4];
} vec4u;

inline vec2 v2sub(vec2 a, vec2 b)
{
    vec2 result = {.x = a.x - b.x, .y = a.y - b.y};
    return result;
}

inline vec2 v2add(vec2 a, vec2 b)
{
    vec2 result = {.x = a.x + b.x, .y = a.y + b.y};
    return result;
}

inline vec2i v2iadd(vec2i a, vec2i b)
{
    vec2i result = {.x = a.x + b.x, .y = a.y + b.y};
    return result;
}

inline vec4 v4f(f32 x, f32 y, f32 z, f32 w)
{
    vec4 result = {.x = x, .y = y, .z = z, .w = w};
    return result;
}

inline vec4u v4u(u32 x, u32 y, u32 z, u32 w)
{
    vec4u result = {.x = x, .y = y, .z = z, .w = w};
    return result;
}

inline vec3u v3u(u32 x, u32 y, u32 z)
{
    vec3u result = {.x = x, .y = y, .z = z};
    return result;
}

inline vec2 v2f(f32 x, f32 y)
{
    vec2 result = {.x = x, .y = y};
    return result;
}

inline vec2u v2u(u32 x, u32 y)
{
    vec2u result = {.x = x, .y = y};
    return result;
}

inline vec2i v2i(s32 x, s32 y)
{
    vec2i result = {.x = x, .y = y};
    return result;
}

inline vec2i v2ifromu32(u32 v)
{
    vec2i result = {.x = (s16)(v & 0xffff), .y = (s16)(v >> 16)};
    return result;
}

inline int max3(int a, int b, int c)
{
    int x = b > c ? b : c;
    return a > x ? a : x;
}

inline int min3(int a, int b, int c)
{
    int x = b < c ? b : c;
    return a < x ? a : x;
}

#endif /* SY_MATH_H */