#ifndef SW_RENDERER_H
#define SW_RENDERER_H

#include "renderer.h"

// TODO: temp
extern b8 g_display_updated;

#if defined(SY_PLATFORM_WIN32)
typedef struct win32_software_renderer {
    software_renderer sw;
    HWND window;
    int window_width;
    int window_height;
    HDC vram_dc;
    HBITMAP vram_bitmap;
    void *vram_data;
    HDC fullscreen_dc;
    HBITMAP fullscreen_bitmap;
    //BITMAPINFO window_bitmap_info;
    void *fullscreen_data;
} win32_software_renderer;
#endif

software_renderer *platform_init_software_renderer(platform_window *window);

void draw_polygon(u32 *commands, u32 flags, vec2i draw_offset, rect2 scissor);
void draw_rectangle(u32 *commands, u32 flags, u32 texpage, vec2i draw_offset, rect2 scissor);

//void draw_triangle(vec2i v1, vec2i v2, vec2i v3, u16 color);
//void draw_shaded_triangle(u16 c1, vec2i v1, u16 c2, vec2i v2, u16 c3, vec2i v3);
//void draw_textured_triangle(u16 c1, vec2i v1, u16 c2, vec2i v2, u16 c3, vec2i v3, vec2i texture_page, vec2i palette);

#endif /* SW_RENDERER_H */