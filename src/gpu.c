#include "gpu.h"
#include "psx.h"
#include "cpu.h"
#include "timers.h"
#include "event.h"
#include "debug.h"
#include "renderer/renderer.h"

static inline u16 swizzle_texel(u16 pixel)
{
    u8 red = (pixel & 0x1f);
    u8 green = ((pixel >> 5) & 0x1f);
    u8 blue = ((pixel >> 10) & 0x1f);
    u16 mask = pixel & 0x8000;
    return mask | (u16)(red << 10) | (u16)(green << 5) | (u16)blue;
}

static inline void gp0_cpu_to_vram(struct gpu_state *gpu, u32 word)
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
u32 gpuread(struct gpu_state *gpu)
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
    if (gpu->pending_store)
    {
        u32 first_pixel = swizzle_texel(gpu->readback_buffer[gpu->readback_buffer_len++]);
        u32 second_pixel = swizzle_texel(gpu->readback_buffer[gpu->readback_buffer_len++]);
        u32 word = (first_pixel & 0xffff) | (second_pixel << 16);
        if (gpu->readback_buffer_len == gpu->store.pending_halfwords)
        {
            gpu->stat.ready_to_send_vram = 0;
            gpu->readback_buffer_len = 0;
            gpu->pending_store = 0;
        }
        return word;
    }
    else
    {
        return gpu->read;
    }

#endif
}

// event fired on start of hblank (horizontal display x2)
void gpu_hblank_event(void *data, u32 param, s32 cycles_late)
{
    struct psx_state *psx = (struct psx_state *)data;
    struct gpu_state *gpu = psx->gpu;
    struct root_counter *counter0 = psx->timers[0];
    struct root_counter *counter1 = psx->timers[1];
    if (counter1->mode.clock_source & 0x1) {
        ++counter1->value;
    }
    if (counter0->mode.sync_mode == 3)
    {

    }
    // NOTE: does not account for when x1 == x2
    //s32 hblank_cycles = (s32)video_to_cpu_cycles(3413 - (cpu->gpu.horizontal_display_x2 - cpu->gpu.horizontal_display_x1));
    s32 ticks_per_scanline = (s32)video_to_cpu_cycles(NTSC_VIDEO_CYCLES_PER_SCANLINE);
    schedule_event(gpu_hblank_event, psx, 0, ticks_per_scanline - cycles_late, EVENT_ID_GPU_HBLANK);
}

u64 gpu_tick(struct gpu_state *gpu) // advance gpu cycle count to determine horizontal timings
{
    // NOTE: This can probably be done in a much simpler way but it's all I could think of at the time
    u64 hblanks = 0;
    u64 elapsed = g_cycles_elapsed - gpu->timestamp;
    gpu->timestamp = g_cycles_elapsed;

    u64 video_cycles = elapsed * (11 / 7.0f);
    u32 remaining_ticks = NTSC_VIDEO_CYCLES_PER_SCANLINE - gpu->prev_cycles; // ticks remaining before adding cycles
    u32 current_ticks = (u32)((gpu->prev_cycles + video_cycles) % NTSC_VIDEO_CYCLES_PER_SCANLINE); // current video cycles
    
    if (gpu->prev_cycles >= gpu->horizontal_display_x2)
    {
        if (video_cycles >= remaining_ticks)
        {
            video_cycles -= remaining_ticks;
            hblanks = video_cycles / gpu->horizontal_display_x2;
        }
    }
    else
    {
        u32 ticks_to_hblank = gpu->horizontal_display_x2 - gpu->prev_cycles;
        if (video_cycles >= ticks_to_hblank)
        {
            hblanks = 1;
            video_cycles -= ticks_to_hblank;
            if (video_cycles >= remaining_ticks)
            {
                video_cycles -= remaining_ticks;
                hblanks += video_cycles / gpu->horizontal_display_x2;
            }
        }
    }


    SY_ASSERT(gpu->horizontal_display_x2 >= gpu->horizontal_display_x1);
    //u32 width = gpu->horizontal_display_x2 - gpu->horizontal_display_x1;
    gpu->prev_cycles = current_ticks;

    return hblanks;
}

void execute_gp1_command(struct gpu_state *gpu, u32 command)
{
    //struct gpu_state *gpu = &cpu->gpu;
    u8 op = command >> 24;

    switch (op)
    {
    case 0x0:
        SY_ASSERT(!gpu->pending_words);
        gpu->fifo_len = 0;
        gpu->pending_load = 0;
        gpu->pending_store = 0;
        gpu->pending_words = 0;
        gpu->vram_display_x = 0;
        gpu->vram_display_y = 0;
        gpu->horizontal_display_x1 = 512;
        gpu->horizontal_display_x2 = 3072;
        gpu->vertical_display_y1 = 16;
        gpu->vertical_display_y2 = 256;
        //gpu->copy_buffer_len = 0;
        //gpu->readback_buffer_len = 0;
        gpu->stat.value = 0x14802000;
        break;
    case 0x1:
        gpu->fifo_len = 0;
        gpu->pending_load = 0;
        gpu->pending_store = 0;
        gpu->pending_words = 0;
        //gpu->copy_buffer_len = 0;
        //gpu->readback_buffer_len = 0;
        break;
    case 0x2:
        gpu->stat.irq = 0;
        break;
    case 0x3:
        gpu->stat.display_disabled = (command & 0x1);
        break;
    case 0x4:
        gpu->stat.data_request = (command & 0x3);
        break;
    case 0x5:
        gpu->vram_display_x = command & 0x3fe; // halfword address
        gpu->vram_display_y = (command >> 10) & 0x1ff;
        break;
    case 0x6:
        gpu_tick(gpu);
        gpu->horizontal_display_x1 = command & 0xfff;
        gpu->horizontal_display_x2 = (command >> 12) & 0xfff;
        break;
    case 0x7:
        gpu->vertical_display_y1 = command & 0x3ff;
        gpu->vertical_display_y2 = (command >> 10) & 0x3ff;
        break;
    case 0x8:
        gpu->stat.value &= (~0x7f0000);
        gpu->stat.value |= (command & 0x3f) << 17;
        gpu->stat.value |= (command & 0x40) << 10;
        break;
    case 0x9:
        //gpu->stat.texture_disable = (command & 0x1);
        gpu->allow_texture_disable = (command & 0x1);
        break;
    default:
        debug_log("Unknown gp1 command: %x\n", op);
        break;
    }
#if LOG_GP1_COMMANDS
    debug_log("GP1 command: %02xh\n", op);
#endif
}

void execute_gp0_command(struct gpu_state *gpu, u32 word)
{
    if (!gpu->pending_words) // get number of remaining words in cmd (includes command itself)
    {
        u8 op = word >> 24;
        gpu->command_type = (enum gpu_command_type)(word >> 29);
        switch (gpu->command_type)
        {
        case 0x0:
            switch (op)
            {
            case 0x0:
                return;
            case 0x1:
                gpu->pending_words = 1;
                break;
            case 0x2:
                // TODO: masking
                gpu->pending_words = 3;
                break;
            default:
                debug_log("Unhandled GP0 MISC: %02xh\n", op);
                break;
            }
            break;
        case 0x1:
        {
            // render polygon
            gpu->polygon_flags = op;
            u32 vertex_count;
            if (op & POLYGON_FLAG_IS_QUAD)
            {
                gpu->pending_words = 4;
                vertex_count = 4;
            }
            else
            {
                gpu->pending_words = 3;
                vertex_count = 3;
            }
            if (op & POLYGON_FLAG_TEXTURED)
            {
                gpu->pending_words *= 2;
            }
            if (op & POLYGON_FLAG_GOURAUD_SHADED)
            {
                gpu->pending_words += (vertex_count - 1); // command includes first color
            }
            ++gpu->pending_words;
        } 
        break;
        case 0x3:
        {
            // render rectangle
            gpu->rect_flags = op;
            gpu->pending_words = 2;
            if (op & RECT_FLAG_TEXTURED)
            {
                ++gpu->pending_words;
            }
            if (!((op >> 3) & 0x3))
            {
                ++gpu->pending_words;
            }
        }
        break;
        case 0x4:
        {
            // vram -> vram
            gpu->pending_words = 4;
        }
        break;
        case 0x5:
        {
            // cpu -> vram
            gpu->pending_words = 3;
        }
        break;
        case 0x6:
        {
            // vram -> cpu
            gpu->pending_words = 3;
        }
        break;
        case 0x7:
        {
            // environment
            gpu->pending_words = 1;
        }
        break;
        default: // TODO: remove
            debug_log("Unimplemented gp0 command: %x\n", op);
            break;
        }
        #if LOG_GP0_COMMANDS
        debug_log("gp0: %02x\n", op);
        #endif
        gpu->fifo_len = 0;
    }

    --gpu->pending_words;

    if (gpu->pending_load)
    {
#if SOFTWARE_RENDERING
        gp0_cpu_to_vram(gpu, word);
#else
        gpu->copy_buffer[gpu->copy_buffer_len++] = swizzle_texel((u16)word); // NOTE: we should probably handling swizzling on the gpu
        gpu->copy_buffer[gpu->copy_buffer_len++] = swizzle_texel((u16)(word >> 16));
        
        if (!gpu->pending_words)
        {
            gpu->pending_load = 0;
            push_cpu_to_vram_copy(gpu->renderer, (void **)gpu->copy_buffer_at, gpu->load.x, gpu->load.y, gpu->load.width, gpu->load.height);
            //gpu->copy_buffer_len = 0;
        }
#endif
    }
    else
    {
        SY_ASSERT(gpu->fifo_len < 16);
        gpu->fifo[gpu->fifo_len++] = word;
        if (!gpu->pending_words)
        {
            u32 *commands = gpu->fifo;
            switch (gpu->command_type)
            {
            case COMMAND_TYPE_MISC:
            {
                switch (commands[0] >> 24)
                {
                case 0x1:
                    NoImplementation;
                    break;
                case 0x2:
                    // TODO: masking
                    if (gpu->draw_area_changed)
                    {
                        push_draw_area(gpu->renderer, gpu->drawing_area);
                        gpu->draw_area_changed = 0;
                    }
                    push_rect(gpu->renderer, commands, 0, 0);
                    break;
                SY_INVALID_CASE;
                }
            }
            break;
            case COMMAND_TYPE_DRAW_POLYGON:
            {
                // NOTE: textured polygons can change bits in gpustat, not sure if it only changes when the command begins execution.. but we pass the tests for now :p
                if (gpu->polygon_flags & POLYGON_FLAG_TEXTURED)
                {
                    u16 texpage_attribute = (u16)((commands[gpu->polygon_flags & POLYGON_FLAG_GOURAUD_SHADED ? 5 : 4]) >> 16);
                    gpu->stat.value &= 0xffff7e00;
                    gpu->stat.value |= (texpage_attribute & 0x1ff);
                    gpu->stat.value |= gpu->allow_texture_disable ? ((texpage_attribute & 0x800) << 4) : 0x0;
                }
                #if 1
                if (gpu->draw_area_changed)
                {
                    push_draw_area(gpu->renderer, gpu->drawing_area);
                    gpu->draw_area_changed = 0;
                }
                #endif
                //push_draw_area(gpu->renderer, gpu->drawing_area);
                push_polygon(gpu->renderer, commands, gpu->polygon_flags, v2f(gpu->draw_offset_x, gpu->draw_offset_y));
            }
            break;
            case COMMAND_TYPE_DRAW_RECT:
            {
                #if 1
                if (gpu->draw_area_changed)
                {
                    push_draw_area(gpu->renderer, gpu->drawing_area);
                    gpu->draw_area_changed = 0;
                }
                #endif
                //push_draw_area(gpu->renderer, gpu->drawing_area);
                push_rect(gpu->renderer, commands, gpu->rect_flags, (gpu->stat.value & 0x7ff));
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
            #if SOFTWARE_RENDERING

            #else
                push_vram_copy(gpu->renderer, src_x, src_y, dst_x, dst_y, width, height);
            #endif
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

            #if SOFTWARE_RENDERING
                SY_ASSERT((width & 0x1) == 0); // TODO: handle odd transfers
            #endif
                gpu->pending_words = (size + (size & 0x1)) >> 1;
                gpu->pending_load = 1; // we are now in a pending load, don't push values to the command buffer
                gpu->copy_buffer_at = gpu->copy_buffer + gpu->copy_buffer_len;
                gpu->load.x = dst_x;
                gpu->load.y = dst_y;
                gpu->load.width = width;
                gpu->load.height = height;
                gpu->load.pending_halfwords = size;
                gpu->load.left = dst_x;
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
                gpu->readback_buffer_len = 0;
                // NOTE: since we return the data right away we flush the commands, but maybe we can rely on bit 27 of GPUSTAT?
            #if !SOFTWARE_RENDERING
                push_vram_to_cpu_copy(gpu->renderer, (void **)&gpu->readback_buffer, src_x, src_y, width, height);
                gpu->renderer->flush_commands(gpu->renderer);
                reset_gpu_draw_state(gpu);
            #endif
                gpu->pending_store = 1;
                struct load_params store = {.x = src_x, .y = src_y, .width = width, .height = height, .pending_halfwords = size, .left = src_x};
                gpu->store = store;
                gpu->stat.ready_to_send_vram = 1;
                //debug_log("[VRAM->CPU] src x: %d, y: %d | w: %d, h: %d\n", src_x, src_y, width, height);
            }
            break;
            case COMMAND_TYPE_ENV:
            {
                SY_ASSERT(gpu->fifo_len == 1);
                switch (commands[0] >> 24)
                {
                case 0xe1:
                    gpu->stat.value &= 0xffff7800;
                    gpu->stat.value |= (commands[0] & 0x7ff);
                    gpu->stat.value |= gpu->allow_texture_disable ? ((commands[0] & 0x800) << 4) : 0x0;
                    //debug_log("GP0: 0xE1\n");
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
                    gpu->draw_area_changed = 1;
                    //debug_log("GP0 draw area: left -> %d, top -> %d\n", gpu->drawing_area.left, gpu->drawing_area.top);
                    //push_clip_rect(gpu->drawing_area.left, gpu->drawing_area.top, gpu->drawing_area.right, gpu->drawing_area.bottom);
                    break;
                case 0xe4:
                    gpu->drawing_area.right = (commands[0] & 0x3ff);
                    gpu->drawing_area.bottom = ((commands[0] >> 10) & 0x3ff);
                    gpu->draw_area_changed = 1;

                    //debug_log("GP0 draw area: right -> %d, bottom -> %d\n", gpu->drawing_area.right, gpu->drawing_area.bottom);
                    //push_clip_rect(gpu->drawing_area.left, gpu->drawing_area.top, gpu->drawing_area.right, gpu->drawing_area.bottom);
                    break;
                case 0xe5:
                    s16 draw_offset_x = (s16)((commands[0] & 0x7ff) << 5);
                    draw_offset_x >>= 5;
                    gpu->draw_offset_x = draw_offset_x;
                    s16 draw_offset_y = (s16)(((commands[0] >> 11) & 0x7ff) << 5);
                    draw_offset_y >>= 5;
                    gpu->draw_offset_y = draw_offset_y;

                    break;
                case 0xe6:
                    gpu->stat.set_mask_on_draw = (commands[0] & 0x1);
                    gpu->stat.draw_to_masked = (commands[0] >> 1) & 0x1;
                    SY_ASSERT(!(gpu->stat.set_mask_on_draw | gpu->stat.draw_to_masked));
                    break;
                SY_INVALID_CASE;
                }
            }
            break;
            SY_INVALID_CASE;
            }
        }
    }
}

void gpu_scanline_complete(void *data, u32 param, s32 cycles_late)
{
    struct psx_state *psx = (struct psx_state *)data;
    struct gpu_state *gpu = psx->gpu;

    ++gpu->scanline;
    if (gpu->scanline == 263) {
        gpu->scanline = 0;
    }
    // in 240 lines mode, bit 31 changes per scanline
    if (!gpu->stat.vertical_res) {
        gpu->stat.odd_line = gpu->scanline & 0x1;
    }
    
    if (gpu->scanline == gpu->vertical_display_y1)
    {
        // end of vblank
        #if 1
        struct root_counter *timer1 = psx->timers[1];
        
        if (timer1->mode.sync_enable)
        {
            switch (timer1->mode.sync_mode)
            {
            case 0:
                timer1->pause_ticks += safe_truncate32(g_cycles_elapsed - timer1->timestamp);
                break;
            case 1:
                break;
            case 2:
                break;
            case 3:
                break;
            }
        }
        #endif
    }
    else if (gpu->scanline == gpu->vertical_display_y2)
    {
        // vblank begins
        struct root_counter *timer1 = psx->timers[1];
        if (timer1->mode.sync_enable)
        {
            switch (timer1->mode.sync_mode)
            {
            case 0: // pause during vblank
                timer1->timestamp = g_cycles_elapsed;
                break;
            case 1: // reset to 0 at vblank
                timer1->value = 0;
                break;
            case 2: // reset to 0 at vblank and pause outside of it
                break;
            case 3: // pause until vblank occurs, then free run
                timer1->paused = 0;
                break;
            }
            timer1->sync = 1;
        }

        gpu->renderer->flush_commands(gpu->renderer);
        gpu->renderer->update_display(gpu->renderer);
        reset_gpu_draw_state(gpu);

        if (gpu->stat.vertical_res) {
            gpu->stat.odd_line ^= 1;
        }
        ++g_vblank_counter;
        psx->cpu->i_stat |= INTERRUPT_VBLANK;
        signal_event_set(g_present_thread_handle);
    }
    s32 cycles_until_event = (s32)video_to_cpu_cycles(NTSC_VIDEO_CYCLES_PER_SCANLINE) - cycles_late;
    schedule_event(gpu_scanline_complete, psx, 0, cycles_until_event, EVENT_ID_GPU_SCANLINE_COMPLETE);
}
