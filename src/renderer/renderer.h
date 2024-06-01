#ifndef RENDERER_H
#define RENDERER_H

#include "common.h"
#include "sy_math.h"
#include "gpu_common.h"
#include "platform/sync.h"

extern signal_event_handle g_present_thread_handle;
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

typedef struct renderer_interface_s renderer_interface;

typedef void (*fp_renderer_flush_commands)(renderer_interface *renderer);
//typedef void (*fp_renderer_end_commands)(renderer_interface *renderer);
typedef void (*fp_renderer_shutdown)(renderer_interface *renderer);
typedef void (*fp_renderer_handle_resize)(renderer_interface *renderer, u32 width, u32 height);
typedef void (*fp_renderer_transfer)(void *data, u32 dst_x, u32 dst_y, u32 width, u32 height);
typedef void (*fp_renderer_read_vram)(void *data, u32 src_x, u32 src_y, u32 width, u32 height);
typedef void (*fp_renderer_copy)(u32 src_x, u32 src_y, u32 dst_x, u32 dst_y, u32 width, u32 height);
typedef void (*fp_renderer_update_display)(renderer_interface *renderer);
typedef void (*fp_renderer_present)(renderer_interface *renderer);

struct render_batch
{
    u32 texture_mode;
    rect2 draw_area;
    u32 vertex_count;
    u32 vertex_array_offset;
    render_vertex *vertex_array;
};

struct renderer_interface_s
{
    //fp_renderer_initialize initialize;
    fp_renderer_flush_commands flush_commands;
    fp_renderer_update_display update_display;
    fp_renderer_present present;
    fp_renderer_shutdown shutdown;
    fp_renderer_handle_resize handle_resize;
    u8 *render_commands;
    u8 *commands_at;
    u32 render_commands_size;
    u32 total_vertex_count;
    render_vertex *vertex_array;
    u32 render_commands_count;
    u32 batch_count;
    u32 dirty_region_count;
    rect2 dirty_regions[128];
    struct render_batch batches[MAX_DRAW_BATCH_COUNT];
#if 0
    fp_renderer_draw_polygon draw_polygon;
    fp_renderer_draw_rect draw_rect;
    fp_renderer_transfer transfer;
    fp_renderer_read_vram read_vram;
    fp_renderer_copy copy;
    fp_renderer_set_scissor set_scissor;
#endif
};

inline vec2 get_texcoord(u32 texcoord)
{
    return v2f((f32)(texcoord & 0xff), (f32)((texcoord >> 8) & 0xff));
}

inline b8 rectangles_intersect(rect2 a, rect2 b)
{
    return (a.right >= b.left && a.bottom >= b.top && a.left <= b.right && b.top <= a.bottom);
}

void *push_render_command(renderer_interface *renderer, enum render_command_type type, u64 size);
void push_draw_area(renderer_interface *renderer, rect2 draw_area);
void push_vram_flush(renderer_interface *renderer);
void push_vram_copy(renderer_interface *renderer, u16 src_x, u16 src_y, u16 dst_x, u16 dst_y, u16 width, u16 height);
void push_cpu_to_vram_copy(renderer_interface *renderer, void **buffer, u16 dst_x, u16 dst_y, u16 width, u16 height);
void push_vram_to_cpu_copy(renderer_interface *renderer, void **dst_buffer, u16 src_x, u16 src_y, u16 width, u16 height);
void push_primitive(renderer_interface *renderer, u32 vertex_count, u32 texture_mode);
void push_polygon(renderer_interface *renderer, u32 *commands, u32 flags, vec2 draw_offset);
void push_rect(renderer_interface *renderer, u32 *commands, u32 flags, u32 texpage);

#endif
