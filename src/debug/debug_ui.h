#ifndef DEBUG_UI_H
#define DEBUG_UI_H

#include "allocator.h"
#include "sy_math.h"

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
    u32 color;
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

struct debug_ui_list_clipper
{
    u32 start_index;
    u32 end_index;
    u32 item_size;
    u32 item_count;
};

enum debug_ui_interaction_flags
{
    DEBUG_UI_INTERACTION_HOVERED  = 0x1,
    DEBUG_UI_INTERACTION_PRESSED  = 0x2,
    DEBUG_UI_INTERACTION_HELD     = 0x4,
    DEBUG_UI_INTERACTION_RELEASED = 0x8,
    DEBUG_UI_INTERACTION_CLICKED  = 0x10
};

enum debug_ui_textbox_flags
{
    TEXTBOX_FLAG_RESET_ON_EDIT = 0x1
};

void debug_ui_init(struct memory_arena *arena);

void debug_ui_begin(u32 screen_w, u32 screen_h);
void debug_ui_end(void);

void debug_ui_reset_command_ptr(void);
b8 debug_ui_next_command(struct debug_ui_command_header **cmd);

void debug_ui_keydown(int key);

void debug_ui_mousemove(int x, int y);
void debug_ui_mousedown(int button);
void debug_ui_mouseup(int button);

void debug_ui_mousewheel(int delta);

void debug_ui_quad(u32 color, rect2 rect);

void debug_ui_push_clip_rect(rect2 r);
void debug_ui_pop_clip_rect(void);

void debug_ui_push_layout(debug_ui_direction dir);
void debug_ui_pop_layout(void);

void debug_ui_layout_row(void);

vec2i debug_ui_next_pos(void);

void debug_ui_push_id(u32 id);
void debug_ui_pop_id(void);

u32 debug_ui_button_behavior(u32 id, rect2 bounds);

b8 debug_ui_button(const char *text);
void debug_ui_label(const char *label);
void debug_ui_labelf(const char *fmt, ...);
void debug_ui_color_label(u32 color, const char *label);
void debug_ui_color_labelf(u32 color, const char *fmt, ...);

void debug_ui_checkbox(b8 *value, const char *label);

struct debug_ui_list_clipper debug_ui_begin_list_clipper(u32 item_size, u32 item_count);
void debug_ui_end_list_clipper(struct debug_ui_list_clipper *clipper);

b8 debug_ui_begin_window(const char *title, u32 flags, b8 *p_open);
void debug_ui_end_window(void);

void debug_ui_set_window_rect(const char *name, rect2 rect);

b8 debug_ui_begin_group(const char *title);
void debug_ui_end_group(void);

void debug_ui_set_scroll(s32 amount);

rect2 debug_ui_get_layout_rect(void);
rect2 debug_ui_get_panel_rect(void);

b8 debug_ui_textbox(char *buffer, u32 buffer_len, u32 flags);

#endif /* DEBUG_UI_H */
