#include "sw_renderer.h"
#include "gpu.h"
#include "debug/debug_ui.h"
#include "debug/atlas.h"

typedef struct draw_params
{
    u32 flags;
    u32 mode;
    rect2 clip;
    vec2i texpage;
    vec2i clut;
} draw_params;

typedef struct vertex
{
    vec2i pos;
    vec2i uv;
    u32 color;
} vertex;

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

    DeleteObject(renderer->fullscreen_bmp.handle);
    renderer->fullscreen_bmp.handle = CreateDIBSection(renderer->fullscreen_bmp.dc, &fullscreen, DIB_RGB_COLORS, (void **)&renderer->fullscreen_bmp.data, 0, 0);
    SelectObject(renderer->fullscreen_bmp.dc, renderer->fullscreen_bmp.handle);

    renderer->window_width = width;
    renderer->window_height = height;
#endif
}

void update_vram(void)
{
#if defined(SY_PLATFORM_WIN32)
    win32_software_renderer *renderer = (win32_software_renderer *)g_renderer;
    u16 *vram = renderer->sw.vram;
    u32 *vram_bitmap = renderer->vram_bmp.data;
    for (int i = 0; i < (VRAM_SIZE >> 1); ++i)
    {
        vram_bitmap[i] = swizzle_16_32(vram[i]);
    }
#endif
}

void draw_debug_ui(u32 *framebuffer, int width, int height)
{
    debug_ui_reset_command_ptr();

    rect2 clip_rect = {0, 0, width, height};

    struct debug_ui_command_header *cmd;
    while (debug_ui_next_command(&cmd))
    {
        switch (cmd->type)
        {
        case DEBUG_UI_COMMAND_SET_CLIP:
        {
            struct debug_ui_command_set_clip *clip = (struct debug_ui_command_set_clip *)cmd;
            clip_rect = clip->r;
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
                    //u32 alpha = (color & 0xff000000) >> 24;
                    framebuffer[x + (width * y)] = color;
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
                    pen.x += font_space_width;
                    continue;
                }

                atlas_glyph glyph = glyphs[c];
                // skip characters that are not on screen (assumes left to right chars)
                if (pen.x + glyph.left > clip_rect.right || pen.y - glyph.top > clip_rect.bottom) {
                    pen.x += glyph.advance;
                    continue;
                }

                rect2 dest = {
                    pen.x + glyph.left,
                    pen.y - glyph.top,
                    pen.x + glyph.left + glyph.w,
                    pen.y + glyph.h - glyph.top,
                };

                rect2 src = {
                    glyph.x,
                    glyph.y,
                    glyph.x + glyph.w,
                    glyph.y + glyph.h
                };
                
                int offset_x = 0;
                int offset_y = 0;
                
                if (dest.left < clip_rect.left)
                    offset_x = clip_rect.left - dest.left;

                if (dest.top < clip_rect.top)
                    offset_y = clip_rect.top - dest.top;

                if (dest.right > clip_rect.right)
                    dest.right = clip_rect.right;

                if (dest.bottom > clip_rect.bottom)
                    dest.bottom = clip_rect.bottom;

                int right = dest.right - dest.left;
                int bottom = dest.bottom - dest.top;

                u32 color = text->color;
                u32 r = color & 0xff;
                u32 g = (color >> 8) & 0xff;
                u32 b = (color >> 16) & 0xff;

                for (int y = offset_y; y < bottom; ++y)
                {
                    for (int x = offset_x; x < right; ++x)
                    {
                        u32 src_x = src.left + x;
                        u32 src_y = src.top + y;

                        u32 alpha = atlas[src_x + (ATLAS_WIDTH * src_y)];
                        u32 tr = (alpha * r) / 0xff;
                        u32 tg = (alpha * g) / 0xff;
                        u32 tb = (alpha * b) / 0xff;

                        u32 dst_x = dest.left + x;
                        u32 dst_y = dest.top + y;

                        u32 dst = dst_x + (width * dst_y);
                        u32 c_dst = framebuffer[dst];

                        u8 dst_b = c_dst & 0xff;
                        u8 dst_g = (c_dst >> 8) & 0xff;
                        u8 dst_r = (c_dst >> 16) & 0xff;

                        f32 factor = 1.0f - (alpha / 255.0f);
                        u32 c_b = tb + dst_b * factor;
                        u32 c_g = tg + dst_g * factor;
                        u32 c_r = tr + dst_r * factor;

                        framebuffer[dst] = c_b | (c_g << 8) | (c_r << 16);
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
}

static void update_display(void)
{
#if defined(SY_PLATFORM_WIN32)
    win32_software_renderer *renderer = (win32_software_renderer *)g_renderer;
    int window_width = renderer->window_width;
    int window_height = renderer->window_height;
    if (window_height == 0 || window_width == 0)
        return;
#if 1
    int src_x = g_gpu.vram_display_x;
    int src_y = g_gpu.vram_display_y;
    // ref: displayed pixels is rounded to multiple of 4
    int src_w = (((g_gpu.horizontal_display_x2 - g_gpu.horizontal_display_x1) / g_gpu.dot_div) + 2) & ~0x3;
    int src_h = g_gpu.vertical_display_y2 - g_gpu.vertical_display_y1;
    if ((g_gpu.stat.value >> 22) & 0x1)
    {
        SY_ASSERT(g_gpu.stat.vertical_res);
        src_h <<= 1;
    }

    int disp_w, disp_h;
    if (g_gpu.stat.horizontal_res_2)
    {
        disp_w = 368;
    }
    else
    {
        switch (g_gpu.stat.horizontal_res_1)
        {
        case 0:
            disp_w = 256;
            break;
        case 1:
            disp_w = 320;
            break;
        case 2:
            disp_w = 512;
            break;
        case 3:
            disp_w = 640;
            break;
        }
    }

    if (g_gpu.stat.vertical_res)
        disp_h = 480;
    else
        disp_h = 240;

    f32 ratio = disp_w / (f32)disp_h;
    f32 desired_width = window_height * ratio;
    f32 desired_height = window_width / ratio;
    int screen_w, screen_h;
    if (desired_width > window_width)
    {
        screen_w = window_width;
        screen_h = roundf(desired_height);
    }
    else
    {
        screen_w = roundf(desired_width);
        screen_h = window_height;
    }

    int screen_x = (window_width - screen_w) / 2;
    int screen_y = (window_height - screen_h) / 2;

    memset(renderer->fullscreen_bmp.data, 0, (window_width * window_height * 4));

    // TODO: implement
    HDC src_dc = 0;
    u32 disp_x = 0;
    u32 disp_y = 0;
    if (g_gpu.stat.display_depth_24_bit)
    {
        src_dc = renderer->vram24_bmp.dc;
        u16 *data = renderer->sw.vram;
        u32 *dst = renderer->vram24_bmp.data;
        int sy = src_y;

        for (int y = 0; y < src_h; ++y)
        {
            u8 *src = (u8 *)&data[src_x + (sy * VRAM_WIDTH)];
            for (int x = 0; x < src_w; ++x)
            {
                u32 red = src[0];
                u32 green = src[1];
                u32 blue = src[2];
                src += 3;
                u32 result = (red << 16) | (green << 8) | blue;
                
                dst[x + (y * 640)] = result;
            }
            ++sy;
        }
    }
    else
    {
        src_dc = renderer->vram_bmp.dc;
        disp_x = src_x;
        disp_y = src_y;
    }

    StretchBlt(renderer->fullscreen_bmp.dc, screen_x, screen_y, screen_w, screen_h, src_dc, disp_x, disp_y, src_w, src_h, SRCCOPY);
#else
    StretchBlt(renderer->fullscreen_bmp.dc, 0, 0, window_width, window_height, renderer->vram_bmp.dc, 0, 0, VRAM_WIDTH, VRAM_HEIGHT, SRCCOPY);
#endif

    draw_debug_ui(renderer->fullscreen_bmp.data, window_width, window_height);

    HDC window_dc = GetDC(renderer->window);

    BitBlt(window_dc, 0, 0, window_width, window_height, renderer->fullscreen_bmp.dc, 0, 0, SRCCOPY);

    ReleaseDC(renderer->window, window_dc);
#endif
}

static void win32_delete_bitmap(win32_bitmap *bmp)
{
    DeleteObject(bmp->handle);
    DeleteDC(bmp->dc);
}

static void win32_create_bitmap(int width, int height, int bpp, win32_bitmap *bmp)
{
    BITMAPINFO bitmap_info = {0};
    bitmap_info.bmiHeader.biSize = sizeof(bitmap_info.bmiHeader);
    bitmap_info.bmiHeader.biWidth = width;
    bitmap_info.bmiHeader.biHeight = -height; // negative height so we can use 0,0 as top-left
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = bpp;
    bitmap_info.bmiHeader.biCompression = BI_RGB;

    bmp->dc = CreateCompatibleDC(0);
    bmp->handle = CreateDIBSection(bmp->dc, &bitmap_info, DIB_RGB_COLORS, &bmp->data, NULL, 0);

    SelectObject(bmp->dc, bmp->handle);
}

void platform_shutdown_software_renderer(void)
{
#if defined(SY_PLATFORM_WIN32)
    win32_software_renderer *renderer = (win32_software_renderer *)g_renderer;
    win32_delete_bitmap(&renderer->fullscreen_bmp);
    win32_delete_bitmap(&renderer->vram_bmp);
    win32_delete_bitmap(&renderer->vram24_bmp);
    free(renderer->sw.vram);
    free(renderer);
#endif
}

software_renderer *platform_init_software_renderer(platform_window *window)
{
#if defined(SY_PLATFORM_WIN32)
    win32_software_renderer *result = malloc(sizeof(win32_software_renderer));
    if (!result)
        return NULL;

    memset(result, 0, sizeof(win32_software_renderer));

    // actual vram data which draw commands write to
    void *vram = malloc(VRAM_SIZE);
    if (!vram)
    {
        DeleteDC(result->vram_bmp.dc);
        free(result);
        return NULL;
    }

    // vram texture that is blitted to screen, copied from the original vram
    win32_create_bitmap(VRAM_WIDTH, VRAM_HEIGHT, 32, &result->vram_bmp);

    result->sw.vram = vram;

    result->window = window->handle;

    RECT client;
    GetClientRect(window->handle, &client);

    int width = client.right - client.left;
    int height = client.bottom - client.top;

    win32_create_bitmap(width, height, 32, &result->fullscreen_bmp);
    SetStretchBltMode(result->fullscreen_bmp.dc, COLORONCOLOR);

    win32_create_bitmap(640, 480, 32, &result->vram24_bmp);

    result->window_width = width;
    result->window_height = height;

    result->sw.base.is_initialized = true;
    //result->base.flush_commands = renderer_stub_function;
    result->sw.base.present = renderer_stub_function;
    result->sw.base.handle_resize = handle_resize;
    result->sw.base.update_display = update_display;
    result->sw.base.shutdown = platform_shutdown_software_renderer;

    return (software_renderer *)result;
#endif
}

static int dither_offsets[16] = {-4, 0, -3, 1, 2, -2, 3, -1, -3, 1, -4, 0, 3, -1, 2, -2};

static inline int clamp8(int value)
{
    if (value > 0xff)
        return 0xff;
    if (value < 0)
        return 0;
    return value;
}

#define CLAMP_UPPER(value, to) (value) > (to) ? (to) : (value)
#define CLAMP_LOWER(value, to) (value) < (to) ? (to) : (value)

static inline u32 blend_color(u8 mode, u32 bg_color, u32 fg_color)
{
    s32 br = bg_color & 0x1f;
    s32 bg = (bg_color >> 5) & 0x1f;
    s32 bb = (bg_color >> 10) & 0x1f;

    s32 fr = fg_color & 0x1f;
    s32 fg = (fg_color >> 5) & 0x1f;
    s32 fb = (fg_color >> 10) & 0x1f;
    u32 rr, rg, rb;
    switch (mode)
    {
    case 0:
        rr = (br + fr) >> 1;
        rg = (bg + fg) >> 1;
        rb = (bb + fb) >> 1;
        break;
    case 1:
        rr = CLAMP_UPPER(br + fr, 31);
        rg = CLAMP_UPPER(bg + fg, 31);
        rb = CLAMP_UPPER(bb + fb, 31);
        break;
    case 2:
        rr = CLAMP_LOWER(br - fr, 0);
        rg = CLAMP_LOWER(bg - fg, 0);
        rb = CLAMP_LOWER(bb - fb, 0);
        break;
    case 3:
        rr = CLAMP_UPPER(br + (fr >> 2), 31);
        rg = CLAMP_UPPER(bg + (fg >> 2), 31);
        rb = CLAMP_UPPER(bb + (fb >> 2), 31);
        break;
    }
    u32 result = rr | (rg << 5) | (rb << 10) | (fg_color & 0x8000);
    return result;
}

static inline u32 blend_texel(u32 r, u32 g, u32 b, u16 texel)
{
    // texel channels are expanded to 8-bit before being converted to 5-bit
    u32 texel_r = (texel & 0x1f) << 3;
    u32 texel_g = (texel >> 2) & 0xf8;
    u32 texel_b = (texel >> 7) & 0xf8;
    u32 red = ((r * texel_r) >> 7) >> 3;
    u32 green = ((g * texel_g) >> 7) >> 3;
    u32 blue = ((b * texel_b) >> 7) >> 3;
    return red | (green << 5) | (blue << 10) | (texel & 0x8000);
}

static inline s32 edge(vec2i a, vec2i b, vec2i p)
{
    return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
}

static inline int top_left(vec2i a, vec2i b)
{
    if ((b.x > a.x && a.y == b.y) || b.y < a.y)
        return 0;
    return -1;
}

static void rasterize_triangle(vertex v0, vertex v1, vertex v2, draw_params *params)
{
    software_renderer *renderer = (software_renderer *)g_renderer;
    u16 *vram = renderer->vram;

    s32 area = edge(v0.pos, v1.pos, v2.pos);
    if (!area)
        return;

    if (area < 0)
    {
        // sort to clockwise ordering
        vertex swap = v2;
        v2 = v1;
        v1 = swap;
        area = -area;
    }

    int minX = min3(v0.pos.x, v1.pos.x, v2.pos.x);
    int maxX = max3(v0.pos.x, v1.pos.x, v2.pos.x);
    int minY = min3(v0.pos.y, v1.pos.y, v2.pos.y);
    int maxY = max3(v0.pos.y, v1.pos.y, v2.pos.y);

    if ((maxX - minX) > 1023 || (maxY - minY) > 511)
        return;

    rect2 *clip = &params->clip;
    if (minX < clip->left)
        minX = clip->left;
    if (minY < clip->top)
        minY = clip->top;
    if (maxX > clip->right + 1)
        maxX = clip->right + 1;
    if (maxY > clip->bottom + 1)
        maxY = clip->bottom + 1;

    u32 c0b = (v0.color >> 16) & 0xff;
    u32 c0g = (v0.color >> 8) & 0xff;
    u32 c0r = v0.color & 0xff;

    u32 c1b = (v1.color >> 16) & 0xff;
    u32 c1g = (v1.color >> 8) & 0xff;
    u32 c1r = v1.color & 0xff;

    u32 c2b = (v2.color >> 16) & 0xff;
    u32 c2g = (v2.color >> 8) & 0xff;
    u32 c2r = v2.color & 0xff;

    vec2i texpage = params->texpage;
    vec2i clut = params->clut;
    u32 mode = params->mode;
    u32 flags = params->flags;
    vec2i p = {.x = minX, .y = minY};

    int bias0 = top_left(v1.pos, v2.pos);
    int bias1 = top_left(v2.pos, v0.pos);
    int bias2 = top_left(v0.pos, v1.pos);

    // Pineda edge test
    s32 e0_row = edge(v1.pos, v2.pos, p) + bias0;
    s32 e1_row = edge(v2.pos, v0.pos, p) + bias1;
    s32 e2_row = edge(v0.pos, v1.pos, p) + bias2;

    s32 dy0 = v1.pos.y - v2.pos.y;
    s32 dy1 = v2.pos.y - v0.pos.y;
    s32 dy2 = v0.pos.y - v1.pos.y;

    s32 dx0 = v2.pos.x - v1.pos.x;
    s32 dx1 = v0.pos.x - v2.pos.x;
    s32 dx2 = v1.pos.x - v0.pos.x;

    b32 set_mask = g_gpu.stat.set_mask_on_draw << 15;
    b8 check_mask = g_gpu.stat.check_mask_before_draw;
    u8 semi_transparency = g_gpu.stat.semi_transparency_mode;
    b8 dither = g_gpu.stat.dither_enable && ((flags & POLYGON_FLAG_GOURAUD_SHADED) || !(flags & POLYGON_FLAG_RAW_TEXTURE));

    for (int y = minY; y < maxY; ++y)
    {
        s32 e0 = e0_row;
        s32 e1 = e1_row;
        s32 e2 = e2_row;

        for (int x = minX; x < maxX; ++x)
        {   
            if ((e0 | e1 | e2) >= 0)
            {
                if (check_mask)
                {
                    if (vram[x + y * VRAM_WIDTH] & 0x8000)
                    {
                        e0 += dy0;
                        e1 += dy1;
                        e2 += dy2;
                        continue;
                    }
                }

                // fix barycentric coordinates
                s32 w0 = e0 - bias0;
                s32 w1 = e1 - bias1;
                s32 w2 = e2 - bias2;
                
                u32 r = c0r;
                u32 g = c0g;
                u32 b = c0b;
                u16 mask = 0;
        
                if (flags & POLYGON_FLAG_GOURAUD_SHADED)
                {
                    r = (w0 * c0r + w1 * c1r + w2 * c2r) / area;
                    g = (w0 * c0g + w1 * c1g + w2 * c2g) / area;
                    b = (w0 * c0b + w1 * c1b + w2 * c2b) / area;
                }

                if (flags & POLYGON_FLAG_TEXTURED)
                {
                    s32 texcoord_x = (w0 * v0.uv.x + w1 * v1.uv.x + w2 * v2.uv.x) / area;
                    s32 texcoord_y = (w0 * v0.uv.y + w1 * v1.uv.y + w2 * v2.uv.y) / area;

                    //u32 texel = (u32)sample_texture(vram, mode, texcoord_x, texcoord_y, texture_page, clut);
                    u32 texel = 0;

                    switch (mode)
                    {
                    case TEXTURE_MODE_DIRECT:
                    {
                        s32 sample_x = texcoord_x + texpage.x;
                        s32 sample_y = texcoord_y + texpage.y;
                        texel = vram[sample_x + (sample_y * VRAM_WIDTH)];
                        break;
                    }
                    case TEXTURE_MODE_4BPP:
                    {
                        s32 sample_x = (texcoord_x >> 2) + texpage.x;
                        s32 sample_y = texcoord_y + texpage.y;

                        s32 shift = (texcoord_x & 0x3) << 2;

                        u16 sample = vram[sample_x + (sample_y * VRAM_WIDTH)];

                        int index = (sample >> shift) & 0xf;

                        texel = vram[(clut.x + index) + (clut.y * VRAM_WIDTH)];
                        break;
                    }
                    case TEXTURE_MODE_8BPP:
                    {
                        s32 sample_x = (texcoord_x >> 1) + texpage.x;
                        s32 sample_y = texcoord_y + texpage.y;

                        s32 shift = (texcoord_x & 0x1) << 3;

                        u16 sample = vram[sample_x + (sample_y * VRAM_WIDTH)];

                        int index = (sample >> shift) & 0xff;

                        texel = vram[(clut.x + index) + (clut.y * VRAM_WIDTH)];
                        break;
                    }
                    }

                    if (!texel)
                    {
                        e0 += dy0;
                        e1 += dy1;
                        e2 += dy2;
                        continue;
                    }

                    // texture modulation - raw textures have a color value of 0x808080, which negates the modulation
                    // we could also just branch if its a raw texture :p
                    r = (r * ((texel & 0x1f) << 3)) >> 7;
                    g = ((g * ((texel >> 5) & 0x1f) << 3) >> 7);
                    b = ((b * ((texel >> 10) & 0x1f) << 3) >> 7);
                    mask = texel & 0x8000;
                }

                if (dither)
                {
                    int offset = dither_offsets[(x & 0x3) + ((y & 0x3) * 4)];
                    r = clamp8(r + offset);
                    g = clamp8(g + offset);
                    b = clamp8(b + offset);
                }

                u16 color = color16from888(r, g, b) | mask;

                if (flags & POLYGON_FLAG_SEMI_TRANSPARENT && (!(flags & POLYGON_FLAG_TEXTURED) || (color & 0x8000)))
                {
                    u32 b = (u32)vram[x + y * VRAM_WIDTH];
                    color = blend_color(semi_transparency, b, color);
                }

                color |= set_mask;
                
                vram[x + (VRAM_WIDTH * y)] = color;
            }

            e0 += dy0;
            e1 += dy1;
            e2 += dy2;
        }
        
        e0_row += dx0;
        e1_row += dx1;
        e2_row += dx2;
    }
}

void draw_polygon(u32 *commands, u32 op)
{
    // op is commands[0] >> 24, passed for convenience
    u32 stride = 1;

    u32 mode = 0;
    vec2i texture_page;
    vec2i clut_base;
    rect2 clip = g_gpu.drawing_area;

    int draw_offset_x = (int)g_gpu.draw_offset_x;
    int draw_offset_y = (int)g_gpu.draw_offset_y;

    draw_params params;
    params.flags = op;
    params.clip = g_gpu.drawing_area;

    int is_raw_texture = op & POLYGON_FLAG_RAW_TEXTURE;
    int is_gouraud = op & POLYGON_FLAG_GOURAUD_SHADED;
    int is_textured = op & POLYGON_FLAG_TEXTURED;
    
    if (is_textured)
    {
        ++stride;

        u32 clut_x = (commands[2] >> 16) & 0x3f;
        u32 clut_y = (commands[2] >> 22) & 0x1ff;

        u32 texpage = commands[(op & POLYGON_FLAG_GOURAUD_SHADED) ? 5 : 4] >> 16;
        u32 texpage_x = texpage & 0xf;
        u32 texpage_y = (texpage >> 4) & 0x1;

        params.texpage.x = texpage_x * 64;
        params.texpage.y = texpage_y * 256;

        params.clut.x = clut_x * 16;
        params.clut.y = clut_y;
     
        switch ((texpage >> 7) & 0x3)
        {
        case 0:
            mode = TEXTURE_MODE_4BPP;
            break;
        case 1:
            mode = TEXTURE_MODE_8BPP;
            break;
        case 2:
        case 3:
            mode = TEXTURE_MODE_DIRECT;
            break;
        }

        params.mode = mode;
    }

    if (is_gouraud)
    {
        ++stride;
    }

    u32 color = commands[0]; // by default, color is the first word in the command
    
    vertex v[4];

    s32 v0 = commands[1];
    s32 v1 = commands[1 + stride];
    s32 v2 = commands[1 + stride * 2];

    v[0].pos.x = VERTEX_X(v0) + draw_offset_x;
    v[0].pos.y = VERTEX_Y(v0) + draw_offset_y;

    v[1].pos.x = VERTEX_X(v1) + draw_offset_x;
    v[1].pos.y = VERTEX_Y(v1) + draw_offset_y;

    v[2].pos.x = VERTEX_X(v2) + draw_offset_x;
    v[2].pos.y = VERTEX_Y(v2) + draw_offset_y;

    if (is_textured)
    {
        u16 t0 = commands[2 + stride * 0];
        u16 t1 = commands[2 + stride * 1];
        u16 t2 = commands[2 + stride * 2];

        v[0].uv.x = t0 & 0xff;
        v[0].uv.y = (t0 >> 8) & 0xff;

        v[1].uv.x = t1 & 0xff;
        v[1].uv.y = (t1 >> 8) & 0xff;

        v[2].uv.x = t2 & 0xff;
        v[2].uv.y = (t2 >> 8) & 0xff;

        if (is_raw_texture)
        {
            // gouraud shading without modulation means we output raw textures without blending, assuming texturing is enabled
            is_gouraud = 0;
            color = 0x808080;
        }
    }

    if (is_gouraud)
    {
        v[0].color = commands[stride * 0];
        v[1].color = commands[stride * 1];
        v[2].color = commands[stride * 2];
    }
    else
    {
        // flat shading/raw-texture
        v[0].color = v[1].color = v[2].color = v[3].color = color;
    }

    rasterize_triangle(v[0], v[1], v[2], &params);

    if (op & POLYGON_FLAG_IS_QUAD)
    {
        s32 v3 = commands[1 + stride * 3];
        v[3].pos.x = VERTEX_X(v3) + draw_offset_x;
        v[3].pos.y = VERTEX_Y(v3) + draw_offset_y;

        if (is_textured)
        {
            u16 t3 = commands[2 + stride * 3];
            v[3].uv.x = t3 & 0xff;
            v[3].uv.y = (t3 >> 8) & 0xff;
        }

        if (is_gouraud)
        {
            v[3].color = commands[stride * 3];
        }

        rasterize_triangle(v[1], v[2], v[3], &params);
    }
}

void draw_rectangle(u32 *commands, u32 op)
{
    software_renderer *renderer = (software_renderer *)g_renderer;
    u16 *vram = renderer->vram;

    u32 size = (commands[0] >> 27) & 0x3;
    u32 x_size, y_size;

    switch (size)
    {
    case 0:
        u32 param = commands[op & RECT_FLAG_TEXTURED ? 3 : 2];
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

    s32 pos = commands[1];
    int draw_offset_x = (int)g_gpu.draw_offset_x;
    int draw_offset_y = (int)g_gpu.draw_offset_y;
    int pos_x = VERTEX_X(pos) + draw_offset_x;
    int pos_y = VERTEX_Y(pos) + draw_offset_y;

    int x1 = pos_x;
    int y1 = pos_y;
    int x2 = pos_x + x_size;
    int y2 = pos_y + y_size;

    rect2 clip = g_gpu.drawing_area;

    if (x1 < clip.left)
        x1 = clip.left;
    if (y1 < clip.top)
        y1 = clip.top;
    if (x2 > clip.right + 1)
        x2 = clip.right + 1;
    if (y2 > clip.bottom + 1)
        y2 = clip.bottom + 1;

    b8 check_mask = g_gpu.stat.check_mask_before_draw;
    b32 set_mask = g_gpu.stat.set_mask_on_draw << 15;
    u8 semi_transparency = g_gpu.stat.semi_transparency_mode;

    if (op & RECT_FLAG_TEXTURED)
    {
        u32 draw_mode = g_gpu.stat.value & 0x1ff;
        u32 r, g, b;

        if (op & RECT_FLAG_RAW_TEXTURE)
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

        u32 texpage_x = draw_mode & 0xf;
        u32 texpage_y = (draw_mode >> 4) & 0x1;

        vec2i texpage;
        texpage.x = texpage_x * 64;
        texpage.y = texpage_y * 256;

        u32 uv = commands[2];

        u32 clut_x = (uv >> 16) & 0x3f;
        u32 clut_y = (uv >> 22) & 0x1ff;

        vec2i clut;
        clut.x = clut_x * 16;
        clut.y = clut_y;

        u32 uv_x = uv & 0xff;
        u32 uv_y = (uv >> 8) & 0xff;

        u32 mode;

        switch ((draw_mode >> 7) & 0x3)
        {
        case 0:
            mode = TEXTURE_MODE_4BPP;
            break;
        case 1:
            mode = TEXTURE_MODE_8BPP;
            break;
        case 2:
        case 3:
            mode = TEXTURE_MODE_DIRECT;
            break;
        }

        u16 color;
        int texcoord_row = uv_y + (y1 - pos_y);
        int texcoord_col = uv_x + (x1 - pos_x);

        for (int y = y1, texcoord_y = texcoord_row; y < y2; ++y, ++texcoord_y)
        {
            texcoord_y &= 0xff;

            for (int x = x1, texcoord_x = texcoord_col; x < x2; ++x, ++texcoord_x)
            {
                texcoord_x &= 0xff;

                if (check_mask)
                {
                    if (vram[x + y * VRAM_WIDTH] & 0x8000)
                        continue;
                }

                u16 texel = 0;

                switch (mode)
                {
                case TEXTURE_MODE_DIRECT:
                {
                    s32 sample_x = texcoord_x + texpage.x;
                    s32 sample_y = texcoord_y + texpage.y;
                    texel = vram[sample_x + (sample_y * VRAM_WIDTH)];
                    break;
                }
                case TEXTURE_MODE_4BPP:
                {
                    s32 sample_x = (texcoord_x >> 2) + texpage.x;
                    s32 sample_y = texcoord_y + texpage.y;

                    s32 shift = (texcoord_x & 0x3) << 2;

                    u16 sample = vram[sample_x + (sample_y * VRAM_WIDTH)];

                    int index = (sample >> shift) & 0xf;

                    texel = vram[(clut.x + index) + (clut.y * VRAM_WIDTH)];
                    break;
                }
                case TEXTURE_MODE_8BPP:
                {
                    s32 sample_x = (texcoord_x >> 1) + texpage.x;
                    s32 sample_y = texcoord_y + texpage.y;

                    s32 shift = (texcoord_x & 0x1) << 3;

                    u16 sample = vram[sample_x + (sample_y * VRAM_WIDTH)];

                    int index = (sample >> shift) & 0xff;

                    texel = vram[(clut.x + index) + (clut.y * VRAM_WIDTH)];
                    break;
                }
                }

                if (!texel)
                    continue;

                color = (u16)blend_texel(r, g, b, texel);

                if (op & RECT_FLAG_SEMI_TRANSPARENT && (color & 0x8000))
                {
                    u16 b = vram[x + (y * VRAM_WIDTH)];
                    color = blend_color(semi_transparency, b, color);
                }

                color |= set_mask;

                vram[x + (y * VRAM_WIDTH)] = color;
            }
        }
    }
    else
    {   
        u16 color = color16from24(commands[0]);
        color |= set_mask;
        for (int y = y1; y < y2; ++y)
        {
            for (int x = x1; x < x2; ++x)
            {
                u16 result = color;
                if (check_mask)
                {
                    if (vram[x + (y * VRAM_WIDTH)] & 0x8000)
                        continue;
                }

                if (op & RECT_FLAG_SEMI_TRANSPARENT)
                {
                    u16 b = vram[x + (y * VRAM_WIDTH)];
                    result = blend_color(semi_transparency, b, color);
                }

                vram[x + (y * VRAM_WIDTH)] = result;
            }
        }
    }
}
