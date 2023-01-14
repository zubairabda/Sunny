// TODO: include paths
#include "../common.h"
#include "../sy_math.h"
#include "../gpu_common.h"

#define VRAM_WIDTH 1024
#define VRAM_HEIGHT 512
#define VRAM_SIZE (VRAM_WIDTH * VRAM_HEIGHT * 2)

typedef b8 (*fp_renderer_initialize)(void* window, void* instance, u32 width, u32 height);
typedef void (*fp_renderer_render_frame)(void);
typedef void (*fp_renderer_shutdown)(void);
typedef void (*fp_renderer_handle_resize)(u32 width, u32 height);
typedef void (*fp_renderer_transfer)(void* data, u32 dst_x, u32 dst_y, u32 width, u32 height);
typedef void (*fp_renderer_read_vram)(void* data, u32 src_x, u32 src_y, u32 width, u32 height);
typedef void (*fp_renderer_copy)(u32 src_x, u32 src_y, u32 dst_x, u32 dst_y, u32 width, u32 height);
typedef void (*fp_renderer_draw_polygon)(const u32* commands, u32 flags, vec2 draw_offset);
typedef void (*fp_renderer_draw_rect)(const u32* commands, u32 flags, u32 texpage);
typedef void (*fp_renderer_set_scissor)(s16 x1, s16 y1, s16 x2, s16 y2);

typedef struct Renderer
{
    fp_renderer_initialize initialize;
    fp_renderer_render_frame render_frame;
    fp_renderer_shutdown shutdown;
    fp_renderer_handle_resize handle_resize;
    fp_renderer_draw_polygon draw_polygon;
    fp_renderer_draw_rect draw_rect;
    fp_renderer_transfer transfer;
    fp_renderer_read_vram read_vram;
    fp_renderer_copy copy;
    fp_renderer_set_scissor set_scissor;
} Renderer;

typedef Renderer* (*fp_load_renderer)(void);