#ifndef GPU_COMMON_H
#define GPU_COMMON_H

#include "common.h"

enum polygon_render_flags
{
    POLYGON_FLAG_GOURAUD_SHADED = 0x10,
    POLYGON_FLAG_IS_QUAD = 0x8,
    POLYGON_FLAG_TEXTURED = 0x4,
    POLYGON_FLAG_SEMI_TRANSPARENT = 0x2,
    POLYGON_FLAG_RAW_TEXTURE = 0x1
};

enum rectangle_render_flags
{
    RECT_FLAG_TEXTURED = 0x4,
    RECT_FLAG_SEMI_TRANSPARENT = 0x2,
    RECT_FLAG_RAW_TEXTURE = 0x1
};

enum line_render_flags
{
    LINE_FLAG_SEMI_TRANSPARENT = 0x2,
    LINE_FLAG_POLYLINE = 0x8,
    LINE_FLAG_GOURAUD_SHADED = 0x10
};

enum texture_mode
{
    TEXTURE_MODE_DIRECT = 0,
    TEXTURE_MODE_4BPP = 1,
    TEXTURE_MODE_8BPP = 2
};

#define VERTEX_X(v) ((((v) & 0x7ff) << 21) >> 21)
#define VERTEX_Y(v) ((((v) & 0x7ff0000) << 5) >> 21)

inline u16 color16from24(u32 color)
{
    u16 red = ((color >> 3) & 0x1f);
    u16 green = ((color >> 11) & 0x1f);
    u16 blue = ((color >> 19) & 0x1f);
    return (blue << 10) | (green << 5) | red;
}

inline u16 color16from888(u32 r, u32 g, u32 b)
{
    u16 red = r >> 3;
    u16 green = g >> 3;
    u16 blue = b >> 3;
    return (blue << 10) | (green << 5) | red;
}

#endif /* GPU_COMMON_H */