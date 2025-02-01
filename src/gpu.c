#include "gpu.h"
#include "cpu.h"
#include "counters.h"
#include "event.h"
#include "debug.h"
#include "renderer/renderer.h"
#include "renderer/sw_renderer.h"

struct gpu_state g_gpu;
static s32 dotclks[] = {10, 8, 5, 4};

static inline void fill_vram(u32 *commands)
{
    u16 xsize = ((commands[2] & 0x3ff) + 0xf) & 0xff0;
    u16 ysize = (commands[2] >> 16) & 0x1ff;

    if ((xsize | ysize) == 0) {
        return;
    }

    u16 xpos = commands[1] & 0x3f0;
    u16 ypos = (commands[1] >> 16) & 0x1ff;

    u32 rgb = commands[0];
    u16 color = ((rgb & 0xf8) >> 3) | ((rgb & 0xf800) >> 6) | ((rgb & 0xf80000) >> 8);

    software_renderer *renderer = (software_renderer *)g_renderer;

    u16 *dst = (u16 *)renderer->vram + (xpos + (VRAM_WIDTH * ypos));
    // TODO: wrapping
    while (ysize--) 
    {
        for (u32 i = 0; i < xsize; ++i) {
            dst[i] = color;
        }
        dst += VRAM_WIDTH;
    }
}

static inline void copy_cpu_to_vram(void)
{
    software_renderer *renderer = (software_renderer *)g_renderer;

    u16 *at = g_gpu.copy_buffer;
    u16 *dst = (u16 *)renderer->vram + (g_gpu.load.x + (VRAM_WIDTH * g_gpu.load.y));
    u16 width = g_gpu.load.width;
    // TODO: wrapping
    for (u32 i = 0; i < g_gpu.load.height; ++i)
    {
        memcpy(dst, at, width * 2);
        dst += VRAM_WIDTH;
        at += width;
    }

    g_gpu.copy_buffer_len = 0;
}

static inline void reset_gpu_draw_state(void)
{
    g_gpu.copy_buffer_len = 0;
    g_gpu.draw_area_changed = true;
}

// NOTE: not sure how this works, it says if certain gp1 commands are issued, then they are immediately read from GPUREAD,
// does this mean they interrupt VRAM->CPU transfers and place themselves ahead of the data to be read?
u32 gpuread(void)
{
    if (g_gpu.pending_store)
    {
        if (g_gpu.software_rendering)
        {
            software_renderer *renderer = (software_renderer *)g_renderer;
            u16 *vram = renderer->vram;
            // TODO: assumes even number of pixels
            if (g_gpu.store.x > (g_gpu.store.left + g_gpu.store.width))
            {
                g_gpu.store.x = g_gpu.store.left;
                ++g_gpu.store.y;
            }
            u32 index = (g_gpu.store.y * 1024) + g_gpu.store.x;
            u32 word = vram[index] | (((u32)vram[index + 1]) << 16);
            g_gpu.store.x += 2;
            return word;
        }
        else
        {
            u32 first_pixel = g_gpu.readback_buffer[g_gpu.readback_buffer_len++];
            u32 second_pixel = g_gpu.readback_buffer[g_gpu.readback_buffer_len++];
            u32 word = (first_pixel & 0xffff) | (second_pixel << 16);
            if (g_gpu.readback_buffer_len == g_gpu.store.pending_halfwords)
            {
                g_gpu.stat.ready_to_send_vram = 0;
                g_gpu.readback_buffer_len = 0;
                g_gpu.pending_store = 0;
            }
            return word;
        }
    }
    else
    {
        return g_gpu.read;
    }
}

void gpu_reset(void)
{
    g_gpu.fifo_len = 0;
    g_gpu.pending_load = 0;
    g_gpu.pending_store = 0;
    g_gpu.pending_words = 0;
    g_gpu.vram_display_x = 0;
    g_gpu.vram_display_y = 0;
    g_gpu.horizontal_display_x1 = 512;
    g_gpu.horizontal_display_x2 = 3072;
    g_gpu.vertical_display_y1 = 16;
    g_gpu.vertical_display_y2 = 256;
    //g_gpu.copy_buffer_len = 0;
    //g_gpu.readback_buffer_len = 0;
    g_gpu.stat.value = 0x14802000;
    g_gpu.dot_div = dotclks[g_gpu.stat.horizontal_res_1];
}

void execute_gp1_command(u32 command)
{
    u8 op = command >> 24;

    switch (op)
    {
    case 0x0:
    {
        SY_ASSERT(!g_gpu.pending_words);
        gpu_reset();
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
        g_gpu.stat.data_request = (command & 0x3);
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
        gpu_hsync();
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
    default:
    {
        debug_log("Unknown gp1 command: %x\n", op);
        break;
    }
    }
#if LOG_GP1_COMMANDS
    debug_log("GP1 command: %02xh\n", op);
#endif
}
#include <time.h>
void execute_gp0_command(u32 word)
{
    if (!g_gpu.pending_words) // get number of remaining words in cmd (includes command itself)
    {
        u8 op = word >> 24;
        g_gpu.command_type = (enum gpu_command_type)(word >> 29);
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
                // TODO: masking
                g_gpu.pending_words = 3;
                break;
            default:
                debug_log("Unhandled GP0 MISC: %02xh\n", op);
                break;
            }
            break;
        case COMMAND_TYPE_DRAW_POLYGON:
        {
            // render polygon
            g_gpu.render_flags = op;
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
        } 
        break;
        case COMMAND_TYPE_DRAW_LINE:
        {
            g_gpu.render_flags = op;
            /* at minimum, we need 2 vertices */
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
            // render rectangle
            g_gpu.render_flags = op;
            g_gpu.pending_words = 2;
            if (op & RECT_FLAG_TEXTURED)
            {
                ++g_gpu.pending_words;
            }
            if (!((op >> 3) & 0x3)) // TODO: change branch cond
            {
                ++g_gpu.pending_words;
            }
        }
        break;
        case COMMAND_TYPE_VRAM_TO_VRAM:
        {
            // vram -> vram
            g_gpu.pending_words = 4;
        }
        break;
        case COMMAND_TYPE_CPU_TO_VRAM:
        {
            // cpu -> vram
            g_gpu.pending_words = 3;
        }
        break;
        case COMMAND_TYPE_VRAM_TO_CPU:
        {
            // vram -> cpu
            g_gpu.pending_words = 3;
        }
        break;
        case COMMAND_TYPE_ENV:
        {
            // environment
            g_gpu.pending_words = 1;
        }
        break;
        default: // TODO: remove
            debug_log("Unimplemented gp0 command: %x\n", op);
            break;
        }
        #if LOG_GP0_COMMANDS
        debug_log("gp0: %02x\n", op);
        #endif
        g_gpu.fifo_len = 0;
    }

    --g_gpu.pending_words;

    if (g_gpu.pending_load)
    {
        g_gpu.copy_buffer[g_gpu.copy_buffer_len++] = (u16)word;
        g_gpu.copy_buffer[g_gpu.copy_buffer_len++] = (u16)(word >> 16);
        
        if (!g_gpu.pending_words)
        {
            g_gpu.pending_load = false;
            if (g_gpu.software_rendering)
                copy_cpu_to_vram();
            else
                push_cpu_to_vram_copy((void **)g_gpu.copy_buffer_at, g_gpu.load.x, g_gpu.load.y, g_gpu.load.width, g_gpu.load.height);
            //g_gpu.copy_buffer_len = 0;
        }
    }
    else
    {
        SY_ASSERT(g_gpu.fifo_len < 16);
        g_gpu.fifo[g_gpu.fifo_len++] = word;
        if (!g_gpu.pending_words)
        {
            if (g_debug.log_gpu_commands)
            {
                struct debug_gpu_command *cmd = &g_debug.gpu_commands[g_debug.gpu_commands_len++];
                cmd->type = g_gpu.command_type;
                memcpy(cmd->params, g_gpu.fifo, 64);
            }

            u32 *commands = g_gpu.fifo;
            switch (g_gpu.command_type)
            {
            case COMMAND_TYPE_MISC:
            {
                switch (commands[0] >> 24)
                {
                case 0x1:
                    NoImplementation;
                    break;
                case 0x2:
                    if (g_gpu.software_rendering)
                    {
                        fill_vram(commands);
                    }
                    else
                    {
                        // TODO: masking for hw renderer
                        if (g_gpu.draw_area_changed)
                        {
                            push_draw_area(g_gpu.drawing_area);
                            g_gpu.draw_area_changed = false;
                        }
                        push_rect(commands, 0, 0, v2f(0, 0));
                    }
                    break;
                INVALID_CASE;
                }
            }
            break;
            case COMMAND_TYPE_DRAW_POLYGON:
            {
                // NOTE: textured polygons can change bits in gpustat, not sure if it only changes when the command begins execution.. but we pass the tests for now :p
                if (g_gpu.render_flags & POLYGON_FLAG_TEXTURED)
                {
                    u16 texpage_attribute = (u16)((commands[g_gpu.render_flags & POLYGON_FLAG_GOURAUD_SHADED ? 5 : 4]) >> 16);
                    g_gpu.stat.value &= 0xffff7e00;
                    g_gpu.stat.value |= (texpage_attribute & 0x1ff);
                    if (g_gpu.allow_texture_disable) {
                        g_gpu.stat.value |= ((texpage_attribute & 0x800) << 4);
                    }
                }

                if (g_gpu.software_rendering)
                {
                    draw_polygon(commands, g_gpu.render_flags, v2i(g_gpu.draw_offset_x, g_gpu.draw_offset_y), g_gpu.drawing_area);
                }
                else
                {
                    if (g_gpu.draw_area_changed)
                    {
                        push_draw_area(g_gpu.drawing_area);
                        g_gpu.draw_area_changed = false;
                    }
                    push_polygon(commands, g_gpu.render_flags, v2f(g_gpu.draw_offset_x, g_gpu.draw_offset_y));
                }
            }
            break;
            case COMMAND_TYPE_DRAW_LINE:
            {
                if (g_gpu.render_flags & LINE_FLAG_POLYLINE)
                {
                    if ((word & 0xf000f000) == 0x50005000) // terminator flag
                    {

                    }
                    else if (g_gpu.render_flags & LINE_FLAG_GOURAUD_SHADED) 
                    {
                        g_gpu.pending_words = 4;
                    }
                    else
                    {
                        g_gpu.pending_words = 3;
                    }
                }

                break;
            }
            case COMMAND_TYPE_DRAW_RECT:
            {
                if (g_gpu.software_rendering)
                {
                    draw_rectangle(commands, g_gpu.render_flags, (g_gpu.stat.value & 0x7ff), v2i(g_gpu.draw_offset_x, g_gpu.draw_offset_y), g_gpu.drawing_area);
                }
                else
                {
                    if (g_gpu.draw_area_changed)
                    {
                        push_draw_area(g_gpu.drawing_area);
                        g_gpu.draw_area_changed = false;
                    }
                    //push_draw_area(g_gpu.renderer, g_gpu.drawing_area);
                    push_rect(commands, g_gpu.render_flags, (g_gpu.stat.value & 0x7ff), v2f(g_gpu.draw_offset_x, g_gpu.draw_offset_y));
                }
            }
            break;
            case COMMAND_TYPE_VRAM_TO_VRAM:
            {
                u16 src_x = (u16)commands[1];
                u16 src_y = (u16)(commands[1] >> 16);
                u16 dst_x = (u16)commands[2];
                u16 dst_y = (u16)(commands[2] >> 16);
                u16 width = (u16)commands[3];
                u16 height = (u16)(commands[3] >> 16);

                SY_ASSERT(width && height);
                if (g_gpu.software_rendering) {
                    SY_ASSERT(0);
                }
                else {
                    push_vram_copy(src_x, src_y, dst_x, dst_y, width, height);
                }
            }
            break;
            case COMMAND_TYPE_CPU_TO_VRAM:
            {
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

                g_gpu.pending_words = (size + (size & 0x1)) >> 1;
                g_gpu.pending_load = true; // we are now in a pending load, don't push values to the command buffer
                g_gpu.copy_buffer_at = g_gpu.copy_buffer + g_gpu.copy_buffer_len;
                g_gpu.load.x = dst_x;
                g_gpu.load.y = dst_y;
                g_gpu.load.width = width;
                g_gpu.load.height = height;
                g_gpu.load.pending_halfwords = size;
                g_gpu.load.left = dst_x;
                //debug_log("[CPU->VRAM] dst x: %d, y: %d | w: %d, h: %d\n", dst_x, dst_y, width, height);
            }
            break;
            case COMMAND_TYPE_VRAM_TO_CPU:
            {
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
                g_gpu.readback_buffer_len = 0;
                // NOTE: since we return the data right away we flush the commands, but maybe we can rely on bit 27 of GPUSTAT?
                if (g_gpu.software_rendering) {
                    //SY_ASSERT(0);
                }
                else {
                    push_vram_to_cpu_copy((void **)&g_gpu.readback_buffer, src_x, src_y, width, height);
                    hardware_renderer *renderer = (hardware_renderer *)g_renderer;
                    renderer->flush_commands();
                }
                reset_gpu_draw_state();
                g_gpu.pending_store = 1;
                struct load_params store = {.x = src_x, .y = src_y, .width = width, .height = height, .pending_halfwords = size, .left = src_x};
                g_gpu.store = store;
                g_gpu.stat.ready_to_send_vram = 1;
                //debug_log("[VRAM->CPU] src x: %d, y: %d | w: %d, h: %d\n", src_x, src_y, width, height);
            }
            break;
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
                    g_gpu.texture_window_mask_x = commands[0] & 0x1f;
                    g_gpu.texture_window_mask_y = (commands[0] >> 5) & 0x1f;
                    g_gpu.texture_window_offset_x = (commands[0] >> 10) & 0x1f;
                    g_gpu.texture_window_offset_y = (commands[0] >> 15) & 0x1f;
                    break;
                case 0xe3:
                    g_gpu.drawing_area.left = (commands[0] & 0x3ff);
                    g_gpu.drawing_area.top = ((commands[0] >> 10) & 0x3ff);
                    g_gpu.draw_area_changed = true;
                    //debug_log("GP0 draw area: left -> %d, top -> %d\n", g_gpu.drawing_area.left, g_gpu.drawing_area.top);
                    //push_draw_area(g_gpu.renderer, g_gpu.drawing_area);
                    break;
                case 0xe4:
                    g_gpu.drawing_area.right = (commands[0] & 0x3ff);
                    g_gpu.drawing_area.bottom = ((commands[0] >> 10) & 0x3ff);
                    g_gpu.draw_area_changed = true;

                    //debug_log("GP0 draw area: right -> %d, bottom -> %d\n", g_gpu.drawing_area.right, g_gpu.drawing_area.bottom);
                    //push_draw_area(g_gpu.renderer, g_gpu.drawing_area);
                    break;
                case 0xe5:
                    s16 draw_offset_x = ((s16)commands[0] & 0x7ff) << 5;
                    draw_offset_x >>= 5;
                    g_gpu.draw_offset_x = draw_offset_x;
                    s16 draw_offset_y = ((s16)(commands[0] >> 11) & 0x7ff) << 5;
                    draw_offset_y >>= 5;
                    g_gpu.draw_offset_y = draw_offset_y;
                    //debug_log("GP0 draw offset: X -> %d, Y -> %d\n", g_gpu.draw_offset_x, g_gpu.draw_offset_y);
                    break;
                case 0xe6:
                    g_gpu.stat.set_mask_on_draw = (commands[0] & 0x1);
                    g_gpu.stat.draw_to_masked = (commands[0] >> 1) & 0x1;
                    SY_ASSERT(!(g_gpu.stat.set_mask_on_draw | g_gpu.stat.draw_to_masked));
                    break;
                INVALID_CASE;
                }
            }
            break;
            INVALID_CASE;
            }
        }
    }
}

void gpu_scanline_complete(u32 param, s32 cycles_late)
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
    else if (!g_gpu.stat.vertical_res) // in 240 lines mode, bit 31 changes per scanline
    {
        g_gpu.stat.odd_line = g_gpu.scanline & 0x1;
    }
    else // in 480 lines mode, bit 31 changes per frame
    {
        g_gpu.stat.odd_line = g_vblank_counter & 0x1;
    }
    
    if (g_gpu.scanline == g_gpu.vertical_display_y1)
    {
        // end of vblank
#if 1
        struct root_counter *counter1 = &g_counters[1];
        
        if (counter1->mode.sync_enable)
        {
            switch (counter1->mode.sync_mode)
            {
            case 0:
                counter1->pause_ticks += g_cycles_elapsed - counter1->timestamp;
                break;
            case 1:
                break;
            case 2:
                //counter1->paused = 1;

                break;
            case 3:
                break;
            }
        }
#endif
    }
    else if (g_gpu.scanline == g_gpu.vertical_display_y2)
    {
        // vblank begins
        struct root_counter *counter1 = &g_counters[1];
        if (counter1->mode.sync_enable)
        {
            switch (counter1->mode.sync_mode)
            {
            case 0: // pause during vblank
                counter1->timestamp = g_cycles_elapsed;
                break;
            case 1: // reset to 0 at vblank
                counter1->prev_cycle_count = g_cycles_elapsed;
                counter1->value = 0;
                break;
            case 2: // reset to 0 at vblank and pause outside of it
                counter1->prev_cycle_count = g_cycles_elapsed;
                counter1->value = 0;
                //timer1->paused = 0;
                break;
            case 3: // pause until vblank occurs, then free run
                //timer1->paused = 0;
                counter1->prev_cycle_count = g_cycles_elapsed;
                break;
            }
            counter1->sync = 1;
        }

        // TODO: debug gpu log

        if (!g_gpu.software_rendering) {
            hardware_renderer *renderer = (hardware_renderer *)g_renderer;
            renderer->flush_commands();
        }

        if (g_gpu.enable_output)
            g_renderer->update_display();
            
        reset_gpu_draw_state();

        ++g_vblank_counter;
        g_cpu.i_stat |= INTERRUPT_VBLANK;
        platform_set_event(&g_present_ready);
    }
    s32 cycles_until_event = (s32)video_to_cpu_cycles(NTSC_VIDEO_CYCLES_PER_SCANLINE) - cycles_late;
    schedule_event(gpu_scanline_complete, 0, cycles_until_event);
}

void gpu_hsync(void) /* advance gpu cycle count to determine horizontal timings */
{
    struct root_counter *counter0 = &g_counters[0];
    
    if (g_gpu.horizontal_display_x2 == 0)
        return;
    
    //u64 hblanks = 0;
    u64 elapsed = g_cycles_elapsed - g_gpu.timestamp;
#if 1
    /* NTSC video clock is 53.693175 MHz */
    elapsed *= 715909;
    elapsed += g_gpu.remainder_cycles;
    u64 video_cycles = elapsed / 451584;
    g_gpu.remainder_cycles = elapsed % 451584;
#else
    u64 video_cycles = cpu_to_video_cycles(elapsed);
#endif
    u32 remaining_ticks = NTSC_VIDEO_CYCLES_PER_SCANLINE - g_gpu.scanline_cycles; /* ticks remaining before adding cycles */
    u32 current_ticks = (u32)((g_gpu.scanline_cycles + video_cycles) % NTSC_VIDEO_CYCLES_PER_SCANLINE); /* current video cycles in scanline */
    u32 hblank_width = g_gpu.horizontal_display_x2 - g_gpu.horizontal_display_x1;
    
    u64 hblank_cycles = 0;
    
    if (g_gpu.scanline_cycles >= g_gpu.horizontal_display_x2)
    {
        if (video_cycles >= remaining_ticks)
        {
            // TODO: broken code, in test case, hblank_cycles should have been 188, became 137?
            // not accounting for the remaining ticks to x1
            video_cycles -= remaining_ticks;
            hblank_cycles += remaining_ticks;

            u32 hblanks = (u32)(video_cycles / g_gpu.horizontal_display_x2);
            g_gpu.hblanks += hblanks;

            hblank_cycles +=  hblank_width * hblanks;

            if (current_ticks >= g_gpu.horizontal_display_x2)
            {
                hblank_cycles -= NTSC_VIDEO_CYCLES_PER_SCANLINE - current_ticks;
            }
            else
            {
                hblank_cycles += MIN(g_gpu.horizontal_display_x1, current_ticks);
            }
        }
        else
        {
            hblank_cycles = video_cycles;
        }
    }
    else
    {
        u32 ticks_to_hblank = g_gpu.horizontal_display_x2 - g_gpu.scanline_cycles;
        if (video_cycles >= ticks_to_hblank)
        {
            u32 hblanks = 1;
            video_cycles -= ticks_to_hblank;
            u32 ticks_left = NTSC_VIDEO_CYCLES_PER_SCANLINE - g_gpu.horizontal_display_x2;
            if (video_cycles >= remaining_ticks)
            {
                video_cycles -= remaining_ticks;
                hblank_cycles += remaining_ticks;

                hblanks += (u32)(video_cycles / g_gpu.horizontal_display_x2);
                hblank_cycles += hblank_width * hblanks;

                if (current_ticks >= g_gpu.horizontal_display_x2)
                {
                    hblank_cycles -= NTSC_VIDEO_CYCLES_PER_SCANLINE - current_ticks;
                }
                else
                {
                    hblank_cycles += MIN(g_gpu.horizontal_display_x1, current_ticks);
                }
            }
            else
            {
                hblank_cycles = video_cycles;
            }
            g_gpu.hblanks += hblanks;
        }
    }
    u64 result = hblank_cycles * 451584;
    result += g_gpu.remainder_cycles;
    u64 ticks = result / 715909;
    counter0->pause_ticks += ticks;//video_to_cpu_cycles(hblank_cycles);

    if (counter0->mode.sync_enable)
    {
        switch (counter0->mode.sync_mode)
        {
        case 1:
            if (current_ticks >= g_gpu.horizontal_display_x2)
            {
                counter0->value = video_to_cpu_cycles(current_ticks - g_gpu.horizontal_display_x2);
            }
            else if (current_ticks < g_gpu.horizontal_display_x1)
            {
                counter0->value = video_to_cpu_cycles((3413 - g_gpu.horizontal_display_x2) + current_ticks);
            }
            else
            {
                counter0->value = video_to_cpu_cycles(current_ticks - g_gpu.horizontal_display_x1);
            }
            break;
        case 2:
            break;
        case 3:
            break;
        }
    }
#if 0
    if (counter0->mode.sync_enable)
    {
        switch (counter0->mode.sync_mode)
        {
        case 0:
        {
            u64 hblank_cycles = 0;
            
            if (g_gpu.scanline_cycles >= g_gpu.horizontal_display_x2)
            {
                if (video_cycles >= remaining_ticks)
                {
                    // TODO: broken code, in test case, hblank_cycles should have been 188, became 137?
                    // not accounting for the remaining ticks to x1
                    video_cycles -= remaining_ticks;
                    hblank_cycles += remaining_ticks;

                    u32 hblanks = (u32)(video_cycles / g_gpu.horizontal_display_x2);
                    g_gpu.hblanks += hblanks;

                    hblank_cycles +=  hblank_width * hblanks;

                    if (current_ticks >= g_gpu.horizontal_display_x2)
                    {
                        hblank_cycles -= NTSC_VIDEO_CYCLES_PER_SCANLINE - current_ticks;
                    }
                    else
                    {
                        hblank_cycles += MIN(g_gpu.horizontal_display_x1, current_ticks);
                    }
                }
                else
                {
                    hblank_cycles = video_cycles;
                }
            }
            else
            {
                u32 ticks_to_hblank = g_gpu.horizontal_display_x2 - g_gpu.scanline_cycles;
                if (video_cycles >= ticks_to_hblank)
                {
                    u32 hblanks = 1;
                    video_cycles -= ticks_to_hblank;
                    u32 ticks_left = NTSC_VIDEO_CYCLES_PER_SCANLINE - g_gpu.horizontal_display_x2;
                    if (video_cycles >= remaining_ticks)
                    {
                        video_cycles -= remaining_ticks;
                        hblank_cycles += remaining_ticks;

                        hblanks += (u32)(video_cycles / g_gpu.horizontal_display_x2);
                        hblank_cycles += hblank_width * hblanks;

                        if (current_ticks >= g_gpu.horizontal_display_x2)
                        {
                            hblank_cycles -= NTSC_VIDEO_CYCLES_PER_SCANLINE - current_ticks;
                        }
                        else
                        {
                            hblank_cycles += MIN(g_gpu.horizontal_display_x1, current_ticks);
                        }
                    }
                    else
                    {
                        hblank_cycles = video_cycles;
                    }
                    g_gpu.hblanks += hblanks;
                }
            }
            u64 result = hblank_cycles * 451584;
            result += g_gpu.remainder_cycles;
            u64 ticks = result / 715909;
            counter0->pause_ticks += ticks;//video_to_cpu_cycles(hblank_cycles);

            break;
        }
        case 1:
        {
            if (current_ticks >= g_gpu.horizontal_display_x2)
            {
                counter0->value = video_to_cpu_cycles(current_ticks - g_gpu.horizontal_display_x2);
            }
            else if (current_ticks < g_gpu.horizontal_display_x1)
            {
                counter0->value = video_to_cpu_cycles((3413 - g_gpu.horizontal_display_x2) + current_ticks);
            }
            else
            {
                counter0->value = video_to_cpu_cycles(current_ticks - g_gpu.horizontal_display_x1);
            }
            break;
        }
        case 2:
            break;
        case 3:
            break;
        }
    }
#endif
    g_gpu.timestamp = g_cycles_elapsed;
    g_gpu.scanline_cycles = current_ticks;
    g_gpu.video_cycles += video_cycles;
}
