#include "gpu.h"
#include "cpu.h"
#include "counters.h"
#include "event.h"
#include "debug.h"
#include "renderer/renderer.h"
#include "renderer/sw_renderer.h"

struct gpu_state g_gpu;

u16 *g_copy_buffer;
u16 *g_readback_buffer;

static u32 scanline_callback_id;
static s8 dotclks[] = {10, 8, 5, 4};

static void fill_vram(u32 *commands)
{
    u32 xsize = ((commands[2] & 0x3ff) + 0xf) & 0xff0;
    u32 ysize = (commands[2] >> 16) & 0x1ff;
    // TODO: fix
    if ((xsize | ysize) == 0)
        return;

    u32 xpos = commands[1] & 0x3f0;
    u32 ypos = (commands[1] >> 16) & 0x1ff;

    u32 rgb = commands[0];
    u16 color = ((rgb & 0xf8) >> 3) | ((rgb & 0xf800) >> 6) | ((rgb & 0xf80000) >> 9);

    software_renderer *renderer = (software_renderer *)g_renderer;
    u16 *dst = (u16 *)renderer->vram;

    while (ysize--)
    {
        for (u32 x = 0; x < xsize; ++x)
        {
            u32 dst_x = (xpos + x) & 0x3ff;
            dst[dst_x + (ypos * VRAM_WIDTH)] = color;
        }
        ypos = (ypos + 1) & 0x1ff;
    }
}

static void gpu_copy_cpu_to_vram(void)
{
    software_renderer *renderer = (software_renderer *)g_renderer;

    u16 *src = g_copy_buffer;
    u16 *dst = (u16 *)renderer->vram;
    u16 width = g_gpu.load.width;
    u16 height = g_gpu.load.height;
    u16 dst_x;
    u16 dst_y = g_gpu.load.y;

    b8 check_mask = g_gpu.stat.check_mask_before_draw;
    b16 set_mask = g_gpu.stat.set_mask_on_draw << 15;

    while (height--)
    {
        dst_x = g_gpu.load.x;
        for (u32 x = 0; x < width; ++x)
        {
            u32 i = dst_x + (dst_y * VRAM_WIDTH);
            if (check_mask && (dst[i] & 0x8000))
            {
                ++src;
                continue;
            }

            u16 color = *src++;
            color |= set_mask;
            dst[i] = color;

            dst_x = (dst_x + 1) & (VRAM_WIDTH - 1);
        }
        dst_y = (dst_y + 1) & (VRAM_HEIGHT - 1);
    }
    g_gpu.copy_buffer_len = 0;
}

static void gpu_copy_vram_to_vram(struct gpu_transfer *transfer)
{
    software_renderer *renderer = (software_renderer *)g_renderer;
    u16 *vram = (u16 *)renderer->vram;

    u32 width = transfer->width;
    u32 height = transfer->height;
    u32 dx = transfer->dx;
    u32 dy = transfer->dy;
    u32 sx = transfer->sx;
    u32 sy = transfer->sy;

    b8 check_mask = g_gpu.stat.check_mask_before_draw;
    b16 set_mask = g_gpu.stat.set_mask_on_draw << 15;
    
    while (height--)
    {
        for (u32 x = 0; x < width; ++x)
        {
            u32 di = ((dx + x) & 0x3ff) + (dy * VRAM_WIDTH);
            u32 si = ((sx + x) & 0x3ff) + (sy * VRAM_WIDTH);
            u16 color = vram[si];
            if (check_mask && (vram[di] & 0x8000))
                continue;
            color |= set_mask;
            vram[di] = color;
        }
        dy = (dy + 1) & 0x1ff;
        sy = (sy + 1) & 0x1ff;
    }
}

static void gpu_copy_vram_to_cpu(struct gpu_transfer *transfer)
{
    software_renderer *renderer = (software_renderer *)g_renderer;
    u16 *vram = (u16 *)renderer->vram;
    u16 *dst = g_readback_buffer;
    u32 buffer_len = 0;
    u32 width = transfer->width;
    u32 height = transfer->height;
    u32 sx = transfer->sx;
    u32 sy = transfer->sy;

    while (height--)
    {
        for (u32 x = 0; x < width; ++x)
        {
            u32 i = ((sx + x) & 0x3ff) + (sy * VRAM_WIDTH);
            u16 result = vram[i];
            dst[buffer_len++] = result;
        }
        sy = (sy + 1) & 0x1ff;
    }
    
    g_gpu.readback_buffer_len = buffer_len;
}

u32 gpuread(void)
{
    if (g_gpu.pending_store)
    {
        u32 first = (u32)g_readback_buffer[g_gpu.read_index];
        u32 second = (u32)g_readback_buffer[g_gpu.read_index + 1];
        u32 result = first | (second << 16);
        g_gpu.read_index += 2;
        if (g_gpu.read_index >= g_gpu.readback_buffer_len)
        {
            g_gpu.stat.ready_to_send_vram = 0;
            //g_gpu.readback_buffer_len = 0;
            g_gpu.pending_store = false;
        }
        return result;
    }
    else
    {
        return g_gpu.read;
    }
}

static void gpu_softreset(void)
{
    g_gpu.horizontal_display_x1 = 512;
    g_gpu.horizontal_display_x2 = 3072;
    g_gpu.vertical_display_y1 = 16;
    g_gpu.vertical_display_y2 = 256;
    g_gpu.stat.value = 0x14802000;
    g_gpu.dot_div = dotclks[g_gpu.stat.horizontal_res_1];
}

void gpu_reset(struct memory_arena *arena)
{
    // TODO: clear vram
    memset(&g_gpu, 0, sizeof(struct gpu_state));
    gpu_softreset();
    u32 cycles_per_scanline = NTSC_VIDEO_CYCLES_PER_SCANLINE * 451584;
    g_gpu.remainder_cycles = cycles_per_scanline % 715909;
    s32 ticks_for_scanline = cycles_per_scanline / 715090;
    scanline_callback_id = register_callback(gpu_scanline_complete);
    schedule_event(scanline_callback_id, 0, ticks_for_scanline);
    g_copy_buffer = push_arena(arena, VRAM_SIZE);
    g_readback_buffer = push_arena(arena, VRAM_SIZE);
}

void execute_gp1_command(u32 command)
{
    u8 op = command >> 24;

    switch (op)
    {
    case 0x0:
    {
        SY_ASSERT(!g_gpu.pending_words);
        gpu_softreset();
        break;
    }
    case 0x1:
    {
        g_gpu.fifo_len = 0;
        g_gpu.pending_load = 0;
        g_gpu.pending_store = 0;
        g_gpu.pending_words = 0;
        //g_gpu.copy_buffer_len = 0;
        //g_gpu.readback_buffer_len = 0;
        break;
    }
    case 0x2:
    {
        g_gpu.stat.irq = 0;
        break;
    }
    case 0x3:
    {
        g_gpu.stat.display_disabled = (command & 0x1);
        break;
    }
    case 0x4:
    {
        g_gpu.stat.dma_direction = (command & 0x3);
        switch (g_gpu.stat.dma_direction)
        {
        case 0:
            g_gpu.stat.data_request = 0;
            break;
        case 1:
            g_gpu.stat.data_request = (g_gpu.fifo_len != 16);
            break;
        case 2:
            g_gpu.stat.data_request = g_gpu.stat.ready_to_receive_dma;
            break;
        case 3:
            g_gpu.stat.data_request = g_gpu.stat.ready_to_send_vram;
            break;
        }
        break;
    }
    case 0x5:
    {
        g_gpu.vram_display_x = command & 0x3fe; // halfword address
        g_gpu.vram_display_y = (command >> 10) & 0x1ff;
        break;
    }
    case 0x6:
    {
        //gpu_hsync();
        g_gpu.horizontal_display_x1 = command & 0xfff;
        g_gpu.horizontal_display_x2 = (command >> 12) & 0xfff;
        break;
    }
    case 0x7:
    {
        g_gpu.vertical_display_y1 = command & 0x3ff;
        g_gpu.vertical_display_y2 = (command >> 10) & 0x3ff;
        SY_ASSERT(g_gpu.vertical_display_y2 <= 263);
        break;
    }
    case 0x8:
    {
        g_gpu.stat.value &= (~0x7f0000);
        g_gpu.stat.value |= (command & 0x3f) << 17;
        g_gpu.stat.value |= (command & 0x40) << 10;
        if (g_gpu.stat.horizontal_res_2)
        {
            g_gpu.dot_div = 7;
        }
        else 
        {
            g_gpu.dot_div = dotclks[g_gpu.stat.horizontal_res_1];
        }
        break;
    }
    case 0x9:
    {
        //g_gpu.stat.texture_disable = (command & 0x1);
        g_gpu.allow_texture_disable = (command & 0x1);
        break;
    }
    case 0x10:
    {
        u32 register_index = (command & 0x00ffffff);
        switch (register_index)
        {
        case 0x2:
            g_gpu.read = g_gpu.texture_window_bits;
            break;
        case 0x3:
            g_gpu.read = (g_gpu.drawing_area.left & 0x3ff) | (g_gpu.drawing_area.top << 10);
            break;
        case 0x4:
            g_gpu.read = (g_gpu.drawing_area.right & 0x3ff) | (g_gpu.drawing_area.bottom << 10);
            break;
        case 0x5:
            g_gpu.read = (g_gpu.draw_offset_x & 0x7ff) | ((g_gpu.draw_offset_y & 0x7ff) << 11);
            break;
        default:
            debug_error("[GPU] Unhandled GP1 misc command\n");
            break;
        }
        break;
    }
    default:
        debug_error("[GPU] Unknown gp1 command: %x\n", op);
        break;
    }
    //debug_log("[GPU] GP1 command: %02xh\n", op);
}

void execute_gp0_command(u32 word)
{
    if (!g_gpu.pending_words)
    {
        u8 op = word >> 24;
        g_gpu.command_type = (gpu_command_type)(word >> 29);
        switch (g_gpu.command_type)
        {
        case COMMAND_TYPE_MISC:
            switch (op)
            {
            case 0x0:
                return;
            case 0x1:
                g_gpu.pending_words = 1;
                break;
            case 0x2:
                g_gpu.pending_words = 3;
                break;
            default:
                debug_warn("[GPU] Unhandled GP0 MISC: %02xh\n", op);
                break;
            }
            break;
        case COMMAND_TYPE_DRAW_POLYGON:
        {
            u32 vertex_count;
            if (op & POLYGON_FLAG_IS_QUAD)
            {
                g_gpu.pending_words = 4;
                vertex_count = 4;
            }
            else
            {
                g_gpu.pending_words = 3;
                vertex_count = 3;
            }
            if (op & POLYGON_FLAG_TEXTURED)
            {
                g_gpu.pending_words *= 2;
            }
            if (op & POLYGON_FLAG_GOURAUD_SHADED)
            {
                g_gpu.pending_words += (vertex_count - 1); // command includes first color
            }
            ++g_gpu.pending_words;
            break;
        } 
        case COMMAND_TYPE_DRAW_LINE:
        {
            // at minimum, we need 2 vertices
            if (op & LINE_FLAG_GOURAUD_SHADED) 
            {
                g_gpu.pending_words = 4;
            }
            else
            {
                g_gpu.pending_words = 3;
            }
            break;
        }
        case COMMAND_TYPE_DRAW_RECT:
        {
            g_gpu.pending_words = 2;
            if (op & RECT_FLAG_TEXTURED)
            {
                ++g_gpu.pending_words;
            }
            if (!((op >> 3) & 0x3))
            {
                ++g_gpu.pending_words;
            }
            break;
        }
        case COMMAND_TYPE_VRAM_TO_VRAM:
            g_gpu.pending_words = 4;
            break;
        case COMMAND_TYPE_CPU_TO_VRAM:
            g_gpu.pending_words = 3;
            break;
        case COMMAND_TYPE_VRAM_TO_CPU:
            g_gpu.pending_words = 3;
            break;
        case COMMAND_TYPE_ENV:
            g_gpu.pending_words = 1;
            break;
        default:
            debug_log("[GPU] Unimplemented gp0 command: %x\n", op);
            break;
        }
        g_gpu.fifo_len = 0;
    }

    --g_gpu.pending_words;

    if (g_gpu.pending_load)
    {
        g_copy_buffer[g_gpu.copy_buffer_len++] = (u16)word;
        g_copy_buffer[g_gpu.copy_buffer_len++] = (u16)(word >> 16);
        
        if (!g_gpu.pending_words)
        {
            g_gpu.pending_load = false;
            gpu_copy_cpu_to_vram();
        }
    }
    else
    {
        SY_ASSERT(g_gpu.fifo_len < 16);
        g_gpu.fifo[g_gpu.fifo_len++] = word;
        if (!g_gpu.pending_words)
        {
            u32 *commands = g_gpu.fifo;
            u32 op = commands[0] >> 24;

            switch (g_gpu.command_type)
            {
            case COMMAND_TYPE_MISC:
            {
                switch (op)
                {
                case 0x1:
                    NoImplementation;
                    break;
                case 0x2:
                    fill_vram(commands);
                    break;
                INVALID_CASE;
                }
                break;
            }
            case COMMAND_TYPE_DRAW_POLYGON:
            {
                // NOTE: textured polygons can change bits in gpustat, not sure if it only changes when the command begins execution.. but we pass the tests for now :p
                if (op & POLYGON_FLAG_TEXTURED)
                {
                    u16 texpage_attribute = (u16)((commands[op & POLYGON_FLAG_GOURAUD_SHADED ? 5 : 4]) >> 16);
                    g_gpu.stat.value &= 0xffff7e00;
                    g_gpu.stat.value |= (texpage_attribute & 0x1ff);
                    if (g_gpu.allow_texture_disable)
                    {
                        g_gpu.stat.value |= ((texpage_attribute & 0x800) << 4);
                    }
                }

                draw_polygon(commands, op);
                break;
            }
            case COMMAND_TYPE_DRAW_LINE:
            {
                if (op & LINE_FLAG_POLYLINE)
                {
                    // terminator flag
                    if ((word & 0xf000f000) != 0x50005000)
                    {
                        if (op & LINE_FLAG_GOURAUD_SHADED)
                        {
                            g_gpu.pending_words = 2;
                        }
                        else
                        {
                            g_gpu.pending_words = 1;
                        }
                        g_gpu.fifo_len = 1; // truncate fifo to only contain the command word
                    }
                }
                break;
            }
            case COMMAND_TYPE_DRAW_RECT:
            {
                draw_rectangle(commands, op);
                break;
            }
            case COMMAND_TYPE_VRAM_TO_VRAM:
            {
                struct gpu_transfer transfer;
                transfer.sx = commands[1] & 0x3ff;
                transfer.sy = (commands[1] >> 16) & 0x1ff;
                transfer.dx = commands[2] & 0x3ff;
                transfer.dy = (commands[2] >> 16) & 0x1ff;
                u32 width = commands[3] & 0xffff;
                u32 height = (commands[3] >> 16);
                transfer.width = ((width - 1) & 0x3ff) + 1;
                transfer.height = ((height - 1) & 0x1ff) + 1;

                gpu_copy_vram_to_vram(&transfer);
                break;
            }
            case COMMAND_TYPE_CPU_TO_VRAM:
            {
                u16 dst_x = commands[1] & 0x3ff;
                u16 dst_y = (commands[1] >> 16) & 0x1ff;
                u16 width = commands[2] & 0xffff;
                u16 height = (commands[2] >> 16);
                width = ((width - 1) & 0x3ff) + 1;
                height = ((height - 1) & 0x1ff) + 1;

                u32 size = width * height;

                g_gpu.pending_words = (size + (size & 0x1)) >> 1;
                g_gpu.pending_load = true; // we are now in a pending load, don't push values to the command buffer
                g_gpu.load.x = dst_x;
                g_gpu.load.y = dst_y;
                g_gpu.load.width = width;
                g_gpu.load.height = height;
                //printf("[CPU->VRAM] dst x: %d, y: %d | w: %d, h: %d\n", dst_x, dst_y, width, height);
                break;
            }
            case COMMAND_TYPE_VRAM_TO_CPU:
            {
                struct gpu_transfer transfer;
                transfer.sx = commands[1] & 0x3ff;
                transfer.sy = (commands[1] >> 16) & 0x1ff;
                u16 width = commands[2] & 0xffff;
                u16 height = (commands[2] >> 16);
                transfer.width = ((width - 1) & 0x3ff) + 1;
                transfer.height = ((height - 1) & 0x1ff) + 1;

                g_gpu.read_index = 0;
                gpu_copy_vram_to_cpu(&transfer);
                
                g_gpu.pending_store = true;
                g_gpu.stat.ready_to_send_vram = 1;
                //debug_log("[VRAM->CPU] src x: %d, y: %d | w: %d, h: %d\n", src_x, src_y, width, height);
                break;
            }
            case COMMAND_TYPE_ENV:
            {
                SY_ASSERT(g_gpu.fifo_len == 1);
                switch (commands[0] >> 24)
                {
                case 0xe1:
                    g_gpu.stat.value &= 0xffff7800;
                    g_gpu.stat.value |= (commands[0] & 0x7ff);
                    g_gpu.stat.value |= g_gpu.allow_texture_disable ? ((commands[0] & 0x800) << 4) : 0x0;
                    //debug_log("GP0: 0xE1\n");
                    break;
                case 0xe2:
                    g_gpu.texture_window_bits = commands[0] & 0xfffff;

                    u32 texture_window_mask_x = commands[0] & 0x1f;
                    u32 texture_window_mask_y = (commands[0] >> 5) & 0x1f;
                    u32 texture_window_offset_x = (commands[0] >> 10) & 0x1f;
                    u32 texture_window_offset_y = (commands[0] >> 15) & 0x1f;

                    g_gpu.texture_window_premask_x = ~(texture_window_mask_x * 8);
                    g_gpu.texture_window_premask_y = ~(texture_window_mask_y * 8);
                    g_gpu.texture_window_postmask_x = (texture_window_offset_x & texture_window_mask_x) * 8;
                    g_gpu.texture_window_postmask_y = (texture_window_offset_y & texture_window_mask_y) * 8;
                    break;
                case 0xe3:
                    // NOTE: v2 gpu has 10 bits for top
                    g_gpu.drawing_area.left = (commands[0] & 0x3ff);
                    g_gpu.drawing_area.top = ((commands[0] >> 10) & 0x3ff);
                    break;
                case 0xe4:
                    g_gpu.drawing_area.right = (commands[0] & 0x3ff);
                    g_gpu.drawing_area.bottom = ((commands[0] >> 10) & 0x3ff);
                    break;
                case 0xe5:
                    g_gpu.draw_offset_x = SIGN_EXTEND32(11, commands[0] & 0x7ff);
                    g_gpu.draw_offset_y = SIGN_EXTEND32(11, commands[0] >> 11);
                    break;
                case 0xe6:
                    g_gpu.stat.set_mask_on_draw = (commands[0] & 0x1);
                    g_gpu.stat.check_mask_before_draw = (commands[0] >> 1) & 0x1;
                    break;
                INVALID_CASE;
                }
                break;
            }
            INVALID_CASE;
            }
        }
    }
}

void tick_hblank_timer(void)
{
    struct root_counter *counter1 = &g_counters[1];
#if 0
    if (counter1->mode.clock_source & 0x1)
    {
        b8 paused = false;
        if (counter1->mode.sync_enable)
        {
            switch (counter1->mode.sync_mode)
            {
            case 0:
                paused = in_vblank();
                break;
            case 1:
                if (g_gpu.scanline == g_gpu.vertical_display_y1 || g_gpu.scanline == g_gpu.vertical_display_y2)
                    counter1->value = 0;
                break;
            case 2:
                if (g_gpu.scanline == g_gpu.vertical_display_y1 || g_gpu.scanline == g_gpu.vertical_display_y2)
                    counter1->value = 0;
                paused = !in_vblank();
                break;
            case 3:
                if (!counter1->gate)
                {
                    paused = true;
                    if (g_gpu.scanline == g_gpu.vertical_display_y2)
                        counter1->gate = true;
                }
                break;
            }
        }
        if (!paused)
            ++counter1->value;
    }
#else
    if (counter1->mode.sync_enable)
    {
        switch (counter1->mode.sync_mode)
        {
        case 0:
            if (g_gpu.scanline == g_gpu.vertical_display_y2)
                counter1->timestamp = g_cycles_elapsed;
            else if (g_gpu.scanline == g_gpu.vertical_display_y1)
                counter1->pause_ticks += safe_truncate32(g_cycles_elapsed - counter1->timestamp);
            break;
        case 1:
            if (g_gpu.scanline == g_gpu.vertical_display_y1 || g_gpu.scanline == g_gpu.vertical_display_y2)
            {
                counter1->timestamp = g_cycles_elapsed;
                counter1->value = 0;
            }
            break;
        case 2:
            if (g_gpu.scanline == g_gpu.vertical_display_y1)
            {
                counter1->value = 0;
            }
            else if (g_gpu.scanline == g_gpu.vertical_display_y2)
            {
                counter1->timestamp = g_cycles_elapsed;
                counter1->value = 0;
            }
            break;
        case 3:
            if (!counter1->gate)
            {
                if (g_gpu.scanline == g_gpu.vertical_display_y2)
                {
                    counter1->begin_ticks = g_cycles_elapsed;
                    counter1->gate = true;
                }
            }
            break;
        }
    }
#endif
}

void gpu_scanline_complete(u32 param, s32 ticks_late)
{
    ++g_gpu.scanline;
    if (g_gpu.scanline == 263) 
    {
        g_gpu.scanline = 0;
    }
    
    if (in_vblank() || !g_gpu.stat.vertical_interlace)
    {
        g_gpu.stat.odd_line = 0;
    }
    else if (!g_gpu.stat.vertical_res)
    {
        g_gpu.stat.odd_line = g_gpu.scanline & 0x1; // in 240 lines mode, bit 31 changes per scanline
    }
    else
    {
        g_gpu.stat.odd_line = g_vblank_counter & 0x1; // in 480 lines mode, bit 31 changes per frame
    }
    
    if (g_gpu.scanline == g_gpu.vertical_display_y2)
    {
        // vblank begins
        update_vram();
        
        ++g_vblank_counter;
        g_cpu.i_stat |= INTERRUPT_VBLANK;
    }

    tick_hblank_timer();

    u32 cycles_per_scanline = NTSC_VIDEO_CYCLES_PER_SCANLINE * 451584;
    cycles_per_scanline += g_gpu.remainder_cycles;
    g_gpu.remainder_cycles = cycles_per_scanline % 715909;
    s32 ticks_for_scanline = cycles_per_scanline / 715090;
    schedule_event(scanline_callback_id, 0, ticks_for_scanline - ticks_late);
}
