#include "renderer.h"

void *push_render_command(renderer_interface *renderer, enum render_command_type type, u64 size)
{
    SY_ASSERT((renderer->commands_at + size) <= (renderer->render_commands + renderer->render_commands_size));
    struct render_command_header *result = (struct render_command_header *)renderer->commands_at;
    renderer->commands_at += size;
    ++renderer->render_commands_count;
    result->type = type;
    return (void *)result;
}

void push_draw_area(renderer_interface *renderer, rect2 draw_area)
{
    struct render_command_set_draw_area *cmd = push_render_command(renderer, RENDER_COMMAND_SET_DRAW_AREA, sizeof(struct render_command_set_draw_area));
    cmd->draw_area = draw_area;
}

void push_vram_flush(renderer_interface *renderer)
{
    struct render_command_flush_vram *cmd = push_render_command(renderer, RENDER_COMMAND_FLUSH_VRAM, sizeof(struct render_command_flush_vram));
}

void push_vram_copy(renderer_interface *renderer, u16 src_x, u16 src_y, u16 dst_x, u16 dst_y, u16 width, u16 height)
{
    struct render_command_transfer *cmd = push_render_command(renderer, RENDER_COMMAND_TRANSFER_VRAM_TO_VRAM, sizeof(struct render_command_transfer));
    cmd->x = src_x;
    cmd->y = src_y;
    cmd->x2 = dst_x;
    cmd->y2 = dst_y;
    cmd->width = width;
    cmd->height = height;
}

void push_cpu_to_vram_copy(renderer_interface *renderer, void **buffer, u16 dst_x, u16 dst_y, u16 width, u16 height)
{
    struct render_command_transfer *cmd = push_render_command(renderer, RENDER_COMMAND_TRANSFER_CPU_TO_VRAM, sizeof(struct render_command_transfer));
    cmd->buffer = buffer;
    cmd->x = dst_x;
    cmd->y = dst_y;
    cmd->width = width;
    cmd->height = height;
}

void push_vram_to_cpu_copy(renderer_interface *renderer, void **dst_buffer, u16 src_x, u16 src_y, u16 width, u16 height)
{
    struct render_command_transfer *cmd = push_render_command(renderer, RENDER_COMMAND_TRANSFER_VRAM_TO_CPU, sizeof(struct render_command_transfer));
    cmd->buffer = dst_buffer;
    cmd->x = src_x;
    cmd->y = src_y;
    cmd->width = width;
    cmd->height = height;
}

void push_primitive(renderer_interface *renderer, u32 vertex_count, u32 texture_mode)
{
    enum render_command_type type = texture_mode ? RENDER_COMMAND_DRAW_TEXTURED_PRIMITIVE : RENDER_COMMAND_DRAW_SHADED_PRIMITIVE;
    struct render_command_draw *cmd = push_render_command(renderer, type, sizeof(struct render_command_draw));
    cmd->texture_mode = texture_mode;
    cmd->vertex_count = vertex_count;
    cmd->vertex_array_offset = renderer->total_vertex_count;
}

void push_polygon(renderer_interface *renderer, u32 *commands, u32 flags, vec2 draw_offset)
{
    u32 in_color = commands[0];
    u32 color[4];

    u32 num_vertices = (flags & POLYGON_FLAG_IS_QUAD) ? 4 : 3;
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
            mode = 2;
            break;
        case 1: // 8-bit CLUT mode
            mode = 3;
            break;
        case 2: // 15-bit direct
        case 3:
            mode = 1;
            break;
        }

        // determine if textured area intersects with any unflushed regions
        #if 0
        rect2 texture_page_rect = {.left = texture_page.x, .right = texture_page.x + 64, .top = texture_page.y, .bottom = texture_page.y + 256};
        for (u32 i = 0; i < renderer->dirty_region_count; ++i)
        {
            if (rectangles_intersect(renderer->dirty_regions[i], texture_page_rect))
            {
                push_vram_flush(renderer);
                renderer->dirty_region_count = 0;
                break;
            }
        }
        #endif
    }

    if (flags & POLYGON_FLAG_RAW_TEXTURE)
    {
        color[0] = 0x00808080;
        color[3] = color[2] = color[1] = color[0];
    }
    else if (flags & POLYGON_FLAG_GOURAUD_SHADED)
    {    
        stride += 1;
        for (u32 i = 0; i < num_vertices; ++i)
        {
            color[i] = commands[stride * i];
        }
    }
    else
    {
        color[3] = color[2] = color[1] = color[0] = in_color;
    }

    render_vertex *v = renderer->vertex_array + renderer->total_vertex_count;
    u32 added_vertices = 3;

    for (u32 i = 0; i < 3; ++i)
    {
        v[i].pos = commands[1 + stride * i];//v2add(temp_cvt(commands[1 + stride * i]), draw_offset);
        if (flags & POLYGON_FLAG_TEXTURED)
        {
            v[i].uv = get_texcoord(commands[2 + stride * i]); // NOTE: yucky
            v[i].texture_page = texture_page;
            v[i].clut = clut_base;
        }
        v[i].color = color[i];

    }

    if (flags & POLYGON_FLAG_IS_QUAD)
    {
        added_vertices += 3;

        for (u32 i = 1; i < 4; ++i)
        {
            v[i + 2].pos = commands[1 + stride * i];//temp_cvt(commands[1 + stride * i]);
            if (flags & POLYGON_FLAG_TEXTURED)
            {
                v[i + 2].uv = get_texcoord(commands[2 + stride * i]); // NOTE: yucky
                v[i + 2].texture_page = texture_page;
                v[i + 2].clut = clut_base;
            }
            v[i + 2].color = color[i];
        }
    }

    // determine the aabb of the affected region of the draw
    #if 0
    vertex_attrib k = {.vertex = v[0].pos};
    u32 min_x = k.x;
    u32 min_y = k.y;
    u32 max_x = k.x;
    u32 max_y = k.y;

    for (u32 i = 1; i < added_vertices; ++i)
    {
        vertex_attrib vert = {.vertex = v[i].pos};
        if (vert.x < min_x)
            min_x = vert.x;
        else if (vert.x > max_x)
            max_x = vert.x;
        if (vert.y < min_y)
            min_y = vert.y;
        else if (vert.y > max_y)
            max_y = vert.y;
    }

    SY_ASSERT(renderer->dirty_region_count < ARRAYCOUNT(renderer->dirty_regions));
    rect2 *affected_area = &renderer->dirty_regions[renderer->dirty_region_count++];
    affected_area->left = min_x;
    affected_area->top = min_y;
    affected_area->right = max_x;
    affected_area->bottom = max_y;
    #else
    if (mode)
        push_vram_flush(renderer);
    #endif
    push_primitive(renderer, added_vertices, mode);
    renderer->total_vertex_count += added_vertices;
}

void push_rect(renderer_interface *renderer, u32 *commands, u32 flags, u32 texpage)
{
    vec2i texture_page = v2i((texpage & 0xf) * 64, ((texpage >> 4) & 0x1) * 256);
    vec2i clut_base;
    u8 uv_x;
    u8 uv_y;
    vertex_attrib base_texcoord;
    u32 color = commands[0] & 0xffffff;
    u32 offset = 0;
    u32 mode = 0;

    if (flags & RECT_FLAG_TEXTURED)
    {
        ++offset;
        uv_x = commands[2] & 0xff;
        uv_y = (u8)(commands[2] >> 8);
        base_texcoord.x = uv_x;
        base_texcoord.y = uv_y;
        u32 clut_x = (commands[2] >> 16) & 0x3f;
        u32 clut_y = (commands[2] >> 22) & 0x1ff;
        clut_base.x = clut_x * 16;
        clut_base.y = clut_y;

        switch ((texpage >> 7) & 0x3)
        {
        case 0: // 4-bit CLUT mode
            mode = 2;
            break;
        case 1: // 8-bit CLUT mode
            mode = 3;
            break;
        case 2: // 15-bit direct
        case 3:
            mode = 1;
            break;
        }
    }

    if (flags & RECT_FLAG_RAW_TEXTURE)
    {
        color = 0x00808080;
    }

    u32 width;
    u32 height;

    switch ((flags >> 3) & 0x3)
    {
    case 0x0: // variable size
        width = (u16)commands[2 + offset];
        height = (u16)(commands[2 + offset] >> 16);
        break;
    case 0x1: // 1x1
        width = height = 1;
        break;
    case 0x2: // 8x8
        width = height = 8;
        break;
    case 0x3: // 16x16
        width = height = 16;
        break;
    }

    render_vertex *v = renderer->vertex_array + renderer->total_vertex_count;

    //u32 base_vertex = commands[1];
    vertex_attrib base_vertex = {.vertex = commands[1]};
    u32 sizes[] = {0, width, (height << 16), width | (height << 16)};

    u32 loop[] = {0, 1, 2, 1, 2, 3};
    for (u32 l = 0, i = loop[0]; l < ARRAYCOUNT(loop); ++l, i = loop[l])
    {
        vertex_attrib current_vertex = {.vertex = sizes[i]};
        current_vertex.x += base_vertex.x;
        current_vertex.y += base_vertex.y;
        v[l].pos = current_vertex.vertex;
        //if (current_vertex.y == 44)
        //    DebugBreak();
        if (flags & RECT_FLAG_TEXTURED)
        {
            vertex_attrib a = {.vertex = sizes[i]};

            a.x += base_texcoord.x;
            a.y += base_texcoord.y;

            vec2 final_uv = v2f(a.x, a.y);

            v[l].uv = final_uv;
            v[l].clut = clut_base;
            v[l].texture_page = texture_page;
        }
        
        v[l].color = color;

    }

    if (mode)
        push_vram_flush(renderer);

    push_primitive(renderer, 6, mode);
    renderer->total_vertex_count += 6;
}
