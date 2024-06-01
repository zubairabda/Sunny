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

typedef union
{
    struct
    {
        u16 x;
        u16 y;
    };
    u32 vertex;
} vertex_attrib;

#endif