#ifndef GPU_H
#define GPU_H

#include "gpu_common.h"
#include "renderer/renderer.h"

#define NTSC_VIDEO_CYCLES_PER_SCANLINE 3413

enum gpu_command_type
{
    COMMAND_TYPE_MISC = 0x0,
    COMMAND_TYPE_DRAW_POLYGON = 0x1,
    COMMAND_TYPE_DRAW_LINE = 0x2,
    COMMAND_TYPE_DRAW_RECT = 0x3,
    COMMAND_TYPE_VRAM_TO_VRAM = 0x4,
    COMMAND_TYPE_CPU_TO_VRAM = 0x5,
    COMMAND_TYPE_VRAM_TO_CPU = 0x6,
    COMMAND_TYPE_ENV = 0x7
};
typedef enum gpu_command_type gpu_command_type;

struct load_params
{
    u16 width;
    u16 height;
    u16 x;
    u16 y;
    u16 left;
    u16 reserved;
    u32 pending_halfwords;
};

typedef union
{
    struct
    {
        u32 texture_page_base_x : 4;
        u32 texture_page_base_y : 1;
        u32 semi_transparency_mode : 2;
        u32 texture_page_colors : 2;
        u32 dither_enable : 1;
        u32 draw_to_display_area : 1;
        u32 set_mask_on_draw : 1;
        u32 draw_to_masked : 1;
        u32 interlace_field : 1;
        u32 reverse_flag : 1;
        u32 texture_disable : 1;
        u32 horizontal_res_2 : 1;
        u32 horizontal_res_1 : 2;
        u32 vertical_res : 1;
        u32 video_mode : 1;
        u32 display_depth_24_bit : 1;
        u32 vertical_interlace: 1;
        u32 display_disabled : 1;
        u32 irq : 1;
        u32 data_request : 1;
        u32 ready_to_receive_cmd : 1;
        u32 ready_to_send_vram : 1;
        u32 ready_to_receive_dma : 1;
        u32 dma_direction : 2;
        u32 odd_line : 1;
    };
    u32 value;
} GPUSTAT;

struct gpu_state
{
    //renderer_interface *renderer;
    b8 software_rendering;
    b8 enable_output;
    gpu_command_type command_type;
    u8 render_flags;

    u32 fifo[16];
    u32 fifo_len;
    u32 read;
    GPUSTAT stat;
    // timings
    u64 timestamp; // cpu cycles
    u32 scanline_cycles; // video cycles
    u32 remainder_cycles;
    u32 hblanks;
    s32 dot_div;
    
    b8 allow_texture_disable;
    b8 draw_area_changed;

    u16 vertical_timing;
    u16 horizontal_timing;

    u32 scanline;
    u32 copy_buffer_len;
    u32 readback_buffer_len;
    u16 *copy_buffer;
    u16 *readback_buffer;
    u16 *copy_buffer_at;
    
    rect2 drawing_area;
    s16 draw_offset_x;
    s16 draw_offset_y;
    b8 pending_load;
    b8 pending_store;
    u32 pending_words;
    struct load_params load;
    struct load_params store;
    u8 texture_window_mask_x;
    u8 texture_window_mask_y;
    u8 texture_window_offset_x;
    u8 texture_window_offset_y;
    u16 vram_display_x;
    u16 vram_display_y;
    u16 horizontal_display_x1;
    u16 horizontal_display_x2;
    u16 vertical_display_y1;
    u16 vertical_display_y2;

    u64 video_cycles;
};

extern struct gpu_state g_gpu;

#if 0
inline u64 video_to_cpu_cycles(u64 video_cycles, u64 remainder)
{
    return ((video_cycles * 451584) + remainder) / 715909;
}
#else
inline u64 video_to_cpu_cycles(u64 video_cycles)
{
    return (video_cycles * 451584) / 715909;
}
#endif
inline u64 cpu_to_video_cycles(u64 cpu_cycles, u64 remainder)
{
    return (cpu_cycles * 715909) / 451584;
}

inline b8 in_vblank(void)
{
    return g_gpu.scanline < g_gpu.vertical_display_y1 || g_gpu.scanline >= g_gpu.vertical_display_y2;
}

void gpu_reset(void);
u32 gpuread(void);
void execute_gp1_command(u32 command);
void execute_gp0_command(u32 word);
void gpu_scanline_complete(u32 param, s32 cycles_late);
void gpu_hsync(void);

#endif /* GPU_H */
