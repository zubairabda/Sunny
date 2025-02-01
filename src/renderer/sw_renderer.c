#include "sw_renderer.h"
#include "gpu.h"
#include "debug/debug_ui.h"
#include "debug/atlas.h"

b8 g_display_updated;

static void renderer_stub_function(void) {}

static inline u32 swizzle_16_32(u16 pixel)
{
    u32 r = pixel & 0x1f;
    u32 g = pixel & 0x3e0;
    u32 b = pixel & 0x7c00;
    //u32 mask = pixel & 0x8000;
    //return (mask << 16) | (b >> 7) | (g << 6) | (r << 19);
    return (r << 19) | (g << 6) | (b >> 7);
}

static void handle_resize(u32 width, u32 height) 
{
#if defined(SY_PLATFORM_WIN32)
    win32_software_renderer *renderer = (win32_software_renderer *)g_renderer;
    BITMAPINFO fullscreen = {0};
    fullscreen.bmiHeader.biSize = sizeof(fullscreen.bmiHeader);
    fullscreen.bmiHeader.biWidth = width;
    fullscreen.bmiHeader.biHeight = -height;
    fullscreen.bmiHeader.biPlanes = 1;
    fullscreen.bmiHeader.biBitCount = 32;
    fullscreen.bmiHeader.biCompression = BI_RGB;

    DeleteObject(renderer->fullscreen_bitmap);
    renderer->fullscreen_bitmap = CreateDIBSection(renderer->fullscreen_dc, &fullscreen, DIB_RGB_COLORS, (void **)&renderer->fullscreen_data, 0, 0);
    SelectObject(renderer->fullscreen_dc, renderer->fullscreen_bitmap);

    renderer->window_width = width;
    renderer->window_height = height;
#endif
}

static void update_display(void)
{
#if defined(SY_PLATFORM_WIN32)
    win32_software_renderer *renderer = (win32_software_renderer *)g_renderer;

    u16 *vram = renderer->sw.vram;
    u32 *vram_texture = renderer->vram_data;
#if 1
    int width = renderer->window_width;
    int height = renderer->window_height;

    for (int i = 0; i < (VRAM_SIZE >> 1); ++i)
    {
        vram_texture[i] = swizzle_16_32(vram[i]);
    }
    
#if 0
    int src_x = g_gpu.vram_display_x;
    int src_y = g_gpu.vram_display_y;
    int src_w = (g_gpu.horizontal_display_x2 - g_gpu.horizontal_display_x1) / g_gpu.dot_div;
    int src_h = g_gpu.vertical_display_y2 - g_gpu.vertical_display_y1;
    if ((g_gpu.stat.value >> 22) & 0x1)
    {
        src_h <<= 1;
    }
    StretchBlt(renderer->fullscreen_dc, 0, 0, width, height, renderer->vram_dc, src_x, src_y, src_w, src_h, SRCCOPY);
#else
    StretchBlt(renderer->fullscreen_dc, 0, 0, width, height, renderer->vram_dc, 0, 0, VRAM_WIDTH, VRAM_HEIGHT, SRCCOPY);
#endif
    // update ui
    debug_ui_reset_command_ptr();
    u32 *pixels = renderer->fullscreen_data;

    rect2 clip_rect = {0, 0, width, height};
    //int count = 0;
    struct debug_ui_command_header *cmd = NULL;
    while ((cmd = debug_ui_next_command()) != NULL)
    {
        //++count;
        switch (cmd->type)
        {
        case DEBUG_UI_COMMAND_SET_CLIP:
        {
            struct debug_ui_command_set_clip *clip = (struct debug_ui_command_set_clip *)cmd;
            clip_rect = clip->r;
#if 0
            vec2i p0 = {.x = clip_rect.left, .y = clip_rect.top};
            vec2i p1 = {.x = clip_rect.left, .y = clip_rect.bottom-1};
            vec2i p2 = {.x = clip_rect.right-1, .y = clip_rect.top};
            vec2i p3 = {.x = clip_rect.right-1, .y = clip_rect.bottom-1};
            pixels[p0.x + (width * p0.y)] = 0x00ff0000;
            pixels[p1.x + (width * p1.y)] = 0x00ff0000;
            pixels[p2.x + (width * p2.y)] = 0x00ff0000;
            pixels[p3.x + (width * p3.y)] = 0x00ff0000;
#endif
            break;
        }
        case DEBUG_UI_COMMAND_QUAD:
        {
            struct debug_ui_command_quad *quad = (struct debug_ui_command_quad *)cmd;
            int x1 = quad->r.left;
            int y1 = quad->r.top;
            int x2 = quad->r.right;
            int y2 = quad->r.bottom;

            if (x1 < clip_rect.left)
                x1 = clip_rect.left;
            if (y1 < clip_rect.top)
                y1 = clip_rect.top;
            if (x2 > clip_rect.right)
                x2 = clip_rect.right;
            if (y2 > clip_rect.bottom)
                y2 = clip_rect.bottom;

            for (int y = y1; y < y2; ++y)
            {
                for (int x = x1; x < x2; ++x)
                {
                    u32 color = quad->color;
                    u32 alpha = (color & 0xff000000) >> 24;

                    pixels[x + (width * y)] = color;
                }
            }
            break;
        }
        case DEBUG_UI_COMMAND_TEXT:
        {
            struct debug_ui_command_text *text = (struct debug_ui_command_text *)cmd;
            vec2i pen = text->pos;
            char *str = (char *)(text + 1);
            while (*str)
            {
                int c = *str++;
                if (c > 126) {
                    c = '?';
                }
                else if (c == ' ') {
                    pen.x += 6; // TODO: font metrics
                    continue;
                }

                atlas_glyph glyph = glyphs[c];
                // skip characters that are not on screen (assumes left to right chars)
                if (pen.x + glyph.left > clip_rect.right || pen.y - glyph.top > clip_rect.bottom) {
                    pen.x += glyph.advance;
                    continue;
                }
                
                int y1 = pen.y - glyph.top;
                int y2 = pen.y + glyph.h - glyph.top;

                int x1 = pen.x + glyph.left;
                int x2 = pen.x + glyph.left + glyph.w;

                if (x1 < clip_rect.left)
                    x1 = clip_rect.left;
                if (y1 < clip_rect.top)
                    y1 = clip_rect.top;
                if (x2 > clip_rect.right)
                    x2 = clip_rect.right;
                if (y2 > clip_rect.bottom)
                    y2 = clip_rect.bottom;

                for (int y = glyph.y, dst_y = y1; dst_y < y2; ++y, ++dst_y)
                {
                    for (int x = glyph.x, dst_x = x1; dst_x < x2; ++x, ++dst_x)
                    {
                        u32 alpha = atlas[x + (ATLAS_WIDTH * y)];

                        u32 dst = dst_x + (width * dst_y);
                        u32 c_dst = pixels[dst];

                        u8 dst_b = c_dst & 0xff;
                        u8 dst_g = (c_dst >> 8) & 0xff;
                        u8 dst_r = (c_dst >> 16) & 0xff;

                        f32 factor = 1.0f - (alpha / 255.0f);
                        u32 c_b = alpha + dst_b * factor;
                        u32 c_g = alpha + dst_g * factor;
                        u32 c_r = alpha + dst_r * factor;

                        pixels[dst] = c_b | (c_g << 8) | (c_r << 16);
                    }
                }
                pen.x += glyph.advance;
            }
            break;
        }
        default:
            break;
        }
    }
        //printf("%d\n", count);
    //}

    HDC window_dc = GetDC(renderer->window);
#if 0
    if (width == VRAM_WIDTH && height == VRAM_HEIGHT)
    {
        for (int i = 0; i < (VRAM_SIZE >> 1); ++i)
        {
            vram_texture[i] = swizzle_16_32(vram[i]);
        }
    }
    else
    {
        // Nearest-neighbor interpolation
        float f_x = VRAM_WIDTH / (float)width;
        float f_y = VRAM_HEIGHT / (float)height;

        int y_pos, x_pos;

        for (int y = 0; y < height; ++y)
        {
            y_pos = (int)(f_y * y);

            int dst_row = width * y;
            int src_row = VRAM_WIDTH * y_pos;

            for (int x = 0; x < width; ++x)
            {
                x_pos = (int)(f_x * x);

                vram_texture[x + dst_row] = swizzle_16_32(vram[x_pos + src_row]);
            }
        }
    }

    // blit the ui texture
    u32 *pixels = (u32 *)renderer->fullscreen_data;
    for (int y = 0; y < height; ++y)
    {
        int dst_row = width * y;

        for (int x = 0; x < width; ++x)
        {
            u32 pixel = pixels[x + dst_row];
            if (pixel)
            {
                vram_texture[x + dst_row] = pixel;
            }
        }
    }

    BitBlt(window_dc, 0, 0, width, height, renderer->vram_dc, 0, 0, SRCCOPY);
#endif

    BitBlt(window_dc, 0, 0, width, height, renderer->fullscreen_dc, 0, 0, SRCCOPY);

    ReleaseDC(renderer->window, window_dc);

    g_display_updated = true;
#else
    InvalidateRect(renderer->window, NULL, FALSE);
    UpdateWindow(renderer->window);
#endif
#endif
}

software_renderer *platform_init_software_renderer(platform_window *window)
{
#if defined(SY_PLATFORM_WIN32)
    win32_software_renderer *result = malloc(sizeof(win32_software_renderer));
    if (!result) {
        return NULL;
    }
    memset(result, 0, sizeof(win32_software_renderer));

    result->vram_dc = CreateCompatibleDC(0);
#if 1
    BITMAPINFO bitmap_info = {0};
    bitmap_info.bmiHeader.biSize = sizeof(bitmap_info.bmiHeader);
    bitmap_info.bmiHeader.biWidth = VRAM_WIDTH;
    bitmap_info.bmiHeader.biHeight = -VRAM_HEIGHT; // negative height so we can use 0,0 as top-left
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;
#endif
    void *vram = malloc(VRAM_SIZE);
    if (!vram) {
        free(result);
        return NULL;
    }

    result->vram_bitmap = CreateDIBSection(NULL, &bitmap_info, DIB_RGB_COLORS, (void **)&result->vram_data, 0, 0);

    SelectObject(result->vram_dc, result->vram_bitmap);

    result->sw.vram = vram;

    result->window = window->handle;

    result->fullscreen_dc = CreateCompatibleDC(0);
    SetStretchBltMode(result->fullscreen_dc, COLORONCOLOR);

    RECT client;
    GetClientRect(window->handle, &client);

    int width = client.right - client.left;
    int height = client.bottom - client.top;

    BITMAPINFO fullscreen = {0};
    fullscreen.bmiHeader.biSize = sizeof(fullscreen.bmiHeader);
    fullscreen.bmiHeader.biWidth = width;
    fullscreen.bmiHeader.biHeight = -height;
    fullscreen.bmiHeader.biPlanes = 1;
    fullscreen.bmiHeader.biBitCount = 32;
    fullscreen.bmiHeader.biCompression = BI_RGB;

    result->fullscreen_bitmap = CreateDIBSection(NULL, &fullscreen, DIB_RGB_COLORS, (void **)&result->fullscreen_data, 0, 0);

    SelectObject(result->fullscreen_dc, result->fullscreen_bitmap);

    result->window_width = width;
    result->window_height = height;

    result->sw.base.is_initialized = true;
    //result->base.flush_commands = renderer_stub_function;
    result->sw.base.present = renderer_stub_function;
    result->sw.base.handle_resize = handle_resize;
    result->sw.base.update_display = update_display;

    return (software_renderer *)result;
#endif
}

static inline vec2i vertex_position(s32 v)
{
    vec2i result;
    result.x = ((v & 0x7ff) << 21) >> 21;
    result.y = ((v & 0x7ff0000) << 5) >> 21;
    return result;
}

static inline u16 color16from24(u32 color)
{
    u16 red = ((color >> 3) & 0x1f);
    u16 green = ((color >> 11) & 0x1f);
    u16 blue = ((color >> 19) & 0x1f);
    return (blue << 10) | (green << 5) | red;
}

static inline s32 edge(vec2i a, vec2i b, vec2i p)
{
    // (px - v0x) * (v1y - v0y) - (py - v0y) * (v1x - v0x)
    return ((p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x));//v1.x * v2.y - v1.y * v2.x;
}

void draw_triangle(vec2i v1, vec2i v2, vec2i v3, u16 color, rect2 scissor)
{
    software_renderer *renderer = (software_renderer *)g_renderer;
    u16 *vram = renderer->vram;
    
    s32 minY = min3(v1.y, v2.y, v3.y);
    s32 minX = min3(v1.x, v2.x, v3.x);
    s32 maxY = max3(v1.y, v2.y, v3.y);
    s32 maxX = max3(v1.x, v2.x, v3.x);

    f32 area = (f32)edge(v1, v2, v3);
    if (area < 0)
    {
        vec2i a = v3;
        v3 = v2;
        v2 = a;
        area = -area;
    }

    if (minX < scissor.left)
        minX = scissor.left;
    if (minY < scissor.top)
        minY = scissor.top;
    if (maxX > scissor.right)
        maxX = scissor.right;
    if (maxY > scissor.bottom)
        maxY = scissor.bottom;

    for (s32 y = minY; y <= maxY; ++y)
    {
        for (s32 x = minX; x <= maxX; ++x)
        {
            vec2i p = {.x = x, .y = y};

            s32 w1 = edge(v2, v3, p);
            s32 w2 = edge(v3, v1, p);
            s32 w3 = edge(v1, v2, p);

            if ((w1 | w2 | w3) >= 0)
            {
                vram[x + (VRAM_WIDTH * y)] = color;
            }
        }
    }
}

// TODO: pass color as vec4
void draw_shaded_triangle(u16 c1, vec2i v1, u16 c2, vec2i v2, u16 c3, vec2i v3, rect2 scissor)
{
    software_renderer *renderer = (software_renderer *)g_renderer;
    u16 *vram = renderer->vram;

    s32 minY = min3(v1.y, v2.y, v3.y);
    s32 minX = min3(v1.x, v2.x, v3.x);
    s32 maxY = max3(v1.y, v2.y, v3.y);
    s32 maxX = max3(v1.x, v2.x, v3.x);

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

    if (minX < scissor.left)
        minX = scissor.left;
    if (minY < scissor.top)
        minY = scissor.top;
    if (maxX > scissor.right)
        maxX = scissor.right;
    if (maxY > scissor.bottom)
        maxY = scissor.bottom;

    f32 c1r = (f32)(c1 & 0x1f);
    f32 c1g = (f32)((c1 >> 5) & 0x1f);
    f32 c1b = (f32)((c1 >> 10) & 0x1f);

    f32 c2r = (f32)(c2 & 0x1f);
    f32 c2g = (f32)((c2 >> 5) & 0x1f);
    f32 c2b = (f32)((c2 >> 10) & 0x1f);

    f32 c3r = (f32)(c3 & 0x1f);
    f32 c3g = (f32)((c3 >> 5) & 0x1f);
    f32 c3b = (f32)((c3 >> 10) & 0x1f);

    for (s32 y = minY; y <= maxY; ++y)
    {
        for (s32 x = minX; x <= maxX; ++x)
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

                f32 r = w1 * c1r + w2 * c2r + w3 * c3r;
                f32 g = w1 * c1g + w2 * c2g + w3 * c3g;
                f32 b = w1 * c1b + w2 * c2b + w3 * c3b;

                //u16 color = ((u16)r << 10) | ((u16)g << 5) | ((u16)b);
                u16 color = ((u16)r) | ((u16)g << 5) | ((u16)b << 10);

                vram[x + (VRAM_WIDTH * y)] = color;
            }
        }
    }
}

void draw_shaded_textured_triangle(u32 c1, vec2i t1, vec2i v1, u32 c2, vec2i t2, vec2i v2, u32 c3, vec2i t3, vec2i v3, 
                                u32 mode, vec2i texture_page, vec2i palette, rect2 scissor)
{
    software_renderer *renderer = (software_renderer *)g_renderer;
    u16 *vram = renderer->vram;

    s32 area = edge(v1, v2, v3);
    if (!area) {
        return;
    }

    if (area < 0)
    {
        vec2i a = v3;
        vec2i b = t3;
        u16 c = c3;
        v3 = v2;
        t3 = t2;
        c3 = c2;
        v2 = a;
        t2 = b;
        c2 = c;
        area = -area;
    }

    s32 minY = min3(v1.y, v2.y, v3.y);
    s32 minX = min3(v1.x, v2.x, v3.x);
    s32 maxY = max3(v1.y, v2.y, v3.y);
    s32 maxX = max3(v1.x, v2.x, v3.x);

    if (minX < scissor.left)
        minX = scissor.left;
    if (minY < scissor.top)
        minY = scissor.top;
    if (maxX > scissor.right)
        maxX = scissor.right;
    if (maxY > scissor.bottom)
        maxY = scissor.bottom;

    f32 c1b = (f32)((c1 >> 16) & 0xff);
    f32 c1g = (f32)((c1 >> 8) & 0xff);
    f32 c1r = (f32)(c1 & 0xff);

    f32 c2b = (f32)((c2 >> 16) & 0xff);
    f32 c2g = (f32)((c2 >> 8) & 0xff);
    f32 c2r = (f32)(c2 & 0xff);

    f32 c3b = (f32)((c3 >> 16) & 0xff);
    f32 c3g = (f32)((c3 >> 8) & 0xff);
    f32 c3r = (f32)(c3 & 0xff);

    vec2i p;

    for (s32 y = minY; y <= maxY; ++y)
    {
        for (s32 x = minX; x <= maxX; ++x)
        {
            p.x = x;
            p.y = y;
            
            s32 e1 = edge(v2, v3, p);
            s32 e2 = edge(v3, v1, p);
            s32 e3 = edge(v1, v2, p);

            if ((e1 | e2 | e3) >= 0)
            {
                f32 w1 = e1 / (f32)area;
                f32 w2 = e2 / (f32)area;
                f32 w3 = e3 / (f32)area;

                f32 texcoord_x = w1 * t1.x + w2 * t2.x + w3 * t3.x;
                f32 texcoord_y = w1 * t1.y + w2 * t2.y + w3 * t3.y;

                f32 r = w1 * c1r + w2 * c2r + w3 * c3r;
                f32 g = w1 * c1g + w2 * c2g + w3 * c3g;
                f32 b = w1 * c1b + w2 * c2b + w3 * c3b;
#if 1
                u16 mask = 0;
                switch (mode)
                {
                case 0:

                    break;
                case 1:
                {
                    s32 sample_x = ((s32)texcoord_x >> 2) + texture_page.x;
                    s32 sample_y = (s32)texcoord_y + texture_page.y;

                    s32 shift = ((s32)texcoord_x & 0x3) << 2;

                    u16 clut = vram[sample_x + (VRAM_WIDTH * sample_y)];

                    u16 index = (clut >> shift) & 0xf;

                    u16 result = vram[(palette.x + index) + (VRAM_WIDTH) * palette.y];

                    r = (r * (result & 0x1f)) / 128.0f;
                    g = (g * ((result >> 5) & 0x1f)) / 128.0f;
                    b = (b * ((result >> 10) & 0x1f)) / 128.0f;

                    mask = result & 0x8000;
                    break;
                }
                case 2:
                {
                    s32 sample_x = ((s32)texcoord_x >> 1) + texture_page.x;
                    s32 sample_y = (s32)texcoord_y + texture_page.y;

                    s32 shift = ((s32)texcoord_x & 0x1) << 3;

                    u16 clut = vram[sample_x + (VRAM_WIDTH * sample_y)];

                    u16 index = (clut >> shift) & 0xff;

                    u16 result = vram[(palette.x + index) + (VRAM_WIDTH) * palette.y];

                    r = (r * (result & 0x1f)) / 128.0f;
                    g = (g * ((result >> 5) & 0x1f)) / 128.0f;
                    b = (b * ((result >> 10) & 0x1f)) / 128.0f;

                    mask = result & 0x8000;
                    break;
                }
                }
#endif
                //u16 color = ((u16)r << 10) | ((u16)g << 5) | ((u16)b);
                u16 color = ((u16)r) | ((u16)g << 5) | ((u16)b << 10) | mask;

                if (!color)
                    continue;

                vram[x + (VRAM_WIDTH * y)] = color;
            }
        }
    }
}

void draw_textured_triangle(u32 color, vec2i t1, vec2i v1, vec2i t2, vec2i v2, vec2i t3, vec2i v3, 
                                u32 mode, vec2i texture_page, vec2i palette, rect2 scissor)
{
    software_renderer *renderer = (software_renderer *)g_renderer;
    u16 *vram = renderer->vram;

    s32 area = edge(v1, v2, v3);
    if (!area) {
        return;
    }

    if (area < 0)
    {
        vec2i a = v3;
        vec2i b = t3;
        v3 = v2;
        t3 = t2;
        v2 = a;
        t2 = b;
        area = -area;
    }

    s32 minY = min3(v1.y, v2.y, v3.y);
    s32 minX = min3(v1.x, v2.x, v3.x);
    s32 maxY = max3(v1.y, v2.y, v3.y);
    s32 maxX = max3(v1.x, v2.x, v3.x);

    if (minX < scissor.left)
        minX = scissor.left;
    if (minY < scissor.top)
        minY = scissor.top;
    if (maxX > scissor.right)
        maxX = scissor.right;
    if (maxY > scissor.bottom)
        maxY = scissor.bottom;

    f32 cr = (f32)(color & 0xff);
    f32 cg = (f32)((color >> 8) & 0xff);
    f32 cb = (f32)((color >> 16) & 0xff);

    u16 mask = 0;

    for (s32 y = minY; y <= maxY; ++y)
    {
        for (s32 x = minX; x <= maxX; ++x)
        {
            vec2i p = {.x = x, .y = y};
            s32 e1 = edge(v2, v3, p);
            s32 e2 = edge(v3, v1, p);
            s32 e3 = edge(v1, v2, p);
            if ((e1 | e2 | e3) >= 0)
            {
                f32 w1 = e1 / (f32)area;
                f32 w2 = e2 / (f32)area;
                f32 w3 = e3 / (f32)area;

                f32 texcoord_x = w1 * t1.x + w2 * t2.x + w3 * t3.x;
                f32 texcoord_y = w1 * t1.y + w2 * t2.y + w3 * t3.y;

                f32 r = 0.0f;
                f32 g = 0.0f;
                f32 b = 0.0f;

                switch (mode)
                {
                case 0:

                    break;
                case 1:
                {
                    s32 sample_x = ((s32)texcoord_x >> 2) + texture_page.x;
                    s32 sample_y = (s32)texcoord_y + texture_page.y;

                    s32 shift = ((s32)texcoord_x & 0x3) << 2;

                    u16 value = vram[sample_x + (VRAM_WIDTH * sample_y)];

                    u16 index = (value >> shift) & 0xf;

                    u16 result = vram[(palette.x + index) + (VRAM_WIDTH) * palette.y];

                    r = (cr * (result & 0x1f)) / 128.0f;
                    g = (cg * ((result >> 5) & 0x1f)) / 128.0f;
                    b = (cb * ((result >> 10) & 0x1f)) / 128.0f;

                    mask = result & 0x8000;
                    break;
                }
                case 2:

                    break;
                }

                //u16 color = ((u16)r << 10) | ((u16)g << 5) | ((u16)b);
                u16 color = ((u16)r) | ((u16)g << 5) | ((u16)b << 10) | mask;
#if 1
                if (!color)
                    continue;
#endif
                vram[x + (VRAM_WIDTH * y)] = color;
            }
        }
    }
}

void draw_polygon(u32 *commands, u32 flags, vec2i draw_offset, rect2 scissor)
{
    //u32 num_vertices = (flags & POLYGON_FLAG_IS_QUAD) ? 4 : 3;
    u32 stride = 1;

    u32 mode = 0;
    vec2i texture_page;
    vec2i clut_base;

    if (flags & POLYGON_FLAG_TEXTURED)
    {
        stride += 1;
        u32 clut_x = (commands[2] >> 16) & 0x3f;
        u32 clut_y = (commands[2] >> 22) & 0x1ff;

        u16 texpage = (u16)((commands[(flags & POLYGON_FLAG_GOURAUD_SHADED) ? 5 : 4]) >> 16);
        u32 texpage_x = texpage & 0xf;
        u32 texpage_y = (texpage >> 4) & 0x1;

        texture_page.x = texpage_x * 64;
        texture_page.y = texpage_y * 256;

        clut_base.x = clut_x * 16;
        clut_base.y = clut_y;
     
        switch ((texpage >> 7) & 0x3)
        {
        case 0: // 4-bit CLUT mode
            mode = 1;
            break;
        case 1: // 8-bit CLUT mode
            mode = 2;
            break;
        case 2: // 15-bit direct
        case 3:
            mode = 0;
            break;
        }
        
        if (flags & POLYGON_FLAG_RAW_TEXTURE)
        {
            // raw texture
            u32 color = 0x00808080;
            SY_ASSERT(0); // TODO: pass down the stride
        }
        else if (flags & POLYGON_FLAG_GOURAUD_SHADED)
        {
            // shaded textured
            stride += 1;
            u32 c0 = commands[stride * 0];
            u32 c1 = commands[stride * 1];
            u32 c2 = commands[stride * 2];

            u16 t0 = commands[2 + stride * 0];
            u16 t1 = commands[2 + stride * 1];
            u16 t2 = commands[2 + stride * 2];

            vec2i v0 = v2iadd(vertex_position(commands[1]), draw_offset);

            vec2i v1 = v2iadd(vertex_position(commands[1 + stride]), draw_offset);

            vec2i v2 = v2iadd(vertex_position(commands[1 + stride * 2]), draw_offset);

            draw_shaded_textured_triangle(c0, v2i(t0 & 0xff, (t0 >> 8) & 0xff), v0, c1, v2i(t1 & 0xff, (t1 >> 8) & 0xff), v1, c2, v2i(t2 & 0xff, (t2 >> 8) & 0xff), v2, 
                mode, texture_page, clut_base, scissor);

            if (flags & POLYGON_FLAG_IS_QUAD)
            {
                u32 c3 = commands[stride * 3];
                u16 t3 = commands[2 + stride * 3];
                vec2i v3 = v2iadd(vertex_position(commands[1 + stride * 3]), draw_offset);
                draw_shaded_textured_triangle(c1, v2i(t1 & 0xff, (t1 >> 8) & 0xff), v1, c2, v2i(t2 & 0xff, (t2 >> 8) & 0xff), v2, c3, v2i(t3 & 0xff, (t3 >> 8) & 0xff), v3, 
                    mode, texture_page, clut_base, scissor);
            }
        }
        else
        {
            // monochrome textured?
            u32 color = commands[0];

            u16 t0 = commands[2 + stride * 0];
            u16 t1 = commands[2 + stride * 1];
            u16 t2 = commands[2 + stride * 2];

            vec2i v0 = v2iadd(vertex_position(commands[1]), draw_offset);

            vec2i v1 = v2iadd(vertex_position(commands[1 + stride]), draw_offset);

            vec2i v2 = v2iadd(vertex_position(commands[1 + stride * 2]), draw_offset);

            draw_textured_triangle(color, v2i(t0 & 0xff, (t0 >> 8) & 0xff), v0, v2i(t1 & 0xff, (t1 >> 8) & 0xff), v1, v2i(t2 & 0xff, (t2 >> 8) & 0xff), v2,
                mode, texture_page, clut_base, scissor);

            if (flags & POLYGON_FLAG_IS_QUAD)
            {
                u16 t3 = commands[2 + stride * 3];
                vec2i v3 = v2iadd(vertex_position(commands[1 + stride * 3]), draw_offset);
                draw_textured_triangle(color, v2i(t1 & 0xff, (t1 >> 8) & 0xff), v1, v2i(t2 & 0xff, (t2 >> 8) & 0xff), v2, v2i(t3 & 0xff, (t3 >> 8) & 0xff), v3,
                    mode, texture_page, clut_base, scissor);
            }
        }
    }
    else
    {
        if (flags & POLYGON_FLAG_GOURAUD_SHADED)
        {
            // shaded polygon
            stride += 1;

            u16 c0 = color16from24(commands[stride * 0]);
            u16 c1 = color16from24(commands[stride * 1]);
            u16 c2 = color16from24(commands[stride * 2]);

            vec2i v0 = v2iadd(vertex_position(commands[1]), draw_offset);

            vec2i v1 = v2iadd(vertex_position(commands[1 + stride]), draw_offset);

            vec2i v2 = v2iadd(vertex_position(commands[1 + stride * 2]), draw_offset);

            draw_shaded_triangle(c0, v0, c1, v1, c2, v2, scissor);

            if (flags & POLYGON_FLAG_IS_QUAD)
            {
                u16 c3 = color16from24(commands[stride * 3]);
                vec2i v3 = v2iadd(vertex_position(commands[1 + stride * 3]), draw_offset);
                draw_shaded_triangle(c1, v1, c2, v2, c3, v3, scissor);
            }
        }
        else
        {
            // flat color polygon
            u16 c = color16from24(commands[0]);

            vec2i v0 = v2iadd(vertex_position(commands[1]), draw_offset);

            vec2i v1 = v2iadd(vertex_position(commands[1 + stride]), draw_offset);

            vec2i v2 = v2iadd(vertex_position(commands[1 + stride * 2]), draw_offset);

            draw_triangle(v0, v1, v2, c, scissor);

            if (flags & POLYGON_FLAG_IS_QUAD)
            {
                vec2i v3 = v2iadd(vertex_position(commands[1 + stride * 3]), draw_offset);
                draw_triangle(v1, v2, v3, c, scissor);
            }
        }
    }
}

static inline u16 blend_texel(u32 r, u32 g, u32 b, u16 texel)
{
    u32 red = (r * (texel & 0x1f)) >> 7;
    u32 green = (g * ((texel >> 5) & 0x1f)) >> 7;
    u32 blue = (b * ((texel >> 10) & 0x1f)) >> 7;
    return (red) | (green << 5) | (blue << 10) | (texel & 0x8000);
}

void draw_rectangle(u32 *commands, u32 flags, u32 texpage, vec2i draw_offset, rect2 scissor)
{
    software_renderer *renderer = (software_renderer *)g_renderer;
    u16 *vram = renderer->vram;

    u32 size = (commands[0] >> 27) & 0x3;
    u32 x_size, y_size;

    switch (size)
    {
    case 0:
        u32 param = commands[flags & RECT_FLAG_TEXTURED ? 3 : 2];
        x_size = param & 0x3ff;
        y_size = (param >> 16) & 0x1ff;
        break;
    case 1:
        x_size = y_size = 1;
        break;
    case 2:
        x_size = y_size = 8;
        break;
    case 3:
        x_size = y_size = 16;
        break;
    }

    vec2i pos = vertex_position(commands[1]);

    pos.x += draw_offset.x;
    pos.y += draw_offset.y;

    s32 width = pos.x + x_size;
    s32 height = pos.y + y_size;

    if (pos.x < scissor.left)
        pos.x = scissor.left;
    if (pos.y < scissor.top)
        pos.y = scissor.top;
    if (width > scissor.right)
        width = scissor.right;
    if (height > scissor.bottom)
        height = scissor.bottom;

    if (flags & RECT_FLAG_TEXTURED)
    {
        u32 r, g, b;

        if (flags & RECT_FLAG_RAW_TEXTURE)
        {
            r = g = b = 0x80;
        }
        else
        {
            u32 color = commands[0];
            r = color & 0xff;
            g = (color >> 8) & 0xff;
            b = (color >> 16) & 0xff;
        }

        u32 texpage_x = texpage & 0xf;
        u32 texpage_y = (texpage >> 4) & 0x1;

        vec2i texture_page;
        texture_page.x = texpage_x * 64;
        texture_page.y = texpage_y * 256;

        u32 clut_x = (commands[2] >> 16) & 0x3f;
        u32 clut_y = (commands[2] >> 22) & 0x1ff;

        u32 uv_x = commands[2] & 0xff;
        u32 uv_y = (commands[2] >> 8) & 0xff;

        //uv_x += texture_page.x;
        //uv_y += texture_page.y;

        vec2i clut_base;
        clut_base.x = clut_x * 16;
        clut_base.y = clut_y;
     
        u32 mode;

        switch ((texpage >> 7) & 0x3)
        {
        case 0: // 4-bit CLUT mode
            mode = 1;
            break;
        case 1: // 8-bit CLUT mode
            mode = 2;
            break;
        case 2: // 15-bit direct
        case 3:
            mode = 0;
            break;
        }

        u16 color;

        for (s32 y = pos.y, texcoord_y = uv_y; y < height; ++y, ++texcoord_y)
        {
            for (s32 x = pos.x, texcoord_x = uv_x; x < width; ++x, ++texcoord_x)
            {
                texcoord_x &= 0xff;
                texcoord_y &= 0xff;

                switch (mode)
                {
                case 0:
                {
                    u32 sample_x = texcoord_x + texture_page.x;
                    u32 sample_y = texcoord_y + texture_page.y;
                    u16 texel = vram[sample_x + (VRAM_WIDTH * sample_y)];
                    color = blend_texel(r, g, b, texel);
                    break;
                }
                case 1:
                {
                    s32 sample_x = (texcoord_x >> 2) + texture_page.x;
                    s32 sample_y = texcoord_y + texture_page.y;

                    s32 shift = (texcoord_x & 0x3) << 2;

                    u16 value = vram[sample_x + (VRAM_WIDTH * sample_y)];

                    u16 index = (value >> shift) & 0xf;

                    u16 indexed_color = vram[(clut_base.x + index) + (VRAM_WIDTH * clut_base.y)];

                    color = blend_texel(r, g, b, indexed_color);
                    break;
                }
                case 2:
                {
                    s32 sample_x = (texcoord_x >> 1) + texture_page.x;
                    s32 sample_y = texcoord_y + texture_page.y;

                    s32 shift = (texcoord_x & 0x1) << 3;

                    u16 value = vram[sample_x + (VRAM_WIDTH * sample_y)];

                    u16 index = (value >> shift) & 0xff;

                    u16 indexed_color = vram[(clut_base.x + index) + (VRAM_WIDTH * clut_base.y)];

                    color = blend_texel(r, g, b, indexed_color);
                    break;
                }
                }

                if (!color)
                    continue;

                vram[x + (VRAM_WIDTH * y)] = color;
            }
        }
    }
    else
    {
        u16 color = color16from24(commands[0]);

        for (s32 y = pos.y; y < height; ++y)
        {
            for (s32 x = pos.x; x < width; ++x)
            {
                vram[x + (VRAM_WIDTH * y)] = color;
            }
        }
    }
}
