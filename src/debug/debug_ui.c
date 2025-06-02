#include "debug_ui.h"
#include "atlas.h"
#include "platform/platform.h"

#define MAX_UI_PANELS 64
#define MAX_UI_LAYOUT_STACK 8
#define MAX_UI_CLIP_STACK 8
#define MAX_UI_ID_STACK 8

#define MAX_UI_FILE_PATH 260

#define WINDOW_BORDER_SIZE 5

enum
{
    PANEL_FLAG_FIRSTFRAME = 0x1,
    PANEL_FLAG_SIZEABLE   = 0x2,
    PANEL_FLAG_TITLEBAR   = 0x4,
    PANEL_FLAG_VSCROLLBAR = 0x8,
    PANEL_FLAG_HSCROLLBAR = 0x10
};

struct debug_ui_panel
{
    u32 id;
    u32 flags;
    vec2i pos;
    vec2i size;
    vec2i content_size;
    vec2i scroll;
    u64 last_frame_touched;
    struct debug_ui_command_jump *end_jmp;
    u32 commands;
    struct debug_ui_panel *next;
    struct debug_ui_panel *prev;
};

struct debug_ui_layout
{
    debug_ui_direction dir;
    s32 at_x;
    s32 at_y;
    s32 start_x;
    s32 start_y;
};

struct debug_ui_style
{
    //u32 font_line_height;
    u32 text_color;
    u32 base;
    u32 border;
    u32 hover;
    u32 active;
    u32 inactive;
    u16 pad_x;
    u16 pad_y;
    u16 margin_x;
    u16 margin_y;
};

struct debug_ui_state
{
    u32 hot;
    u32 active;
    u32 hot_panel;
    f32 dt;
    vec2i mouse_pos;
    vec2i last_mouse_pos;
    vec2i mouse_delta;
    vec2i click_offset;
    s32 mouse_wheel_delta;
    u8 mouse_buttons; /* state of buttons bits: 0 - left, 1 - right, 2 - middle */
    u8 mouse_pressed; /* mouse button went down */
    u8 key_pressed;
    vec2u screen_dim;
    u8 *commands;
    u32 commands_at;
    u32 next_command;
    u64 frame_counter;
    struct debug_ui_panel panels[MAX_UI_PANELS];
    struct debug_ui_panel *current;
    struct debug_ui_panel *parent;
    struct debug_ui_command_jump *first;
    struct debug_ui_layout layout_stack[MAX_UI_LAYOUT_STACK];
    rect2 clip_stack[MAX_UI_CLIP_STACK];
    u32 id_stack[MAX_UI_ID_STACK];
    u32 layout_stack_index;
    u32 clip_stack_index;
    u32 id_stack_index;
    struct debug_ui_style style;
};

struct debug_ui_state g_debug_ui;

static inline void *push_command(enum debug_ui_command_type type, u32 size)
{
    struct debug_ui_command_header *result = (struct debug_ui_command_header *)(g_debug_ui.commands + g_debug_ui.commands_at);
    result->type = type;
    result->size = size;
    g_debug_ui.commands_at += size;
    SY_ASSERT(g_debug_ui.commands_at <= KILOBYTES(512));
    return result;
}

static inline void *push_jump(u32 dst)
{
    struct debug_ui_command_jump *result = push_command(DEBUG_UI_COMMAND_JUMP, sizeof(struct debug_ui_command_jump));
    result->dst = dst;
    return result;
}

static inline void push_text(const char *text, u32 length, s32 x, s32 y)
{
    struct debug_ui_command_text *result = push_command(DEBUG_UI_COMMAND_TEXT, sizeof(struct debug_ui_command_text) + length + 1);
    result->pos.x = x;
    result->pos.y = y;
    char *dst = (char *)(result + 1);
    memcpy(dst, text, length);
    dst[length] = '\0';
}

static inline b8 point_in_rect(vec2i p, rect2 r)
{
    return (p.x > r.left && p.x < r.right && p.y > r.top && p.y < r.bottom);
}

static inline rect2 expand_rect(rect2 rect, u32 size)
{
    rect2 result = {.left = rect.left - size, .top = rect.top - size, .right = rect.right + size, .bottom = rect.bottom + size};
    return result;
}

static struct debug_ui_panel *update_panel_states(void)
{
    // prune stale windows and update hovered window
    struct debug_ui_panel *result = NULL;
    struct debug_ui_panel *sentinel = &g_debug_ui.panels[0];
    struct debug_ui_panel *current = sentinel->next;
    #if 0
    if (current == g_debug_ui.sentinel)
        return NULL;
    #endif
    g_debug_ui.first->dst = current->commands;

    vec2i mousep = g_debug_ui.mouse_pos;
    while (current != sentinel)
    {
        if (current->last_frame_touched < g_debug_ui.frame_counter)
        {
            current->next->prev = current->prev;
            current->prev->next = current->next;
        }
        else
        {
            rect2 bounds = {current->pos.x, current->pos.y, current->pos.x + current->size.x, current->pos.y + current->size.y};

            if (current->flags & PANEL_FLAG_SIZEABLE)
                bounds = expand_rect(bounds, WINDOW_BORDER_SIZE);

            if (point_in_rect(mousep, bounds))
                result = current;

            if (current->prev != sentinel) // TODO: remove
                current->prev->end_jmp->dst = current->commands;
        }

        current = current->next;
    }
    return result;
}

static void bring_panel_to_front(struct debug_ui_panel *panel)
{
    panel->next->prev = panel->prev;
    panel->prev->next = panel->next;

    struct debug_ui_panel *sentinel = &g_debug_ui.panels[0];
    struct debug_ui_panel *top = sentinel->prev;
    top->next = panel;
    panel->prev = top;
    panel->next = sentinel;
    sentinel->prev = panel;
}

void debug_ui_init(struct memory_arena *arena)
{
    g_debug_ui.commands = push_arena(arena, KILOBYTES(512));
    SY_ASSERT(g_debug_ui.commands);
    struct debug_ui_panel *sentinel = &g_debug_ui.panels[0];
    sentinel->next = sentinel->prev = sentinel;

    // configure style
    struct debug_ui_style style = {
        //.font_line_height = font_line_height,
        .text_color = 0xffffff,
        .base = 0x4f4f4f,
        .border = 0x1e1e1e,
        .hover = 0x646464,
        .active = 0x3f3f3f,
        .inactive = 0x1e1e1e,
        .pad_x = 4,
        .pad_y = 2,
        .margin_x = font_space_width,
        .margin_y = 0
    };

    g_debug_ui.style = style;
}

void debug_ui_push_layout(debug_ui_direction dir, s32 x, s32 y)
{
    struct debug_ui_layout *layout = &g_debug_ui.layout_stack[g_debug_ui.layout_stack_index++];
    layout->dir = dir;
    layout->at_x = layout->start_x = x;
    layout->at_y = layout->start_y = y;
}

void debug_ui_pop_layout(void)
{
    // TODO: add current layout size to previous layout
    if (g_debug_ui.layout_stack_index > 0)
        --g_debug_ui.layout_stack_index;
}

void debug_ui_layout_row(void)
{
    struct debug_ui_layout *layout = &g_debug_ui.layout_stack[g_debug_ui.layout_stack_index - 1];
    struct debug_ui_style *style = &g_debug_ui.style;
    layout->at_y += font_line_height + (style->pad_y + style->margin_y) * 2;
    layout->at_x = layout->start_x;
}

void debug_ui_push_id(u32 id)
{
    u32 result = id;
    if (g_debug_ui.id_stack_index > 0)
        result ^= g_debug_ui.id_stack[g_debug_ui.id_stack_index - 1];
    g_debug_ui.id_stack[g_debug_ui.id_stack_index++] = result;
}

void debug_ui_pop_id(void)
{
    if (g_debug_ui.id_stack_index > 0)
        --g_debug_ui.id_stack_index;
}

static u32 debug_ui_get_id_from_string(const char *text)
{
    u32 id = fnv1a(text);
    return (id ^ g_debug_ui.id_stack[g_debug_ui.id_stack_index - 1]);
}

void debug_ui_begin(f32 dt, u32 screen_w, u32 screen_h)
{
    g_debug_ui.dt = dt;

    g_debug_ui.commands_at = 0;
    g_debug_ui.next_command = 0;

    //g_debug_ui.current_panel = NULL;

    ++g_debug_ui.frame_counter;

    g_debug_ui.screen_dim.x = screen_w;
    g_debug_ui.screen_dim.y = screen_h;

    g_debug_ui.mouse_delta.x = g_debug_ui.mouse_pos.x - g_debug_ui.last_mouse_pos.x;
    g_debug_ui.mouse_delta.y = g_debug_ui.mouse_pos.y - g_debug_ui.last_mouse_pos.y;

    // set up root items
    rect2 window_rect = {.right = screen_w, .bottom = screen_h};
    struct debug_ui_command_set_clip *cmd = push_command(DEBUG_UI_COMMAND_SET_CLIP, sizeof(struct debug_ui_command_set_clip));
    cmd->r = window_rect;
    g_debug_ui.clip_stack[g_debug_ui.clip_stack_index++] = window_rect;

    g_debug_ui.first = push_jump(0); // initial jump so we can link it to the first window
}

void debug_ui_end(void)
{
    SY_ASSERT(g_debug_ui.clip_stack_index == 1);
    SY_ASSERT(g_debug_ui.layout_stack_index == 0);
    g_debug_ui.layout_stack_index = 0;
    g_debug_ui.clip_stack_index = 0;
    g_debug_ui.mouse_pressed = 0;
    g_debug_ui.key_pressed = 0;
    g_debug_ui.last_mouse_pos = g_debug_ui.mouse_pos;
    g_debug_ui.mouse_wheel_delta = 0;

    struct debug_ui_panel *panel = update_panel_states();

    if (panel)
    {
        g_debug_ui.hot_panel = panel->id;
    }
    else
    {
        g_debug_ui.hot_panel = 0;
    }
}

void debug_ui_reset_command_ptr(void)
{
    g_debug_ui.next_command = 0;
}

struct debug_ui_command_header *debug_ui_next_command(void)
{
    if (g_debug_ui.next_command == g_debug_ui.commands_at)
    {
        return NULL;
    }

    struct debug_ui_command_header *cmd = (struct debug_ui_command_header *)(g_debug_ui.commands + g_debug_ui.next_command);
    while (cmd->type == DEBUG_UI_COMMAND_JUMP)
    {
        struct debug_ui_command_jump *jmp = (struct debug_ui_command_jump *)cmd;
        if (!jmp->dst) // NOTE: this is reliant on never having window commands at the start of the buffer
            return NULL;
        g_debug_ui.next_command = jmp->dst;
        cmd = (struct debug_ui_command_header *)(g_debug_ui.commands + g_debug_ui.next_command);
    }
    g_debug_ui.next_command += cmd->size;
    return cmd;
}

void debug_ui_keydown(int key)
{
    g_debug_ui.key_pressed = key;
}

void debug_ui_mousemove(int x, int y)
{
    g_debug_ui.mouse_pos.x = x;
    g_debug_ui.mouse_pos.y = y;
}

void debug_ui_mousedown(int button)
{
    g_debug_ui.mouse_buttons |= button;
    g_debug_ui.mouse_pressed |= button;
}

void debug_ui_mouseup(int button)
{
    g_debug_ui.mouse_buttons &= ~button;
}

void debug_ui_mousewheel(int delta)
{
    g_debug_ui.mouse_wheel_delta = delta;
}

vec2i debug_ui_next_pos(void)
{
    struct debug_ui_layout *layout = &g_debug_ui.layout_stack[g_debug_ui.layout_stack_index - 1];
    vec2i result = {.data = {layout->at_x, layout->at_y}};
    return result;
}

static vec2i advance_layout(s32 width, s32 height)
{
    struct debug_ui_layout *layout = &g_debug_ui.layout_stack[g_debug_ui.layout_stack_index - 1];
    vec2i result = {.data = {layout->at_x, layout->at_y}};

    struct debug_ui_style *style = &g_debug_ui.style;
    
    switch (layout->dir)
    {
    case DIR_HORIZONTAL:
        layout->at_x += width + style->margin_x;
        break;
    case DIR_VERTICAL:
        layout->at_y += height + style->margin_y;
        break;
    }

    return result;
}

static int get_text_width(const char *text, int *p_len)
{
    int len = 0;
    int width = 0;
    for (const char *p = text; *p; ++p)
    {
        ++len;
        int c = *p;
        if (c > 126)
        {
            c = '?';
        }
        else if (c == ' ')
        {
            width += font_space_width;
            continue;
        }
        
        width += glyphs[c].advance;
    }

    if (p_len)
        *p_len = len;

    return width;
}

u32 debug_ui_button_behavior(u32 id, rect2 bounds)
{
    u32 result = 0;

    vec2i mousep = g_debug_ui.mouse_pos;

    rect2 clip_rect = g_debug_ui.clip_stack[g_debug_ui.clip_stack_index - 1];
    // clip interaction area
    rect2 a;
    a.left = MAX(bounds.left, clip_rect.left);
    a.top = MAX(bounds.top, clip_rect.top);
    a.right = MIN(bounds.right, clip_rect.right);
    a.bottom = MIN(bounds.bottom, clip_rect.bottom);

    b8 window_hovered = (g_debug_ui.hot_panel == g_debug_ui.parent->id);
    b8 mouse_over = point_in_rect(mousep, a);
    if (mouse_over && window_hovered)
    {
        if (!g_debug_ui.active || g_debug_ui.active == id)
        {
            g_debug_ui.hot = id;
            result |= DEBUG_UI_INTERACTION_HOVERED;
            if (g_debug_ui.mouse_pressed & DEBUG_UI_MOUSE_LEFT)
            {
                result |= DEBUG_UI_INTERACTION_PRESSED;
                g_debug_ui.click_offset.y = mousep.y - bounds.top;
                g_debug_ui.click_offset.x = mousep.x - bounds.left;
                g_debug_ui.active = id;
            }
        }
    }
    else if (g_debug_ui.hot == id)
    {
        g_debug_ui.hot = 0;
    }

    if (g_debug_ui.active == id)
    {
        if (!(g_debug_ui.mouse_buttons & DEBUG_UI_MOUSE_LEFT)) 
        {
            if (g_debug_ui.hot == id)
            {
                result |= DEBUG_UI_INTERACTION_CLICKED;
            }
            result |= DEBUG_UI_INTERACTION_RELEASED;
            g_debug_ui.active = 0;
        }
        else
        {
            result |= DEBUG_UI_INTERACTION_HELD;
        }
    }

    return result;
}

enum
{
    BUTTON_FLAG_INACTIVE = 0x1
};

b8 debug_ui_button(const char *text)
{
    SY_ASSERT(g_debug_ui.current);
    
    int len;
    int text_width = get_text_width(text, &len);

    int ascender = 14;
    int descender = 4;
    int height = 18;

    struct debug_ui_style *style = &g_debug_ui.style;
    int button_w = text_width + style->pad_x * 2;
    int button_h = height + style->pad_y * 2;

    vec2i pos = advance_layout(button_w, button_h);
    rect2 bounds = {pos.x, pos.y, pos.x + button_w, pos.y + button_h};
    
    u32 color = style->base;
    
    u32 id = debug_ui_get_id_from_string(text);

    u32 result = debug_ui_button_behavior(id, bounds);

    if (result & DEBUG_UI_INTERACTION_HELD)
        color = style->active;
    else if (result & DEBUG_UI_INTERACTION_HOVERED)
        color = style->hover;

    struct debug_ui_command_quad *quad = push_command(DEBUG_UI_COMMAND_QUAD, sizeof(struct debug_ui_command_quad));
    quad->color = color;
    quad->r = bounds;

    push_text(text, len, bounds.left + style->pad_x, bounds.top + style->pad_y + ascender);

    return (result & DEBUG_UI_INTERACTION_CLICKED) ? true : false;
}

void debug_ui_label(const char *label)
{
    int len;
    int text_width = get_text_width(label, &len);
    vec2i pos = advance_layout(text_width, 18);
    push_text(label, len, pos.x, pos.y + font_line_height);
}

void debug_ui_labelf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    char buf[128];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    debug_ui_label(buf);
}

void debug_ui_quad(u32 color, int x, int y, int w, int h) 
{
    rect2 bounds = {x, y, x + w, y + h};

    struct debug_ui_command_quad *quad = push_command(DEBUG_UI_COMMAND_QUAD, sizeof(struct debug_ui_command_quad));
    quad->color = color;
    quad->r = bounds;
}

void debug_ui_push_clip_rect(rect2 r)
{
    rect2 prev = g_debug_ui.clip_stack[g_debug_ui.clip_stack_index - 1];
    // new clip rect is confined to the previous clip rect
    if (r.right > prev.right)
        r.right = prev.right;
    if (r.bottom > prev.bottom)
        r.bottom = prev.bottom;
    if (r.left < prev.left)
        r.left = prev.left;
    if (r.top < prev.top)
        r.top = prev.top;
    g_debug_ui.clip_stack[g_debug_ui.clip_stack_index++] = r;
    struct debug_ui_command_set_clip *cmd = push_command(DEBUG_UI_COMMAND_SET_CLIP, sizeof(struct debug_ui_command_set_clip));
    cmd->r = r;
}

void debug_ui_pop_clip_rect(void)
{
    --g_debug_ui.clip_stack_index;
    rect2 r = g_debug_ui.clip_stack[g_debug_ui.clip_stack_index - 1];
    struct debug_ui_command_set_clip *cmd = push_command(DEBUG_UI_COMMAND_SET_CLIP, sizeof(struct debug_ui_command_set_clip));
    cmd->r = r;
}

static struct debug_ui_panel *get_panel(u32 id)
{
    u32 f = g_debug_ui.frame_counter;
    u32 n = 0;
    // get the oldest entry, or the matching entry that was updated last frame
    // first entry is skipped since it is the sentinel
    for (u32 i = 1; i < MAX_UI_PANELS; ++i)
    {
        struct debug_ui_panel *panel = &g_debug_ui.panels[i];
        if (panel->id == id)
        {
            if (panel->last_frame_touched == (g_debug_ui.frame_counter - 1))
            {
                panel->flags &= ~(PANEL_FLAG_FIRSTFRAME);
                panel->last_frame_touched = g_debug_ui.frame_counter;
                return panel;
            }
        }
        if (panel->last_frame_touched < f)
        {
            f = panel->last_frame_touched;
            n = i;
        }
    }

    struct debug_ui_panel *result = &g_debug_ui.panels[n];
    memset(result, 0, sizeof(struct debug_ui_panel));
    result->last_frame_touched = g_debug_ui.frame_counter;
    result->id = id;
    result->flags = PANEL_FLAG_FIRSTFRAME;
    return result;
}

b8 debug_ui_begin_window(const char *title, rect2 rect, u32 flags, b8 *p_open)
{
    if (p_open)
    {
        b8 is_open = *p_open;
        if (!is_open)
            return false;
    }

    vec2i mousep = g_debug_ui.mouse_pos;

    u32 id = fnv1a(title); // assume top level widget
    debug_ui_push_id(id);
    struct debug_ui_panel *window = get_panel(id);

    window->commands = g_debug_ui.commands_at;

    g_debug_ui.current = window;
    g_debug_ui.parent = window;

    if (window->flags & PANEL_FLAG_FIRSTFRAME)
    {
        window->pos.x = rect.left;
        window->pos.y = rect.top;
        window->size.x = rect.right - rect.left;
        window->size.y = rect.bottom - rect.top;

        struct debug_ui_panel *sentinel = &g_debug_ui.panels[0];
        struct debug_ui_panel *top = sentinel->prev;
        top->next = window;
        window->prev = top;
        window->next = sentinel;
        sentinel->prev = window;

        window->flags |= PANEL_FLAG_SIZEABLE;

        if (point_in_rect(mousep, expand_rect(rect, WINDOW_BORDER_SIZE)))
            g_debug_ui.hot_panel = id;
    }

    //if (!(window->flags & PANEL_FLAG_IS_ENABLED))
    //    return false;
    struct debug_ui_style *style = &g_debug_ui.style;

    rect2 window_rect = r2(window->pos.x, window->pos.y, window->size.x, window->size.y);

    debug_ui_quad(style->border, window->pos.x - 1, window->pos.y - 1, window->size.x + 2, window->size.y + 2);

    debug_ui_quad(0x363636, window->pos.x, window->pos.y, window->size.x, window->size.y);
#if 0
    debug_ui_quad(0xff0000, window_rect.right, window_rect.top, 5, window->size.y);
    debug_ui_quad(0x00ff00, window_rect.left - 5, window_rect.top, 5, window->size.y);
    debug_ui_quad(0x0000ff, window_rect.left, window_rect.top - 5, window->size.x, 5);
    debug_ui_quad(0x0000ff, window_rect.left, window_rect.bottom, window->size.x, 5);
#endif
    debug_ui_push_clip_rect(window_rect);

    int title_bar_height = font_line_height + style->pad_y * 2;
    debug_ui_push_layout(DIR_HORIZONTAL, window->pos.x, window->pos.y + title_bar_height);

    rect2 title_bar_rect = {window->pos.x, window->pos.y, window->pos.x + window->size.x, window->pos.y + title_bar_height};
    debug_ui_quad(style->border, window->pos.x, window->pos.y, window->size.x, title_bar_height);

    if (p_open)
    {
        rect2 close = r2(window_rect.right - title_bar_height, window->pos.y, title_bar_height, title_bar_height);
        int w = get_text_width("X", NULL);
        w = (title_bar_height - w) / 2;
        push_text("X", 1, close.left + w, close.top + font_line_height);
        if (debug_ui_button_behavior(debug_ui_get_id_from_string("#close"), close) & DEBUG_UI_INTERACTION_CLICKED)
        {
            *p_open = false;
        }
    }

    enum window_interaction
    {
        WINDOW_INTERACTION_NONE,
        WINDOW_INTERACTION_MOVE,
        WINDOW_INTERACTION_SIZE_RIGHT,
        WINDOW_INTERACTION_SIZE_LEFT,
        WINDOW_INTERACTION_SIZE_TOP,
        WINDOW_INTERACTION_SIZE_BOTTOM,
    };

    static enum window_interaction interaction;
    static vec2i mouse_grab_offset;
    static vec2i window_size_on_grab;

    if (g_debug_ui.hot_panel == id && !g_debug_ui.active)
    {
        if (g_debug_ui.mouse_pressed & DEBUG_UI_MOUSE_LEFT)
        {
            bring_panel_to_front(window);
            if (point_in_rect(mousep, title_bar_rect))
            {
                interaction = WINDOW_INTERACTION_MOVE;
            }
            else if (point_in_rect(mousep, r2(window_rect.right, window_rect.top, 5, window->size.y)))
            {
                interaction = WINDOW_INTERACTION_SIZE_RIGHT;
                mouse_grab_offset.x = g_debug_ui.mouse_pos.x - window_rect.left;
                window_size_on_grab.x = window->size.x - mouse_grab_offset.x;
            }
            else if (point_in_rect(mousep, r2(window_rect.left - 5, window_rect.top, 5, window->size.y)))
            {
                interaction = WINDOW_INTERACTION_SIZE_LEFT;
                mouse_grab_offset.x = g_debug_ui.mouse_pos.x - window_rect.left;
                window_size_on_grab.x = window->size.x - mouse_grab_offset.x;
            }
            else if (point_in_rect(mousep, r2(window_rect.left, window_rect.top - 5, window->size.x, 5)))
            {
                interaction = WINDOW_INTERACTION_SIZE_TOP;
                mouse_grab_offset.y = g_debug_ui.mouse_pos.y - window_rect.top;
                window_size_on_grab.y = window->size.y - mouse_grab_offset.y;
            }
            else if (point_in_rect(mousep, r2(window_rect.left, window_rect.bottom, window->size.x, 5)))
            {
                interaction = WINDOW_INTERACTION_SIZE_BOTTOM;
                mouse_grab_offset.y = g_debug_ui.mouse_pos.y - window_rect.top;
                window_size_on_grab.y = window->size.y - mouse_grab_offset.y;
            }
            else
            {
                interaction = WINDOW_INTERACTION_NONE;
                goto nointeraction;
            }
            g_debug_ui.active = id;
        }
    }

    if (g_debug_ui.active == id)
    {
        //bring_panel_to_front(window);
        switch (interaction)
        {
        case WINDOW_INTERACTION_MOVE:
            window->pos.x += g_debug_ui.mouse_delta.x;
            window->pos.y += g_debug_ui.mouse_delta.y;  
            break;
        case WINDOW_INTERACTION_SIZE_RIGHT:
            window->size.x = (g_debug_ui.mouse_pos.x + window_size_on_grab.x) - window->pos.x;
            if (window->size.x < 50)
                window->size.x = 50;
            break;
        case WINDOW_INTERACTION_SIZE_LEFT:
            window->pos.x = (g_debug_ui.mouse_pos.x - mouse_grab_offset.x);
            window->size.x = window_rect.right - window->pos.x;
            if (window->size.x < 50)
            {
                window->size.x = 50;
                window->pos.x = window_rect.right - 50;
            }
            break;
        case WINDOW_INTERACTION_SIZE_TOP:
            window->pos.y = (g_debug_ui.mouse_pos.y - mouse_grab_offset.y);
            window->size.y = window_rect.bottom - window->pos.y;
            if (window->size.y < title_bar_height)
            {
                window->size.y = title_bar_height;
                window->pos.y = window_rect.bottom - title_bar_height;
            }
            break;
        case WINDOW_INTERACTION_SIZE_BOTTOM:
            window->size.y = (g_debug_ui.mouse_pos.y + window_size_on_grab.y) - window->pos.y;
            if (window->size.y < title_bar_height)
                window->size.y = title_bar_height;
            break;
        default:
            break;
        }

        if (!(g_debug_ui.mouse_buttons & DEBUG_UI_MOUSE_LEFT))
        {
            g_debug_ui.active = 0;
        }
    }

nointeraction:

    return true;
}

void debug_ui_end_window(void)
{
    //struct debug_ui_layout *window_layout = &g_debug_ui.layout_stack[g_debug_ui.layout_stack_index - 1];
    struct debug_ui_panel *window = g_debug_ui.current;
    debug_ui_pop_clip_rect(); // window_rect
    window->end_jmp = push_jump(0);
    debug_ui_pop_layout();
    debug_ui_pop_id();
    g_debug_ui.current = NULL;
    g_debug_ui.parent = NULL;
}

vec2i debug_ui_get_window_size(void)
{
    SY_ASSERT(g_debug_ui.current);
    return g_debug_ui.current->size;
}

b8 debug_ui_begin_group(const char *title)
{
    u32 id = debug_ui_get_id_from_string(title);
    struct debug_ui_panel *parent = g_debug_ui.current;
    struct debug_ui_panel *panel = get_panel(id);

    g_debug_ui.current = panel;

    vec2i pos = debug_ui_next_pos();
    panel->pos = pos;

    rect2 bounds = g_debug_ui.clip_stack[g_debug_ui.clip_stack_index - 1]; // TODO: temp
    //panel->size.y = (parent->pos.y + parent->size.y) - pos.y;
    panel->size.y = bounds.bottom - pos.y;
    if (panel->size.y < 0)
        panel->size.y = 0;
    //panel->size.x = parent->size.x;
    panel->size.x = bounds.right - pos.x;
    
    rect2 group_rect = r2(pos.x, pos.y, panel->size.x, panel->size.y);
    const u32 scrollbar_size = 10;

    if (panel->content_size.y > panel->size.y)
    {
        // create vertical scrollbar
        s32 max = panel->content_size.y - panel->size.y; // the maximum scroll distance is to the bottom minus the size of the panel
        s32 knob_height = (panel->size.y * panel->size.y) / panel->content_size.y;
        if (knob_height < 10)
            knob_height = 10;
        
        s32 scroll_pos = group_rect.top + (((s64)panel->scroll.y * (panel->size.y - knob_height)) / max);

        // TODO: remove
        group_rect.right -= scrollbar_size;
        panel->size.x -= scrollbar_size;
        ///////////////

        s32 knob_x = group_rect.right;
        rect2 knob = r2(knob_x, scroll_pos, scrollbar_size, knob_height);

        u32 id = debug_ui_get_id_from_string("vscroll");
        u32 result = debug_ui_button_behavior(id, knob);
        if (result & DEBUG_UI_INTERACTION_HELD)
        {
            // NOTE: this is to fix the scroll jitter when clicking after the scroll has been set. Since we
            // determine the scroll from the mouse position, it offers a more coarse result than manually setting it
            // through set_scroll() or using the mousewheel
            if (g_debug_ui.mouse_pos.y != g_debug_ui.last_mouse_pos.y)
            {
                s64 pos = (g_debug_ui.mouse_pos.y - g_debug_ui.click_offset.y) - panel->pos.y;
                s32 scroll = (pos * max) / (panel->size.y - knob_height);
                panel->scroll.y = scroll;
            }
        }

        const u32 scroll_speed = 20;
        b8 parent_hovered = (g_debug_ui.hot_panel == parent->id);
        if (parent_hovered && point_in_rect(g_debug_ui.mouse_pos, group_rect))
        {
            panel->scroll.y -= g_debug_ui.mouse_wheel_delta * scroll_speed;
        }

        if (panel->scroll.y < 0)
            panel->scroll.y = 0;
        else if (panel->scroll.y > max)
            panel->scroll.y = max;

        struct debug_ui_style *style = &g_debug_ui.style;
        debug_ui_quad(style->border, group_rect.right, group_rect.top, scrollbar_size, panel->size.y);

        debug_ui_quad(style->base, knob_x, scroll_pos, scrollbar_size, knob_height); // NOTE: this is using last frame's scroll_pos
    }
    else
    {
        panel->scroll.y = 0;
    }

    debug_ui_push_layout(DIR_VERTICAL, pos.x, pos.y - panel->scroll.y);

    debug_ui_push_clip_rect(group_rect);

    return true;
}

struct debug_ui_list_clipper debug_ui_begin_list_clipper(u32 item_size, u32 item_count)
{
    struct debug_ui_panel *panel = g_debug_ui.current;
    s32 size_y = panel->size.y;
    f32 calc = (f32)size_y / item_size;
    u32 capacity = (u32)ceilf(calc) + 1; // maximum number of items that can fit in the visible space

    // vec2i pos = debug_ui_next_pos(); // TODO: account for items not part of the list clipper but still in the scrollable region
    u32 preclip_count = panel->scroll.y / item_size; // number of items that can fit before the visible space
    SY_ASSERT(preclip_count <= item_count);
    u32 remaining = item_count - preclip_count;
    u32 count = MIN(capacity, remaining);

    // we first advance the layout by the number of elements preceeding the visible ones,
    // then in the list_end() call we advance the layout by the remaining elements
    s32 advance = preclip_count * item_size;
    advance_layout(0, advance);

    struct debug_ui_list_clipper result = {.start_index = preclip_count, .end_index = preclip_count + count, .item_size = item_size, .item_count = item_count};

    return result;
}

void debug_ui_end_list_clipper(struct debug_ui_list_clipper *clipper)
{
    s32 advance = (clipper->item_count - clipper->end_index) * clipper->item_size;
    advance_layout(0, advance);
}

void debug_ui_end_group(void)
{
    struct debug_ui_panel *group = g_debug_ui.current;
    struct debug_ui_panel *parent = g_debug_ui.parent;

    rect2 group_rect = r2(group->pos.x, group->pos.y, group->size.x, group->size.y);

    struct debug_ui_layout *layout = &g_debug_ui.layout_stack[g_debug_ui.layout_stack_index - 1];

    group->content_size.y = layout->at_y - layout->start_y;
    group->content_size.x = layout->at_x - layout->start_x;

    debug_ui_pop_layout();

    debug_ui_pop_clip_rect();

    g_debug_ui.current = parent;
}

void debug_ui_set_scroll(s32 amount)
{
    struct debug_ui_panel *group = g_debug_ui.current;
    
    if (group->content_size.y > group->size.y)
    {
        group->scroll.y = amount;
        if (group->scroll.y < 0)
            group->scroll.y = 0;
        else if ((group->scroll.y + group->size.y) > group->content_size.y)
            group->scroll.y = group->content_size.y - group->size.y;
    }
}

void debug_ui_checkbox(b8 *value, const char *label)
{
    vec2i pos = debug_ui_next_pos();
    uintptr_t ptr = (uintptr_t)value;
    u32 id = (u32)ptr;
#if 1
    struct debug_ui_style *style = &g_debug_ui.style;

    u32 height = font_line_height + 2 * style->pad_y;
    u32 width = height;
    rect2 bounds = r2(pos.x, pos.y, width, height);
    
    u32 color = style->base;
    u32 result = debug_ui_button_behavior(id, bounds);
    
    if (result & DEBUG_UI_INTERACTION_HELD)
        color = style->active;
    else if (result & DEBUG_UI_INTERACTION_HOVERED)
        color = style->hover;
    
    if (result & DEBUG_UI_INTERACTION_CLICKED)
    {
        b8 val = *value;
        *value = !val;
    }

    debug_ui_quad(color, pos.x, pos.y, width, height);
    if (*value)
        push_text("X", 1, pos.x + style->pad_x, pos.y + font_line_height);

    advance_layout(width, height);
#endif
    if (label)
        debug_ui_label(label);
}
