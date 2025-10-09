#ifndef SW_RENDERER_H
#define SW_RENDERER_H

#include "renderer.h"
#include "platform/platform.h"

#if defined(SY_PLATFORM_WIN32)
typedef struct
{
    void *data;
    HDC dc;
    HBITMAP handle;
} win32_bitmap;

typedef struct
{
    software_renderer sw;
    HWND window;
    int window_width;
    int window_height;
    win32_bitmap vram_bmp;
    win32_bitmap fullscreen_bmp;
    win32_bitmap vram24_bmp;
} win32_software_renderer;
#endif

software_renderer *platform_init_software_renderer(platform_window *window);

void draw_polygon(u32 *commands, u32 op);
void draw_rectangle(u32 *commands, u32 op);
void update_vram(void);

//void draw_triangle(vec2i v1, vec2i v2, vec2i v3, u16 color);
//void draw_shaded_triangle(u16 c1, vec2i v1, u16 c2, vec2i v2, u16 c3, vec2i v3);
//void draw_textured_triangle(u16 c1, vec2i v1, u16 c2, vec2i v2, u16 c3, vec2i v3, vec2i texture_page, vec2i palette);

#endif /* SW_RENDERER_H */