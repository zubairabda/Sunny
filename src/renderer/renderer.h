#define VRAM_WIDTH 1024
#define VRAM_HEIGHT 512
#define VRAM_SIZE (VRAM_WIDTH * VRAM_HEIGHT * 2)

typedef b8 (*fp_renderer_initialize)(void* window, void* instance, u32 width, u32 height);
typedef void (*fp_renderer_render_frame)(void);
typedef void (*fp_renderer_shutdown)(void);
typedef void (*fp_renderer_handle_resize)(u32 width, u32 height);
typedef void (*fp_renderer_draw_quad)(u32 color, u32 v1, u32 v2, u32 v3, u32 v4);
typedef void (*fp_renderer_draw_textured_quad)(u32 color, u32 v1, u32 t1_palette, u32 v2, u32 t2_page, u32 v3, u32 t3, u32 v4, u32 t4);
typedef void (*fp_renderer_draw_raw_textured_quad)(u32 color, u32 v1, u32 t1_palette, u32 v2, u32 t2_page, u32 v3, u32 t3, u32 v4, u32 t4);
typedef void (*fp_renderer_draw_shaded_quad)(u32 c1, u32 v1, u32 c2, u32 v2, u32 c3, u32 v3, u32 c4, u32 v4);
typedef void (*fp_renderer_draw_shaded_triangle)(u32 c1, u32 v1, u32 c2, u32 v2, u32 c3, u32 v3);
typedef void (*fp_renderer_draw_mono_rect)(u32 c1, u32 v1);
typedef void (*fp_renderer_transfer)(void* data, u32 dst_x, u32 dst_y, u32 width, u32 height);
typedef void (*fp_renderer_read_vram)(void* data, u32 src_x, u32 src_y, u32 width, u32 height);
typedef void (*fp_renderer_copy)(u32 src_x, u32 src_y, u32 dst_x, u32 dst_y, u32 width, u32 height);

typedef struct Renderer
{
    fp_renderer_initialize initialize;
    fp_renderer_render_frame render_frame;
    fp_renderer_shutdown shutdown;
    fp_renderer_handle_resize handle_resize;
    fp_renderer_draw_quad draw_quad;
    fp_renderer_draw_textured_quad draw_textured_quad;
    fp_renderer_draw_raw_textured_quad draw_raw_textured_quad;
    fp_renderer_draw_shaded_quad draw_shaded_quad;
    fp_renderer_draw_shaded_triangle draw_shaded_triangle;
    fp_renderer_draw_mono_rect draw_mono_rect;
    fp_renderer_transfer transfer;
    fp_renderer_read_vram read_vram;
    fp_renderer_copy copy;
} Renderer;

typedef Renderer* (*fp_load_renderer)(void);