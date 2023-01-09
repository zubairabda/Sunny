enum command_type
{
    COMMAND_TYPE_NONE,
    COMMAND_TYPE_DRAW_POLYGON,
    COMMAND_TYPE_DRAW_TEXTURED_POLYGON,
    COMMAND_TYPE_COPY_TO_VRAM,
    COMMAND_TYPE_COPY_TO_CPU
};

typedef union
{
    struct
    {
        u8 raw_texture : 1;
        u8 semi_transparent : 1;
        u8 textured : 1;
        u8 is_quad : 1;
        u8 gouraud : 1;
        u8 cmd : 3;
    };
    u8 value;
} polygon_params;

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
    enum command_type command_state;
    u32 fifo[16];
    u32 fifo_len;
    u32 read;
    GPUSTAT stat;

    u16 vertical_timing;
    u16 horizontal_timing;
    f32 video_cycles;
    u32 scanline;
    u32 copy_buffer_len;
    u16* copy_buffer;
    u32 readback_buffer_len;
    u16* readback_buffer;

    u8* vram;
    Rectangle2i drawing_area;
    vec2i draw_offset;
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
};