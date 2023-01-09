static inline u16 swizzle_texel(u16 pixel)
{

    u8 red = (pixel & 0x1f);
    u8 green = ((pixel >> 5) & 0x1f);
    u8 blue = ((pixel >> 10) & 0x1f);
    u16 mask = pixel & 0x8000;
    return mask | (u16)(red << 10) | (u16)(green << 5) | (u16)blue;
}

inline s32 edge(vec2i a, vec2i b, vec2i p)
{
    // (px - v0x) * (v1y - v0y) - (py - v0y) * (v1x - v0x)
    return ((p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x));//v1.x * v2.y - v1.y * v2.x;
}

static void draw_triangle(struct gpu_state* gpu, vec2i v1, vec2i v2, vec2i v3, u16 color)
{
    // TODO: clippping
    // get aabb of triangle
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

static inline void gp0_cpu_to_vram(struct gpu_state* gpu, u32 word)
{
    if (gpu->load.x == (gpu->load.left + gpu->load.width)) // TODO: handle odd transfers
    {
        gpu->load.x = gpu->load.left;
        ++gpu->load.y;
    }

    ((u16*)gpu->vram)[gpu->load.x + (VRAM_WIDTH * gpu->load.y)] = swizzle_texel((u16)word);
    ++gpu->load.x;
    ((u16*)gpu->vram)[gpu->load.x + (VRAM_WIDTH * gpu->load.y)] = swizzle_texel((word >> 16));
    ++gpu->load.x;
    gpu->load.pending_halfwords -= 2;
    if (!gpu->pending_words)
        gpu->pending_load = 0;
}

static inline u16 color16from24(u32 color)
{
    u8 red = ((color >> 3) & 0x1f);
    u8 green = ((color >> 11) & 0x1f);
    u8 blue = ((color >> 19) & 0x1f);
    return (u16)(red << 10) | (u16)(green << 5) | (u16)blue;
}

// NOTE: not sure how this works, it says if certain gp1 commands are issued, then they are immediately read from GPUREAD,
// does this mean they interrupt VRAM->CPU transfers and place themselves ahead of the data to be read?
static inline u32 gpuread(struct gpu_state* gpu)
{
#if SOFTWARE_RENDERING
    // TODO: assumes even number of pixels
    if (gpu->store.x > (gpu->store.left + gpu->store.width))
    {
        gpu->store.x = gpu->store.left;
        ++gpu->store.y;
    }
    u32 index = (gpu->store.y * 1024) + gpu->store.x;
    u32 word = swizzle_texel(((u16*)gpu->vram)[index]) | (swizzle_texel((u32)(((u16*)gpu->vram)[index + 1])) << 16);
    gpu->store.x += 2;
    return word;
#else
    if (gpu->readback_buffer_len == gpu->store.pending_halfwords)
    {
        gpu->stat.ready_to_send_vram = 0;
        return gpu->read;
    }
    u32 first_pixel = swizzle_texel(gpu->readback_buffer[gpu->readback_buffer_len++]);
    u32 second_pixel = 0;

    //--gpu->store.pending_halfwords;
    //if (gpu->store.pending_halfwords != 1) // Extra word sent for odd transfers wont contain a second pixel
    {
        second_pixel = swizzle_texel(gpu->readback_buffer[gpu->readback_buffer_len++]);
        //--gpu->store.pending_halfwords;
    }
    u32 word = (first_pixel & 0xffff) | (second_pixel << 16);
    return word;
#endif
}

static inline void execute_gp1_command(struct gpu_state* gpu, u32 command)
{
    u8 op = command >> 24;

    switch (op)
    {
    case 0x0:
        gpu->fifo_len = 0;
        gpu->pending_load = 0;
        gpu->pending_store = 0;
        gpu->pending_words = 0;
        gpu->copy_buffer_len = 0;
        gpu->readback_buffer_len = 0;
        gpu->stat.value = 0x14802000;
        break;
    case 0x01:
        gpu->fifo_len = 0;
        gpu->pending_load = 0;
        gpu->pending_store = 0;
        gpu->pending_words = 0;
        gpu->copy_buffer_len = 0;
        gpu->readback_buffer_len = 0;
        break;
    case 0x02:
        gpu->stat.irq = 0;
        break;
    case 0x03:
        gpu->stat.display_disabled = (command & 0x1);
        break;
    case 0x04:
        gpu->stat.data_request = (command & 0x3);
        break;
    case 0x05:
        gpu->vram_display_x = command & 0x3fe; // halfword address
        gpu->vram_display_y = (command >> 10) & 0x1ff;
        break;
    case 0x06:
        gpu->horizontal_display_x1 = command & 0xfff;
        gpu->horizontal_display_x2 = (command >> 12) & 0xfff;
        break;
    case 0x07:
        gpu->vertical_display_y1 = command & 0x3ff;
        gpu->vertical_display_y2 = (command >> 10) & 0x3ff;
        break;
    case 0x08:
        gpu->stat.value &= (~0x7f0000);
        gpu->stat.value |= (command & 0x3f) << 17;
        gpu->stat.value |= (command & 0x40) << 10;
        //gpu->stat &= ~(1 << 19); // TODO: temp bit clear to avoid even/odd signaling
        break;
    default:
        printf("Unknown gp1 command: %x\n", op);
        break;
    }
    //printf("GP1 command: %02xh\n", op);
}

static void execute_gp0_command(struct gpu_state* gpu, u32 word)
{
    if (!gpu->pending_words) // get number of remaining words in cmd (includes command itself)
    {
        u8 op = word >> 24;
        switch (op)
        {
        case 0x0:
        case 0x01:
        case 0xe1:
        case 0xe2:
        case 0xe3:
        case 0xe4:
        case 0xe5:
        case 0xe6:
            gpu->pending_words = 1;
            break;
        case 0x28:
            gpu->pending_words = 5;
            break;
        case 0x02:
        case 0xc0:
        case 0xa0:
            gpu->pending_words = 3;
            break;
        case 0x80:
        case 0x65:
            gpu->pending_words = 4;
            break;
        case 0x38:
            gpu->pending_words = 8;
            break;
        case 0x30:
            gpu->pending_words = 6;
            break;
        case 0x2c:
        case 0x2d:
            gpu->pending_words = 9;
            break;
        case 0x68:
            gpu->pending_words = 2;
            break;
        default:
            printf("Unknown gp0 command: %x\n", op);
            break;
        }
        gpu->fifo_len = 0;
    }

    --gpu->pending_words;

    if (gpu->pending_load)
    {
#if SOFTWARE_RENDERING
        gp0_cpu_to_vram(gpu, word);
#else
        gpu->copy_buffer[gpu->copy_buffer_len++] = swizzle_texel((u16)word); // NOTE: we should probably handling swizzling on the gpu
        --gpu->load.pending_halfwords;
        {
            gpu->copy_buffer[gpu->copy_buffer_len++] = swizzle_texel((u16)(word >> 16));
            --gpu->load.pending_halfwords;
        }
        if (!gpu->pending_words)
        {
            gpu->pending_load = 0;
            g_renderer->transfer(gpu->copy_buffer, gpu->load.x, gpu->load.y, gpu->load.width, gpu->load.height);
            gpu->copy_buffer_len = 0;
        }
#endif
    }
    else
    {
        gpu->fifo[gpu->fifo_len++] = word;
        SY_ASSERT(gpu->fifo_len <= 16);
        if (!gpu->pending_words)
        {
            u32* commands = gpu->fifo;
            u8 op = commands[0] >> 24;
            switch (op)
            {
            case 0x0:
                //SY_ASSERT(0);
                break;
            case 0x01:
                printf("Unhandled clear cache command in gp0\n");
                break;
            case 0xe1:
                gpu->stat.value &= 0xffff7800;
                gpu->stat.value |= (commands[0] & 0x7ff);
                gpu->stat.value |= (commands[0] & 0x800) << 4;
                // TODO: finish
                break;
            case 0xe2:
                gpu->texture_window_mask_x = commands[0] & 0x1f;
                gpu->texture_window_mask_y = (commands[0] >> 5) & 0x1f;
                gpu->texture_window_offset_x = (commands[0] >> 10) & 0x1f;
                gpu->texture_window_offset_y = (commands[0] >> 15) & 0x1f; 
                break;
            case 0xe3:
                gpu->drawing_area.left = (commands[0] & 0x3ff);
                gpu->drawing_area.top = ((commands[0] >> 10) & 0x3ff);
                break;
            case 0xe4:
                gpu->drawing_area.right = (commands[0] & 0x3ff);
                gpu->drawing_area.bottom = ((commands[0] >> 10) & 0x3ff);
                break;
            case 0xe5:
                u16 x_offset = (commands[0] & 0x7ff) << 5;
                gpu->draw_offset.x = (s16)x_offset >> 5;
                u16 y_offset = ((commands[0] >> 11) & 0x7ff) << 5;
                gpu->draw_offset.y = (s16)y_offset >> 5;
                //g_renderer->draw_frame(gpu->vram); // TODO: temp
                break;
            case 0xe6:
                gpu->stat.set_mask_on_draw = (commands[0] & 0x1);
                gpu->stat.draw_to_masked = (commands[0] >> 1) & 0x1;
                break;
            case 0x28:
            {
            #if SOFTWARE_RENDERING
                s16 x0 = (commands[1] & 0xffff), y0 = (commands[1] >> 16);
                s16 x1 = (commands[2] & 0xffff), y1 = (commands[2] >> 16);
                s16 x2 = (commands[3] & 0xffff), y2 = (commands[3] >> 16);
                s16 x3 = (commands[4] & 0xffff), y3 = (commands[4] >> 16);
                u32 color = commands[0];
                u8 red = ((color >> 3) & 0x1f);
                u8 green = ((color >> 11) & 0x1f);
                u8 blue = ((color >> 19) & 0x1f);
                u16 pixel_color = (u16)(red << 10) | (u16)(green << 5) | (u16)blue;
                // vertex order : 1,2,3, 2,3,4 (winding order doesn't matter)
                draw_triangle(gpu, v2i(x0, y0), v2i(x1, y1), v2i(x2, y2), pixel_color);
                draw_triangle(gpu, v2i(x1, y1), v2i(x2, y2), v2i(x3, y3), pixel_color);
            #else
                g_renderer->draw_quad(commands[0], commands[1], commands[2], commands[3], commands[4]);
            #endif
            }   break;
            case 0x02:
            {
            #if SOFTWARE_RENDERING
                u16 width = (u16)commands[2];
                u16 height = commands[2] >> 16;
                u16 color = color16from24(commands[0]);
                for (u32 i = 0; i < height; ++i)
                {
                    for (u32 j = 0; j < width; ++j)
                    {
                        gpu->vram[j + (1024 * i)] = color;
                    }
                }
            #else
                typedef union
                {
                    u16 pos[2];
                    u32 value;
                } pos2d;
                // YyyyXxxx
                u16 width = (u16)commands[2];
                u16 height = commands[2] >> 16;

                pos2d v1 = {.value = commands[1]};
                pos2d v2 = {.value = commands[1]};
                pos2d v3 = {.value = commands[1]};
                pos2d v4 = {.value = commands[1]};

                v2.pos[0] += width;
                v3.pos[0] += width;
                v3.pos[1] += height;
                v4.pos[1] += height;

                g_renderer->draw_quad(commands[0], v1.value, v2.value, v4.value, v3.value);
            #endif
            }   break;
            case 0xc0:
            {
                // VRAM->CPU copy
                u16 src_x = (u16)commands[1];
                u16 src_y = (commands[1] >> 16);
                u16 width = (u16)commands[2];
                u16 height = (commands[2] >> 16);
                // TODO: masking
                if (!(width | height))
                {
                    width = VRAM_WIDTH;
                    height = VRAM_HEIGHT;
                }

                u32 size = width * height;
                //size += (size & 0x1);
                SY_ASSERT((width & 0x1) == 0); // TODO: remove
                gpu->readback_buffer_len = 0;   
                // NOTE: possibly a command 'flush' here if we want the data right away, but maybe we can rely on bit 27 of GPUSTAT?
            #if !SOFTWARE_RENDERING
                g_renderer->read_vram(gpu->readback_buffer, src_x, src_y, width, height);
            #endif
                struct load_params store = {.x = src_x, .y = src_y, .width = width, .height = height, .pending_halfwords = size, .left = src_x};
                gpu->pending_store = 1;
                gpu->store = store;
                gpu->stat.ready_to_send_vram = 1;
                
            }   break;
            case 0xa0:
            {
                // CPU->VRAM copy
                u16 dst_x = (u16)commands[1];
                u16 dst_y = (commands[1] >> 16);
                u16 width = (u16)commands[2];
                u16 height = (commands[2] >> 16);

                if (!(width | height))
                {
                    width = VRAM_WIDTH;
                    height = VRAM_HEIGHT;
                }

                u32 size = width * height;
            #if SOFTWARE_RENDERING
                SY_ASSERT((width & 0x1) == 0); // TODO: remove
            #endif
                gpu->pending_words = (size + (size & 0x1)) >> 1;
                gpu->pending_load = 1; // we are now in a pending load, don't push values to the command buffer
                struct load_params load = {.x = dst_x, .y = dst_y, .width = width, .height = height, .pending_halfwords = size, .left = dst_x};
                gpu->load = load;
            }   break;
            case 0x80:
            {
                // VRAM->VRAM
                u16 src_x = (u16)commands[1];
                u16 src_y = (commands[1] >> 16);
                u16 dst_x = (u16)commands[2];
                u16 dst_y = (commands[2] >> 16);
                u16 width = (u16)commands[3];
                u16 height = (commands[3] >> 16);

                SY_ASSERT(width && height);
            #if SOFTWARE_RENDERING

            #else
                g_renderer->copy(src_x, src_y, dst_x, dst_y, width, height);
            #endif
            }   break;
            case 0x65:
            {
                
            }   break;
            case 0x38:
            #if SOFTWARE_RENDERING
                draw_shaded_triangle(gpu, color16from24(commands[0]), v2ifromu32(commands[1]), color16from24(commands[2]), v2ifromu32(commands[3]), 
                    color16from24(commands[4]), v2ifromu32(commands[5]));
                draw_shaded_triangle(gpu, color16from24(commands[2]), v2ifromu32(commands[3]), color16from24(commands[4]), v2ifromu32(commands[5]), 
                    color16from24(commands[6]), v2ifromu32(commands[7]));
            #else
                g_renderer->draw_shaded_quad(commands[0], commands[1], commands[2], commands[3], commands[4], commands[5], commands[6], commands[7]);
            #endif
                break;
            case 0x30:
            #if SOFTWARE_RENDERING
                draw_shaded_triangle(gpu, color16from24(commands[0]), v2ifromu32(commands[1]), color16from24(commands[2]), v2ifromu32(commands[3]), 
                    color16from24(commands[4]), v2ifromu32(commands[5]));
            #else
                g_renderer->draw_shaded_triangle(commands[0], commands[1], commands[2], commands[3], commands[4], commands[5]);
            #endif
                break;
            case 0x2c:
            #if SOFTWARE_RENDERING
            #else
                g_renderer->draw_textured_quad(commands[0], commands[1], commands[2], commands[3], commands[4], commands[5], commands[6], commands[7], commands[8]);
            #endif
                break;
            case 0x68:
            #if SOFTWARE_RENDERING
                u32 v1 = commands[1];
                u16 pos_x = v1 & 0xffff;
                u16 pos_y = (v1 >> 16);
                u32 color = commands[0];
                //u16 texel = ((color >> 3) & 0x1f) | ((color >> 8) & 0x3e0) | ((color >> 13) & 0x7c00);
                u8 red = ((color >> 3) & 0x1f);
                u8 green = ((color >> 11) & 0x1f);
                u8 blue = ((color >> 19) & 0x1f);
                u16 texel = (u16)(red << 10) | (u16)(green << 5) | (u16)blue;//((color >> 19) & 0x1f) | ((color >> 11) & 0x1f) | ((color >> 3) & 0x1f);
                ((u16*)gpu->vram)[pos_x + (1024 * pos_y)] = texel;
            #else
                g_renderer->draw_mono_rect(commands[0], commands[1]);
            #endif
                break;
            case 0x2d:
            #if SOFTWARE_RENDERING
                SY_ASSERT(0);
            #else
                g_renderer->draw_raw_textured_quad(commands[0], commands[1], commands[2], commands[3], commands[4], commands[5], commands[6], commands[7], commands[8]);
            #endif
                break;
            default:
                printf("Unknown gp0 command: %x\n", op);
                break;
            }
            //printf("GP0 command: %02xh\n", op);
        }
    }
}