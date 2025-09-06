#include "debug_ui.h"
#include "atlas.h"
#include "platform/platform.h"

#define MAX_UI_PANELS 64
#define MAX_UI_PANEL_STACK 8
#define MAX_UI_ROOT_STACK 4
#define MAX_UI_LAYOUT_STACK 8
#define MAX_UI_CLIP_STACK 8
#define MAX_UI_ID_STACK 8
#define MAX_UI_STYLE_STACK 8

#define MAX_UI_FILE_PATH 260

#define WINDOW_BORDER_SIZE 5

enum
{
    PANEL_FLAG_FIRST_USE  = 0x1,
    PANEL_FLAG_APPEARING  = 0x2,
    PANEL_FLAG_SIZEABLE   = 0x4,
    PANEL_FLAG_TITLEBAR   = 0x8,
    PANEL_FLAG_VSCROLLBAR = 0x10,
    PANEL_FLAG_HSCROLLBAR = 0x20
};

struct debug_ui_panel
{
    u32 id;
    u32 flags;
    // up to date pos/size
    vec2i pos;
    vec2i size;
    // last frame pos/size
    rect2 rect;
    vec2i content_size;
    vec2i scroll;
    u64 last_frame_touched;
    u32 commands; // offset to the window commands after the begin jump, which points to after the end jump
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
    rect2 bounds;
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
    s32 mouse_wheel_delta;
    vec2i mouse_pos;
    vec2i last_mouse_pos;
    vec2i mouse_delta;
    vec2i click_offset;
    u8 mouse_buttons; /* state of buttons bits: 0 - left, 1 - right, 2 - middle */
    u8 mouse_pressed; /* mouse button went down */
    u8 key_pressed;
    u8 reserved;
    u32 current_buffer_pos;
    vec2u screen_dim;
    u8 *commands;
    u32 commands_at;
    u32 next_command;
    u64 frame_counter;
    struct debug_ui_panel panels[MAX_UI_PANELS];
    struct debug_ui_panel *root_stack[MAX_UI_ROOT_STACK];
    struct debug_ui_panel *panel_stack[MAX_UI_PANEL_STACK];
    struct debug_ui_layout layout_stack[MAX_UI_LAYOUT_STACK];
    rect2 clip_stack[MAX_UI_CLIP_STACK];
    u32 id_stack[MAX_UI_ID_STACK];
    u32 root_stack_index;
    u32 panel_stack_index;
    u32 layout_stack_index;
    u32 clip_stack_index;
    u32 viewport_stack_index;
    u32 id_stack_index;
    vec2i next_layout_pos;
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

static inline void push_text(const char *text, u32 length, s32 x, s32 y, u32 color)
{
    struct debug_ui_command_text *result = push_command(DEBUG_UI_COMMAND_TEXT, sizeof(struct debug_ui_command_text) + length + 1);
    result->pos.x = x;
    result->pos.y = y;
    result->color = color;
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

static inline struct debug_ui_command_jump *get_panel_tail_jump(struct debug_ui_panel *panel)
{
    struct debug_ui_command_jump *head = ((struct debug_ui_command_jump *)(g_debug_ui.commands + panel->commands)) - 1;
    struct debug_ui_command_jump *tail = ((struct debug_ui_command_jump *)(g_debug_ui.commands + head->dst)) - 1;
    return tail;
}

static void update_panel_states(void)
{
    // prune stale windows and update hovered window
    struct debug_ui_panel *sentinel = &g_debug_ui.panels[0];
    struct debug_ui_panel *result = sentinel;
    struct debug_ui_panel *current = sentinel->next;
    b8 set_jump = false;
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
            set_jump = true;
            rect2 bounds = current->rect;//r2(current->pos.x, current->pos.y, current->size.x, current->size.y);

            if (current->flags & PANEL_FLAG_SIZEABLE)
                bounds = expand_rect(bounds, WINDOW_BORDER_SIZE);

            if (point_in_rect(mousep, bounds))
                result = current;

            if (current->prev != sentinel)
            {
                get_panel_tail_jump(current->prev)->dst = current->commands;
            }
        }

        current = current->next;
    }
    
    // if there are any panels, push a jump at the end of the buffer which points to the first panel's commands
    if (set_jump)
    {
        push_jump(sentinel->next->commands);
        // the last panel's commands should jump to the end of the command buffer
        get_panel_tail_jump(sentinel->prev)->dst = g_debug_ui.commands_at;
    }

    g_debug_ui.hot_panel = result->id;
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

static inline void debug_ui_push_panel(struct debug_ui_panel *panel)
{
    g_debug_ui.panel_stack[g_debug_ui.panel_stack_index++] = panel;
}

static inline void debug_ui_pop_panel(void)
{
    --g_debug_ui.panel_stack_index;
}

static inline struct debug_ui_panel *get_current_panel(void)
{
    return g_debug_ui.panel_stack[g_debug_ui.panel_stack_index - 1];
}

static inline void debug_ui_push_root_panel(struct debug_ui_panel *panel)
{
    debug_ui_push_panel(panel); // push to both root stack and panel stack
    push_jump(0); // initial jump which will point to the end of this window's commands
    panel->commands = g_debug_ui.commands_at;
    g_debug_ui.root_stack[g_debug_ui.root_stack_index++] = panel;
}

static inline void debug_ui_pop_root_panel(void)
{
    struct debug_ui_panel *panel = g_debug_ui.root_stack[g_debug_ui.root_stack_index - 1];
    push_jump(0); // tail jump which points to the next panel
    struct debug_ui_command_jump *begin = (struct debug_ui_command_jump *)(g_debug_ui.commands + panel->commands) - 1;
    begin->dst = g_debug_ui.commands_at;
    g_debug_ui.root_stack_index--;
    debug_ui_pop_panel();
}

void debug_ui_init(void)
{
    g_debug_ui.commands = malloc(KILOBYTES(512));
    SY_ASSERT(g_debug_ui.commands);
    struct debug_ui_panel *sentinel = &g_debug_ui.panels[0];
    sentinel->next = sentinel->prev = sentinel;

    debug_ui_push_id(0);
    debug_ui_push_root_panel(sentinel);

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

void debug_ui_shutdown(void)
{
    free(g_debug_ui.commands);
}

static vec2i advance_layout(s32 width, s32 height)
{
    struct debug_ui_layout *layout = &g_debug_ui.layout_stack[g_debug_ui.layout_stack_index - 1];
    vec2i result = {.data = {layout->at_x, layout->at_y}};

    struct debug_ui_style *style = &g_debug_ui.style;
    
    switch (layout->dir)
    {
    case DIR_HORIZONTAL:
    {
        layout->at_x += width + style->margin_x;
        if (layout->at_x > layout->bounds.right)
            layout->bounds.right = layout->at_x;
        if (layout->at_y + height > layout->bounds.bottom)
            layout->bounds.bottom = layout->at_y + height;
        break;
    }
    case DIR_VERTICAL:
    {
        layout->at_y += height + style->margin_y;
        if (layout->at_y > layout->bounds.bottom)
            layout->bounds.bottom = layout->at_y;
        if (layout->at_x + width > layout->bounds.right)
            layout->bounds.right = layout->at_x + width;
        break;
    }
    INVALID_CASE;
    }

    return result;
}

void debug_ui_push_layout(debug_ui_direction dir)
{
    //layout->container = container;
    vec2i pos = g_debug_ui.layout_stack_index ? debug_ui_next_pos() : g_debug_ui.next_layout_pos;
    vec2i scroll = g_debug_ui.panel_stack[g_debug_ui.panel_stack_index - 1]->scroll;
    struct debug_ui_layout *layout = &g_debug_ui.layout_stack[g_debug_ui.layout_stack_index++];
    layout->dir = dir;
    layout->at_x = layout->start_x = pos.x - scroll.x;
    layout->at_y = layout->start_y = pos.y - scroll.y;
    //layout->width = layout->height = 0;
    layout->bounds = r2(layout->start_x, layout->start_y, 0, 0);
}

void debug_ui_pop_layout(void)
{
    if (g_debug_ui.layout_stack_index > 0)
    {
        struct debug_ui_layout *child = &g_debug_ui.layout_stack[g_debug_ui.layout_stack_index - 1];
        --g_debug_ui.layout_stack_index;
        if (g_debug_ui.layout_stack_index > 0)
        {
            s32 width = child->bounds.right - child->bounds.left;
            s32 height = child->bounds.bottom - child->bounds.top;
            advance_layout(width, height);
        }
    }
}

vec2i debug_ui_next_pos(void)
{
    struct debug_ui_layout *layout = &g_debug_ui.layout_stack[g_debug_ui.layout_stack_index - 1];
    vec2i result = {.data = {layout->at_x, layout->at_y}};
    return result;
}

void debug_ui_layout_row(void)
{
    struct debug_ui_layout *layout = &g_debug_ui.layout_stack[g_debug_ui.layout_stack_index - 1];
    struct debug_ui_style *style = &g_debug_ui.style;
    layout->at_y = layout->bounds.bottom;
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

void debug_ui_begin(u32 screen_w, u32 screen_h)
{
    g_debug_ui.commands_at = 0;
    g_debug_ui.next_command = 0;

    g_debug_ui.next_layout_pos = v2i(0, 0);

    //g_debug_ui.current_panel = NULL;

    ++g_debug_ui.frame_counter;

    g_debug_ui.screen_dim = v2u(screen_w, screen_h);

    g_debug_ui.mouse_delta.x = g_debug_ui.mouse_pos.x - g_debug_ui.last_mouse_pos.x;
    g_debug_ui.mouse_delta.y = g_debug_ui.mouse_pos.y - g_debug_ui.last_mouse_pos.y;

    // set up root items
    rect2 window_rect = {.right = screen_w, .bottom = screen_h};
    struct debug_ui_command_set_clip *cmd = push_command(DEBUG_UI_COMMAND_SET_CLIP, sizeof(struct debug_ui_command_set_clip));
    cmd->r = window_rect;
    g_debug_ui.clip_stack[g_debug_ui.clip_stack_index++] = window_rect;
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

    update_panel_states();
}

void debug_ui_reset_command_ptr(void)
{
    g_debug_ui.next_command = 0;
}

b8 debug_ui_next_command(struct debug_ui_command_header **cmd)
{
    while (g_debug_ui.next_command < g_debug_ui.commands_at)
    {
        *cmd = (struct debug_ui_command_header *)(g_debug_ui.commands + g_debug_ui.next_command);
        if ((*cmd)->type == DEBUG_UI_COMMAND_JUMP)
        {
            struct debug_ui_command_jump *jmp = (struct debug_ui_command_jump *)*cmd;
            g_debug_ui.next_command = jmp->dst;
        }
        else
        {
            g_debug_ui.next_command += (*cmd)->size;
            return true;
        }
    }
    return false;
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

    struct debug_ui_panel *root = g_debug_ui.root_stack[g_debug_ui.root_stack_index - 1];
    b8 window_hovered = (g_debug_ui.hot_panel == root->id);
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

    push_text(text, len, bounds.left + style->pad_x, bounds.top + style->pad_y + ascender, style->text_color);

    return (result & DEBUG_UI_INTERACTION_CLICKED) ? true : false;
}

static inline void draw_text(const char *text, u32 color)
{
    int len;
    int text_width = get_text_width(text, &len);
    vec2i pos = advance_layout(text_width, 18);
    push_text(text, len, pos.x, pos.y + font_line_height, color);
}

void debug_ui_label(const char *label)
{
    draw_text(label, g_debug_ui.style.text_color);
}

void debug_ui_labelf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    debug_ui_label(buf);
}

void debug_ui_color_label(u32 color, const char *label)
{
    draw_text(label, color);
}

void debug_ui_color_labelf(u32 color, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    debug_ui_color_label(color, buf);
}

void debug_ui_quad(u32 color, rect2 rect) 
{
    struct debug_ui_command_quad *quad = push_command(DEBUG_UI_COMMAND_QUAD, sizeof(struct debug_ui_command_quad));
    quad->color = color;
    quad->r = rect;
}

void debug_ui_outline(u32 color, rect2 rect, u32 size)
{
    debug_ui_quad(color, r2(rect.left - size, rect.top - size, size, (rect.bottom - rect.top) + 2 * size));
    debug_ui_quad(color, r2(rect.left, rect.top - size, rect.right - rect.left, size));
    debug_ui_quad(color, r2(rect.right, rect.top - size, size, (rect.bottom - rect.top) + 2 * size));
    debug_ui_quad(color, r2(rect.left, rect.bottom, rect.right - rect.left, size));
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

static struct debug_ui_panel *find_panel(u32 id)
{
    u32 f = g_debug_ui.frame_counter;
    u32 n = 1;
    // get the oldest entry, or the entry with matching ids
    // first entry is skipped since it is the sentinel
    struct debug_ui_panel *panel = NULL;
    for (u32 i = 1; i < MAX_UI_PANELS; ++i)
    {
        panel = &g_debug_ui.panels[i];
        if (panel->id == id)
        {
            return panel;
        }
        if (!panel->id)
        {
            n = i;
            break;
        }
        if (panel->last_frame_touched < f)
        {
            f = panel->last_frame_touched;
            n = i;
        }
    }

    panel = &g_debug_ui.panels[n];
    memset(panel, 0, sizeof(struct debug_ui_panel));
    //panel->last_frame_touched = g_debug_ui.frame_counter;
    panel->id = id;
    return panel;
}

static struct debug_ui_panel *get_panel(u32 id)
{
    struct debug_ui_panel *panel = find_panel(id);
    panel->flags &= ~(PANEL_FLAG_FIRST_USE | PANEL_FLAG_APPEARING);
    if (!panel->last_frame_touched)
    {
        panel->flags |= PANEL_FLAG_FIRST_USE;
    }
    if (panel->last_frame_touched < (g_debug_ui.frame_counter - 1))
    {
        panel->flags |= PANEL_FLAG_APPEARING;
    }
    panel->last_frame_touched = g_debug_ui.frame_counter;
    return panel;
}

void debug_ui_set_window_rect(const char *name, rect2 rect)
{
    struct debug_ui_panel *panel = find_panel(fnv1a(name));
    //panel->rect = rect;
    panel->pos.x = rect.left;
    panel->pos.y = rect.top;
    panel->size.x = rect.right - rect.left;
    panel->size.y = rect.bottom - rect.top;
}

b8 debug_ui_begin_window(const char *title, u32 flags, b8 *p_open)
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
    debug_ui_push_root_panel(window);

    if (window->flags & PANEL_FLAG_FIRST_USE)
    {
        if (window->size.x == 0 || window->size.y == 0)
        {
            window->pos = v2i(0, 0);
            window->size = v2i(300, 400);
            //window->rect = r2(0, 0, 300, 400);
        }
        window->flags |= PANEL_FLAG_SIZEABLE;
    }

    window->rect = r2(window->pos.x, window->pos.y, window->size.x, window->size.y);
    rect2 window_rect = window->rect;

    if (window->flags & PANEL_FLAG_APPEARING)
    {
        struct debug_ui_panel *sentinel = &g_debug_ui.panels[0];
        struct debug_ui_panel *top = sentinel->prev;
        top->next = window;
        window->prev = top;
        window->next = sentinel;
        sentinel->prev = window;

        if (point_in_rect(mousep, expand_rect(window_rect, WINDOW_BORDER_SIZE)))
            g_debug_ui.hot_panel = id;
    }

    struct debug_ui_style *style = &g_debug_ui.style;

    debug_ui_quad(style->border, expand_rect(window_rect, 1));

    debug_ui_quad(0x363636, window_rect);

    debug_ui_push_clip_rect(window_rect);

    int title_bar_size = font_line_height + style->pad_y * 2;
    rect2 container = window_rect;
    container.top += title_bar_size;
    g_debug_ui.next_layout_pos = v2i(container.left, container.top);
    debug_ui_push_layout(DIR_VERTICAL);

    rect2 title_bar = window_rect;
    title_bar.bottom = title_bar.top + title_bar_size;
    debug_ui_quad(style->border, title_bar);

    if (p_open)
    {
        rect2 close = r2(window_rect.right - title_bar_size, window_rect.top, title_bar_size, title_bar_size);
        int w = get_text_width("X", NULL);
        w = (title_bar_size - w) / 2;
        push_text("X", 1, close.left + w, close.top + font_line_height, style->text_color);
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

    vec2i pos = v2i(window_rect.left, window_rect.top);
    vec2i size = v2i(window_rect.right - window_rect.left, window_rect.bottom - window_rect.top);
    if (g_debug_ui.hot_panel == id && !g_debug_ui.active)
    {
        if (g_debug_ui.mouse_pressed & DEBUG_UI_MOUSE_LEFT)
        {
            bring_panel_to_front(window);
            if (point_in_rect(mousep, title_bar))
            {
                interaction = WINDOW_INTERACTION_MOVE;
            }
            else if (point_in_rect(mousep, r2(window_rect.right, window_rect.top, 5, size.y)))
            {
                interaction = WINDOW_INTERACTION_SIZE_RIGHT;
                mouse_grab_offset.x = g_debug_ui.mouse_pos.x - window_rect.left;
                window_size_on_grab.x = size.x - mouse_grab_offset.x;
            }
            else if (point_in_rect(mousep, r2(window_rect.left - 5, window_rect.top, 5, size.y)))
            {
                interaction = WINDOW_INTERACTION_SIZE_LEFT;
                mouse_grab_offset.x = g_debug_ui.mouse_pos.x - window_rect.left;
                window_size_on_grab.x = size.x - mouse_grab_offset.x;
            }
            else if (point_in_rect(mousep, r2(window_rect.left, window_rect.top - 5, size.x, 5)))
            {
                interaction = WINDOW_INTERACTION_SIZE_TOP;
                mouse_grab_offset.y = g_debug_ui.mouse_pos.y - window_rect.top;
                window_size_on_grab.y = size.y - mouse_grab_offset.y;
            }
            else if (point_in_rect(mousep, r2(window_rect.left, window_rect.bottom, size.x, 5)))
            {
                interaction = WINDOW_INTERACTION_SIZE_BOTTOM;
                mouse_grab_offset.y = g_debug_ui.mouse_pos.y - window_rect.top;
                window_size_on_grab.y = size.y - mouse_grab_offset.y;
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
            pos.x += g_debug_ui.mouse_delta.x;
            pos.y += g_debug_ui.mouse_delta.y;  
            break;
        case WINDOW_INTERACTION_SIZE_RIGHT:
            size.x = (g_debug_ui.mouse_pos.x + window_size_on_grab.x) - pos.x;
            if (size.x < 50)
                size.x = 50;
            break;
        case WINDOW_INTERACTION_SIZE_LEFT:
            pos.x = (g_debug_ui.mouse_pos.x - mouse_grab_offset.x);
            size.x = window_rect.right - pos.x;
            if (size.x < 50)
            {
                size.x = 50;
                pos.x = window_rect.right - 50;
            }
            break;
        case WINDOW_INTERACTION_SIZE_TOP:
            pos.y = (g_debug_ui.mouse_pos.y - mouse_grab_offset.y);
            size.y = window_rect.bottom - pos.y;
            if (size.y < title_bar_size)
            {
                size.y = title_bar_size;
                pos.y = window_rect.bottom - title_bar_size;
            }
            break;
        case WINDOW_INTERACTION_SIZE_BOTTOM:
            size.y = (g_debug_ui.mouse_pos.y + window_size_on_grab.y) - pos.y;
            if (size.y < title_bar_size)
                size.y = title_bar_size;
            break;
        default:
            break;
        }

        if (!(g_debug_ui.mouse_buttons & DEBUG_UI_MOUSE_LEFT))
        {
            g_debug_ui.active = 0;
        }

        //window->rect = r2(pos.x, pos.y, size.x, size.y);
        window->pos = pos;
        window->size = size;
    }

nointeraction:

    return true;
}

void debug_ui_end_window(void)
{
    struct debug_ui_panel *window = get_current_panel();
    debug_ui_pop_clip_rect(); // window_rect
    debug_ui_pop_layout();
    debug_ui_pop_id();
    debug_ui_pop_root_panel();
}

rect2 debug_ui_get_layout_rect(void)
{
    return g_debug_ui.layout_stack[g_debug_ui.layout_stack_index - 1].bounds;
}

rect2 debug_ui_get_panel_rect(void)
{
    return g_debug_ui.panel_stack[g_debug_ui.panel_stack_index - 1]->rect;
}

b8 debug_ui_begin_group(const char *title)
{
    u32 id = debug_ui_get_id_from_string(title);
    struct debug_ui_panel *panel = get_panel(id);

    rect2 bounds = g_debug_ui.panel_stack[g_debug_ui.panel_stack_index - 1]->rect;

    vec2i pos = debug_ui_next_pos();

    s32 width = MAX(bounds.right - bounds.left, 0);
    s32 height = MAX(bounds.bottom - pos.y, 0);

    panel->rect = r2(pos.x, pos.y, width, height);
    debug_ui_push_panel(panel);
    
    rect2 group_rect = panel->rect;
    const u32 scrollbar_size = 10;

    if (panel->content_size.y > height)
    {
        // create vertical scrollbar
        s32 max = panel->content_size.y - height; // the maximum scroll distance is to the bottom minus the size of the panel
        s32 knob_height = (height * height) / panel->content_size.y;
        if (knob_height < 10)
            knob_height = 10;
        
        s32 scroll_pos = group_rect.top + (((s64)panel->scroll.y * (height - knob_height)) / max);

        // TODO: remove
        group_rect.right -= scrollbar_size;
        panel->rect.right -= scrollbar_size;
        ///////////////

        s32 knob_x = group_rect.right;
        rect2 knob = r2(knob_x, scroll_pos, scrollbar_size, knob_height);

        u32 id = debug_ui_get_id_from_string("vscroll");
        u32 result = debug_ui_button_behavior(id, knob);
        if (result & DEBUG_UI_INTERACTION_HELD)
        {
            // NOTE: this is to fix the scroll jitter when clicking the scroll knob. Since we
            // determine the scroll from the mouse position in this case, it offers a more coarse result than manually setting it
            // through set_scroll() or using the mousewheel
            if (g_debug_ui.mouse_pos.y != g_debug_ui.last_mouse_pos.y)
            {
                s64 diff = (g_debug_ui.mouse_pos.y - g_debug_ui.click_offset.y) - pos.y;
                s32 scroll = (diff * max) / (height - knob_height);
                panel->scroll.y = scroll;
            }
        }

        const u32 scroll_speed = 20;
        b8 root_hovered = (g_debug_ui.hot_panel == g_debug_ui.root_stack[g_debug_ui.root_stack_index - 1]->id);
        if (root_hovered && point_in_rect(g_debug_ui.mouse_pos, group_rect))
            panel->scroll.y -= g_debug_ui.mouse_wheel_delta * scroll_speed;

        if (panel->scroll.y < 0)
            panel->scroll.y = 0;
        else if (panel->scroll.y > max)
            panel->scroll.y = max;

        struct debug_ui_style *style = &g_debug_ui.style;
        debug_ui_quad(style->border, r2(group_rect.right, group_rect.top, scrollbar_size, height));

        debug_ui_quad(style->base, r2(knob_x, scroll_pos, scrollbar_size, knob_height)); // NOTE: this is using last frame's scroll_pos
    }
    else
    {
        panel->scroll.y = 0;
    }

    debug_ui_push_clip_rect(group_rect);

    debug_ui_push_layout(DIR_VERTICAL);

    return true;
}

struct debug_ui_list_clipper debug_ui_begin_list_clipper(u32 item_size, u32 item_count)
{
    struct debug_ui_panel *panel = g_debug_ui.panel_stack[g_debug_ui.panel_stack_index - 1];
    s32 size_y = (panel->rect.bottom - panel->rect.top);
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
    struct debug_ui_panel *group = get_current_panel();

    struct debug_ui_layout *layout = &g_debug_ui.layout_stack[g_debug_ui.layout_stack_index - 1];

    group->content_size.y = layout->bounds.bottom - layout->bounds.top;
    group->content_size.x = layout->bounds.right - layout->bounds.left;

    debug_ui_pop_layout();

    debug_ui_pop_clip_rect();

    debug_ui_pop_panel();
}

void debug_ui_set_scroll(s32 amount)
{
    struct debug_ui_panel *panel = get_current_panel();
    s32 height = panel->rect.bottom - panel->rect.top;
    
    if (panel->content_size.y > height)
    {
        panel->scroll.y = amount;
        if (panel->scroll.y < 0)
            panel->scroll.y = 0;
        else if ((panel->scroll.y + height) > panel->content_size.y)
            panel->scroll.y = panel->content_size.y - height;
    }
}

void debug_ui_checkbox(b8 *value, const char *label)
{
    //vec2i pos = debug_ui_next_pos();
    uintptr_t ptr = (uintptr_t)value;
    u32 id = (u32)murmur3_mix64(ptr);

    struct debug_ui_style *style = &g_debug_ui.style;

    u32 height = font_line_height + 2 * style->pad_y;
    u32 width = height;
    vec2i pos = advance_layout(width, height);
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

    debug_ui_quad(color, r2(pos.x, pos.y, width, height));
    if (*value)
        push_text("X", 1, pos.x + style->pad_x, pos.y + font_line_height, style->text_color);

    //advance_layout(width, height);

    if (label)
        debug_ui_label(label);
}

b8 debug_ui_textbox(char *buffer, u32 buffer_len, u32 flags)
{
    // returns true on submit
    uintptr_t ptr = (uintptr_t)buffer;
    u32 id = (u32)murmur3_mix64(ptr);

    b8 submitted = false;
    
    struct debug_ui_style *style = &g_debug_ui.style;

    struct debug_ui_panel *panel = get_current_panel();

    s32 width = 120 + style->pad_x * 2;
    s32 height = font_line_height + style->pad_y * 2;

    vec2i pos = advance_layout(width, height);

    rect2 rect = r2(pos.x, pos.y, width, height);

    buffer[buffer_len - 1] = '\0';
    u32 len = strlen(buffer);

    vec2i mousep = g_debug_ui.mouse_pos;
    rect2 clip_rect = g_debug_ui.clip_stack[g_debug_ui.clip_stack_index - 1];
    rect2 a;
    a.left = MAX(rect.left, clip_rect.left);
    a.top = MAX(rect.top, clip_rect.top);
    a.right = MIN(rect.right, clip_rect.right);
    a.bottom = MIN(rect.bottom, clip_rect.bottom);

    struct debug_ui_panel *root = g_debug_ui.root_stack[g_debug_ui.root_stack_index - 1];
    b8 window_hovered = (g_debug_ui.hot_panel == root->id);
    b8 mouse_over = point_in_rect(mousep, a);
    if (mouse_over && window_hovered)
    {
        if (!g_debug_ui.active || g_debug_ui.active == id)
        {
            g_debug_ui.hot = id;
            if (g_debug_ui.mouse_pressed & DEBUG_UI_MOUSE_LEFT)
            {
                g_debug_ui.active = id;
                if (flags & TEXTBOX_FLAG_RESET_ON_EDIT)
                {
                    buffer[0] = '\0';
                    len = 0;
                }
                g_debug_ui.current_buffer_pos = len;
            }
        }
    }
    else if (g_debug_ui.hot == id)
    {
        g_debug_ui.hot = 0;
    }

    u32 color;
    if (g_debug_ui.active == id)
    {
        color = style->active;
        u8 key = g_debug_ui.key_pressed;
        u32 cursor = g_debug_ui.current_buffer_pos;
        if (key >= 32 && key <= 126 && len < (buffer_len - 1))
        {
            u32 remaining = len - cursor;
            // shift elements forward
            while (remaining--)
            {
                buffer[cursor + remaining] = buffer[cursor + remaining - 1];
            }
            buffer[cursor] = key;
            ++g_debug_ui.current_buffer_pos;
            ++len;
            buffer[len] = '\0';
        }
        else if (key == 8)
        {
            // backspace
            u32 remaining = len - cursor;
            for (u32 i = 0; i < remaining; ++i)
            {
                buffer[cursor + i - 1] = buffer[cursor + i];
            }
            if (cursor)
            {
                --len;
                --g_debug_ui.current_buffer_pos;
                buffer[len] = '\0';
            }
        }
        else if (key == 13)
        {
            submitted = true;
            g_debug_ui.active = 0;
        }

        if (g_debug_ui.mouse_buttons && g_debug_ui.hot != id)
        {
            g_debug_ui.active = 0;
        }
    }
    else
    {
        color = style->base;
    }

    debug_ui_quad(color, rect);

    push_text(buffer, len, pos.x, pos.y + font_line_height, style->text_color);

    return submitted;
}
