#ifndef DEBUG_UI_H
#define DEBUG_UI_H

#include "allocator.h"
#include "sy_math.h"

// extern b8 g_update_ui; would be useful if we stored the UI as a seperate texture

struct debug_ui_file_dialog_result
{
    int index;
    char file_name[280];
};

enum debug_ui_mouse_button
{
    DEBUG_UI_MOUSE_LEFT = 0x1,
    DEBUG_UI_MOUSE_RIGHT = 0x2,
    DEBUG_UI_MOUSE_MIDDLE = 0x4
};

enum debug_ui_command_type
{
    DEBUG_UI_COMMAND_SET_CLIP,
    DEBUG_UI_COMMAND_QUAD,
    DEBUG_UI_COMMAND_TEXT
};

struct debug_ui_command_header
{
    enum debug_ui_command_type type;
    u32 size;
};

struct debug_ui_command_set_clip
{
    struct debug_ui_command_header header;
    rect2 r;
};

struct debug_ui_command_quad
{
    struct debug_ui_command_header header;
    u32 color;
    rect2 r;
};

struct debug_ui_command_text
{
    struct debug_ui_command_header header;
    vec2i pos;
};

typedef enum debug_ui_layout_type
{
    HORIZONTAL,
    VERTICAL
} debug_ui_layout_type;

struct debug_ui_layout
{
    debug_ui_layout_type type;
    int start_x;
    int start_y;
    int at_x;
    int at_y;
    int row_pad;
    int column_pad;
    int row_size;
};

void debug_ui_init(struct memory_arena *arena);

void debug_ui_begin(f32 dt, u32 screen_w, u32 screen_h);
void debug_ui_end(void);

void debug_ui_push_layout(debug_ui_layout_type type, int x, int y);
void debug_ui_pop_layout(void);

void debug_ui_reset_command_ptr(void);
struct debug_ui_command_header *debug_ui_next_command(void);

void debug_ui_keydown(int key);

void debug_ui_mousemove(int x, int y);
void debug_ui_mousedown(int button);
void debug_ui_mouseup(int button);

void debug_ui_quad(int x, int y, int w, int h);

void debug_ui_push_clip_rect(rect2 r);
void debug_ui_pop_clip_rect(void);

b8 debug_ui_button(const char *text);
void debug_ui_label(const char *label);

void debug_ui_open_file_dialog(const char *title);
b8 debug_ui_file_dialog(const char *title, const char **file_types, u32 num_file_types, struct debug_ui_file_dialog_result *out_file);

#endif /* DEBUG_UI_H */
