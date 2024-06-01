#include "renderer.h"
#include "gpu.h"

inline s32 edge(vec2i a, vec2i b, vec2i p)
{
    // (px - v0x) * (v1y - v0y) - (py - v0y) * (v1x - v0x)
    return ((p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x));//v1.x * v2.y - v1.y * v2.x;
}

static void draw_triangle(struct gpu_state* gpu, vec2i v1, vec2i v2, vec2i v3, u16 color)
{
    // TODO: clipping
    u32 minY = min3(v1.y, v2.y, v3.y);
    u32 minX = min3(v1.x, v2.x, v3.x);
    u32 maxY = max3(v1.y, v2.y, v3.y);
    u32 maxX = max3(v1.x, v2.x, v3.x);

    f32 area = (f32)edge(v1, v2, v3);
    if (area < 0)
    {
        vec2i a = v3;
        v3 = v2;
        v2 = a;
        area = -area;
    }

    for (u32 y = minY; y < maxY; ++y)
    {
        for (u32 x = minX; x < maxX; ++x)
        {
            vec2i p = {.x = x, .y = y};

            s32 w1 = edge(v2, v3, p);
            s32 w2 = edge(v3, v1, p);
            s32 w3 = edge(v1, v2, p);

            if ((w1 | w2 | w3) >= 0)
            {
                ((u16*)gpu->vram)[x + (VRAM_WIDTH * y)] = color;
            }
        }
    }
}

static void draw_shaded_triangle(struct gpu_state* gpu, u16 c1, vec2i v1, u16 c2, vec2i v2, u16 c3, vec2i v3)
{
    u32 minY = min3(v1.y, v2.y, v3.y);
    u32 minX = min3(v1.x, v2.x, v3.x);
    u32 maxY = max3(v1.y, v2.y, v3.y);
    u32 maxX = max3(v1.x, v2.x, v3.x);

    f32 area = (f32)edge(v1, v2, v3);
    if (area < 0)
    {
        vec2i a = v3;
        u16 c = c3;
        v3 = v2;
        c3 = c2;
        v2 = a;
        c2 = c;
        area = -area;
    }

    for (u32 y = minY; y < maxY; ++y)
    {
        for (u32 x = minX; x < maxX; ++x)
        {
            vec2i p = {.x = x, .y = y};
            s32 e1 = edge(v2, v3, p);
            s32 e2 = edge(v3, v1, p);
            s32 e3 = edge(v1, v2, p);
            if ((e1 | e2 | e3) >= 0)
            {
                f32 w1 = e1 / area;
                f32 w2 = e2 / area;
                f32 w3 = e3 / area;

                f32 r = w1 * ((c1 >> 10) & 0x1f) + w2 * ((c2 >> 10) & 0x1f) + w3 * ((c3 >> 10) & 0x1f);
                f32 g = w1 * ((c1 >> 5) & 0x1f) + w2 * ((c2 >> 5) & 0x1f) + w3 * ((c3 >> 5) & 0x1f);
                f32 b = w1 * (c1 & 0x1f) + w2 * (c2 & 0x1f) + w3 * (c3 & 0x1f);

                u16 color = ((u16)r << 10) | ((u16)g << 5) | ((u16)b);

                ((u16*)gpu->vram)[x + (VRAM_WIDTH * y)] = color;
            }
        }
    }
}

static void draw_textured_triangle(u8* vram, u16 c1, vec2i v1, u16 c2, vec2i v2, u16 c3, vec2i v3, vec2i texture_page, vec2i palette)
{
    u32 minY = min3(v1.y, v2.y, v3.y);
    u32 minX = min3(v1.x, v2.x, v3.x);
    u32 maxY = max3(v1.y, v2.y, v3.y);
    u32 maxX = max3(v1.x, v2.x, v3.x);

    f32 area = (f32)edge(v1, v2, v3);
    if (area < 0)
    {
        vec2i a = v3;
        u16 c = c3;
        v3 = v2;
        c3 = c2;
        v2 = a;
        c2 = c;
        area = -area;
    }

    for (u32 y = minY; y < maxY; ++y)
    {
        for (u32 x = minX; x < maxX; ++x)
        {
            vec2i p = {.x = x, .y = y};
            s32 e1 = edge(v2, v3, p);
            s32 e2 = edge(v3, v1, p);
            s32 e3 = edge(v1, v2, p);
            if ((e1 | e2 | e3) >= 0)
            {
                f32 w1 = e1 / area;
                f32 w2 = e2 / area;
                f32 w3 = e3 / area;

                f32 r = w1 * ((c1 >> 10) & 0x1f) + w2 * ((c2 >> 10) & 0x1f) + w3 * ((c3 >> 10) & 0x1f);
                f32 g = w1 * ((c1 >> 5) & 0x1f) + w2 * ((c2 >> 5) & 0x1f) + w3 * ((c3 >> 5) & 0x1f);
                f32 b = w1 * (c1 & 0x1f) + w2 * (c2 & 0x1f) + w3 * (c3 & 0x1f);

                //f32 tx = (w1 * )

                u16 color = ((u16)r << 10) | ((u16)g << 5) | ((u16)b);

                ((u16*)vram)[x + (VRAM_WIDTH * y)] = color;
            }
        }
    }
}
