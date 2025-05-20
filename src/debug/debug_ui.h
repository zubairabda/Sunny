#ifndef DEBUG_UI_H
#define DEBUG_UI_H

#include "allocator.h"
#include "sy_math.h"

// extern b8 g_update_ui; would be useful if we stored the UI as a seperate texture

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
    DEBUG_UI_COMMAND_TEXT,
    DEBUG_UI_COMMAND_JUMP
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

struct debug_ui_command_jump
{
    struct debug_ui_command_header header;
    u32 dst;
};

typedef enum
{
    DIR_HORIZONTAL,
    DIR_VERTICAL
} debug_ui_direction;

void debug_ui_init(struct memory_arena *arena);

void debug_ui_begin(f32 dt, u32 screen_w, u32 screen_h);
void debug_ui_end(void);

void debug_ui_reset_command_ptr(void);
struct debug_ui_command_header *debug_ui_next_command(void);

void debug_ui_keydown(int key);

void debug_ui_mousemove(int x, int y);
void debug_ui_mousedown(int button);
void debug_ui_mouseup(int button);

void debug_ui_mousewheel(int delta);

void debug_ui_quad(u32 color, int x, int y, int w, int h);

void debug_ui_push_clip_rect(rect2 r);
void debug_ui_pop_clip_rect(void);

void debug_ui_push_layout(debug_ui_direction dir, s32 x, s32 y);
void debug_ui_pop_layout(void);

vec2i debug_ui_next_pos(void);

void debug_ui_push_id(u32 id);
void debug_ui_pop_id(void);

b8 debug_ui_button(const char *text);
void debug_ui_label(const char *label);

b8 debug_ui_begin_window(const char *title, rect2 rect, u32 flags, b8 *p_open);
void debug_ui_end_window(void);

vec2i debug_ui_get_window_size(void);

#endif /* DEBUG_UI_H */
