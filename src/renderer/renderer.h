#ifndef RENDERER_H
#define RENDERER_H

#include "common.h"
#include "sy_math.h"
#include "gpu_common.h"
#include "platform/platform.h"

extern platform_event g_present_ready;
extern platform_mutex g_renderer_mutex;
extern u32 g_vblank_counter;

#define VRAM_WIDTH 1024
#define VRAM_HEIGHT 512
#define VRAM_SIZE (VRAM_WIDTH * VRAM_HEIGHT * 2)

#define MAX_DRAW_BATCH_COUNT 32

typedef struct render_vertex
{
    u32 pos;
    vec2 uv;
    vec2i texture_page;
    vec2i clut;
    u32 color;
} render_vertex;

enum render_command_type
{
    RENDER_COMMAND_DRAW_PRIMITIVES,
    RENDER_COMMAND_DRAW_SHADED_PRIMITIVE,
    RENDER_COMMAND_DRAW_TEXTURED_PRIMITIVE,
    RENDER_COMMAND_SET_DRAW_AREA,
    RENDER_COMMAND_FLUSH_VRAM,
    RENDER_COMMAND_TRANSFER_CPU_TO_VRAM,
    RENDER_COMMAND_TRANSFER_VRAM_TO_CPU,
    RENDER_COMMAND_TRANSFER_VRAM_TO_VRAM
};

struct render_command_header
{
    enum render_command_type type;
};

struct render_command_draw
{
    struct render_command_header header;
    u32 vertex_array_offset;
    u32 vertex_count;
    u32 texture_mode;
};

struct render_command_set_draw_area
{
    struct render_command_header header;
    rect2 draw_area;
};

struct render_command_flush_vram
{
    struct render_command_header header;   
};

struct render_command_transfer
{
    struct render_command_header header;
    u16 x;
    u16 y;
    u16 x2;
    u16 y2;
    u16 width;
    u16 height;
    void **buffer;
};

typedef struct renderer_context
{
    b8 is_initialized;
    b8 is_threaded_present;
#if 0
    u8 *render_commands;
    u8 *commands_at;
    u32 render_commands_size;
    u32 total_vertex_count;
    render_vertex *vertex_array;
    u32 render_commands_count;
#endif
    //void (*flush_commands)(void);
    void (*handle_resize)(u32 new_width, u32 new_height);
    void (*update_display)(void);
    void (*present)(void);
    void (*shutdown)(void);
} renderer_context;

typedef struct hardware_renderer
{
    renderer_context base;
    void (*flush_commands)(void);
    void *(*get_vram_ptr)(void);

    u8 *render_commands;
    u8 *commands_at;
    u32 render_commands_size;
    u32 total_vertex_count;
    render_vertex *vertex_array;
    u32 render_commands_count;
} hardware_renderer;

typedef struct software_renderer
{
    renderer_context base;
    void *vram;
} software_renderer;

extern renderer_context *g_renderer;

inline vec2 get_texcoord(u32 texcoord)
{
    return v2f((f32)(texcoord & 0xff), (f32)((texcoord >> 8) & 0xff));
}

inline u16 swizzle_texel(u16 pixel)
{
    u16 red = (pixel & 0x1f);
    u16 green = ((pixel >> 5) & 0x1f);
    u16 blue = ((pixel >> 10) & 0x1f);
    u16 mask = pixel & 0x8000;
    return mask | (red << 10) | (green << 5) | blue;
}

inline b8 rectangles_intersect(rect2 a, rect2 b)
{
    return (a.right >= b.left && a.bottom >= b.top && a.left <= b.right && b.top <= a.bottom);
}

/* Retained API functions */
void *push_render_command(enum render_command_type type, u64 size);
void push_draw_area(rect2 draw_area);
void push_vram_flush(void);
void push_vram_copy(u16 src_x, u16 src_y, u16 dst_x, u16 dst_y, u16 width, u16 height);
void push_cpu_to_vram_copy(void **src_buffer, u16 dst_x, u16 dst_y, u16 width, u16 height);
void push_vram_to_cpu_copy(void **dst_buffer, u16 src_x, u16 src_y, u16 width, u16 height);
void push_primitive(u32 vertex_count, u32 texture_mode);
void push_polygon(u32 *commands, u32 flags, vec2 draw_offset);
void push_rect(u32 *commands, u32 flags, u32 texpage, vec2 draw_offset);

#endif /* RENDERER_H */
