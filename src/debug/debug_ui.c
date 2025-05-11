#include "debug_ui.h"
#include "atlas.h"
#include "platform/platform.h"

//b8 g_update_ui;
#define MAX_UI_PANELS 32
#define MAX_UI_LAYOUT_STACK 8
#define MAX_UI_CLIP_STACK 8
#define MAX_UI_ID_STACK 8

#define MAX_UI_FILE_PATH 260

#define WINDOW_BORDER_SIZE 5

enum
{
    PANEL_FLAG_FIRST_FRAME = 0x1,
    PANEL_FLAG_IS_ENABLED = 0x2
};

struct debug_ui_panel
{
    u32 id;
    u32 flags;
    vec2i pos;
    vec2i size;
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

struct debug_ui_state
{
    u32 hot;
    u32 active;
    u32 hot_panel;
    vec2i mouse_pos;
    vec2i last_mouse_pos;
    vec2i mouse_delta;
    s32 mouse_wheel_delta;
    u8 mouse_buttons; /* state of buttons bits: 0 - left, 1 - right, 2 - middle */
    u8 mouse_pressed; /* mouse button went down */
    u8 key_pressed;
    b8 dialog_is_open; // is a dialog currently open; blocks other dialogs from opening
    f32 dt;
    vec2u screen_dim;
    u8 *commands;
    u32 commands_at;
    u32 next_command;
    u64 frame_counter;
    char file_dialog_dir[MAX_UI_FILE_PATH];
    struct debug_ui_panel panels[MAX_UI_PANELS];
    struct debug_ui_panel *current;
    struct debug_ui_panel *sentinel;
    struct debug_ui_command_jump *first;
    struct debug_ui_layout layout_stack[MAX_UI_LAYOUT_STACK];
    rect2 clip_stack[MAX_UI_CLIP_STACK];
    u32 id_stack[MAX_UI_ID_STACK];
    u32 layout_stack_index;
    u32 clip_stack_index;
    u32 id_stack_index;
};

struct debug_ui_state g_debug_ui;

static inline void *push_command(enum debug_ui_command_type type, u32 size)
{
    struct debug_ui_command_header *result = (struct debug_ui_command_header *)(g_debug_ui.commands + g_debug_ui.commands_at);
    result->type = type;
    result->size = size;
    g_debug_ui.commands_at += size;
    return result;
}

static inline void *push_jump(u32 dst)
{
    struct debug_ui_command_jump *result = push_command(DEBUG_UI_COMMAND_JUMP, sizeof(struct debug_ui_command_jump));
    result->dst = dst;
    return result;
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
    struct debug_ui_panel *current = g_debug_ui.sentinel->next;
    if (current == g_debug_ui.sentinel)
        return NULL;
    g_debug_ui.first->dst = current->commands;

    vec2i mousep = g_debug_ui.mouse_pos;
    while (current != g_debug_ui.sentinel)
    {
        if (current->last_frame_touched < g_debug_ui.frame_counter)
        {
            current->next->prev = current->prev;
            current->prev->next = current->next;
        }
        else
        {
            rect2 bounds = {current->pos.x, current->pos.y, current->pos.x + current->size.x, current->pos.y + current->size.y};
            if (point_in_rect(mousep, expand_rect(bounds, WINDOW_BORDER_SIZE)))
            {
                result = current;
            }
#if 0
            if (current->next == g_debug_ui.sentinel)
                current->end_jmp->dst = g_debug_ui.commands_at;
            else
                current->end_jmp->dst = current->next->commands;
#endif
            current->end_jmp->dst = current->next->commands;
        }

        current = current->next;
    }
    return result;
}

void debug_ui_init(struct memory_arena *arena)
{
    g_debug_ui.commands = push_arena(arena, KILOBYTES(256));
    SY_ASSERT(g_debug_ui.commands);
    g_debug_ui.sentinel = &g_debug_ui.panels[0];
    g_debug_ui.sentinel->next = g_debug_ui.sentinel->prev = g_debug_ui.sentinel;
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

    struct debug_ui_panel *hot = update_panel_states();
    if (hot)
    {
        g_debug_ui.hot_panel = hot->id;
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

static vec2i layout_next_pos(s32 widget_width, s32 widget_height)
{
    struct debug_ui_layout *layout = &g_debug_ui.layout_stack[g_debug_ui.layout_stack_index - 1];
    vec2i result = {.data = {layout->at_x, layout->at_y}};
    
    switch (layout->dir)
    {
    case DIR_HORIZONTAL:
        layout->at_x += widget_width;
        break;
    case DIR_VERTICAL:
        layout->at_y += widget_height;
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
            width += 6; // TODO: font metrics
            continue;
        }
        
        width += glyphs[c].advance;
    }

    if (p_len)
        *p_len = len;

    return width;
}

enum debug_ui_button_interaction_result
{
    DEBUG_UI_BUTTON_NONE,
    DEBUG_UI_BUTTON_HOVERED,
    DEBUG_UI_BUTTON_PRESSED,
    DEBUG_UI_BUTTON_RELEASED
};

static enum debug_ui_button_interaction_result debug_ui_button_interaction(u32 id, rect2 bounds)
{
    enum debug_ui_button_interaction_result result = DEBUG_UI_BUTTON_NONE;

    vec2i mousep = g_debug_ui.mouse_pos;

    rect2 clip_rect = g_debug_ui.clip_stack[g_debug_ui.clip_stack_index - 1];
    // clip interaction area
    rect2 a;
    a.left = MAX(bounds.left, clip_rect.left);
    a.top = MAX(bounds.top, clip_rect.top);
    a.right = MIN(bounds.right, clip_rect.right);
    a.bottom = MIN(bounds.bottom, clip_rect.bottom);

    b8 window_hovered = (g_debug_ui.hot_panel == g_debug_ui.current->id);
    if (point_in_rect(mousep, a) && window_hovered)
    {
        if (!g_debug_ui.active || g_debug_ui.active == id)
        {
            result = DEBUG_UI_BUTTON_HOVERED;
            g_debug_ui.hot = id;
            if (g_debug_ui.mouse_pressed & DEBUG_UI_MOUSE_LEFT)
            {
                g_debug_ui.active = id;
            }
        }
    }
    else
    {
        g_debug_ui.hot = 0;
    }

    if (g_debug_ui.active == id)
    {
        result = DEBUG_UI_BUTTON_PRESSED;
        if (!(g_debug_ui.mouse_buttons & DEBUG_UI_MOUSE_LEFT)) 
        {
            if (g_debug_ui.hot == id)
            {
                result = DEBUG_UI_BUTTON_RELEASED;
            }
            g_debug_ui.active = 0;
        }
    }

    return result;
}

b8 debug_ui_button(const char *text)
{
    SY_ASSERT(g_debug_ui.current);
    
    int len;
    int text_width = get_text_width(text, &len);

    int ascender = 14;
    int descender = 4;
    int height = 18;

    vec2i padding = {{4, 2}};
    int button_w = text_width + padding.x * 2;
    int button_h = height + padding.y * 2;

    vec2i pos = layout_next_pos(button_w, button_h);

    rect2 bounds = {pos.x, pos.y, pos.x + button_w, pos.y + button_h};

    b8 result = false;
    u32 color = 0x004f4f4f;
    
    u32 id = debug_ui_get_id_from_string(text);
    enum debug_ui_button_interaction_result interaction = debug_ui_button_interaction(id, bounds);
    result = (interaction == DEBUG_UI_BUTTON_RELEASED);
    
    if (interaction == DEBUG_UI_BUTTON_HOVERED)
        color = 0x00646464;
    else if (interaction == DEBUG_UI_BUTTON_PRESSED)
        color = 0x003f3f3f;

    struct debug_ui_command_quad *quad = push_command(DEBUG_UI_COMMAND_QUAD, sizeof(struct debug_ui_command_quad));
    quad->color = color;
    quad->r = bounds;

    struct debug_ui_command_text *txt = push_command(DEBUG_UI_COMMAND_TEXT, sizeof(struct debug_ui_command_text) + len + 1);
    txt->pos = v2i(bounds.left + padding.x, bounds.top + padding.y + ascender);

    char *dst = (char *)(txt + 1);
    memcpy(dst, text, len);
    dst[len] = '\0';

    return result;
}

void debug_ui_label(const char *label)
{
    int len;
    int text_width = get_text_width(label, &len);
    vec2i pos = layout_next_pos(text_width, 18);
    struct debug_ui_command_text *txt = push_command(DEBUG_UI_COMMAND_TEXT, sizeof(struct debug_ui_command_text) + len + 1);
    //txt->pos = v2i(layout->at_x, layout->at_y + font_line_height);
    txt->pos.x = pos.x;
    txt->pos.y = pos.y + font_line_height;
    //layout->at_x += text_width + layout->column_pad;
    char *dst = (char *)(txt + 1);
    memcpy(dst, label, len);
    dst[len] = '\0';
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
                panel->flags &= ~(PANEL_FLAG_FIRST_FRAME);
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
    result->last_frame_touched = g_debug_ui.frame_counter;
    result->id = id;
    result->flags = PANEL_FLAG_FIRST_FRAME;
    return result;
}

void debug_ui_open_file_dialog(const char *title)
{
    if (g_debug_ui.dialog_is_open)
        return;
    u32 id = fnv1a(title);
    struct debug_ui_panel *panel = get_panel(id);
    // TODO: reset scroll
    panel->flags |= PANEL_FLAG_IS_ENABLED;
    g_debug_ui.dialog_is_open = true;
}

b8 debug_ui_file_dialog(const char *title, const char **file_types, u32 num_file_types, struct debug_ui_file_dialog_result *out_file)
{
    u32 id = fnv1a(title);
    struct debug_ui_panel *panel = get_panel(id);
    if (g_debug_ui.key_pressed == 27)
    {
        panel->flags &= ~(PANEL_FLAG_IS_ENABLED);
        g_debug_ui.dialog_is_open = false;
    }

    if (!(panel->flags & PANEL_FLAG_IS_ENABLED))
        return false;

    u32 window_w = g_debug_ui.screen_dim.x;
    u32 window_h = g_debug_ui.screen_dim.y;

    int panel_width = 0.65f * window_w;
    int panel_height = 0.65f * window_h;
    int panel_x = (window_w - panel_width) / 2;
    int panel_y = (window_h - panel_height) / 2;
    panel->pos.x = panel_x;
    panel->pos.y = panel_y;
    //struct debug_ui_layout layout = debug_ui_make_layout(panel_x, panel_y, 0, 5, 20);
    
    debug_ui_push_layout(DIR_VERTICAL, panel_x, panel_y);
    rect2 dialog_rect = {panel_x, panel_y, panel_x + panel_width, panel_y + panel_height};
    debug_ui_quad(0x343434, panel_x, panel_y, panel_width, panel_height);
    debug_ui_label(title);
    
    debug_ui_push_layout(DIR_HORIZONTAL, panel_x + panel_width - 20, panel_y);
    // TODO: ID collision resolver/combiner
    if (debug_ui_button("X"))
    {
        panel->flags &= ~(PANEL_FLAG_IS_ENABLED);
        g_debug_ui.dialog_is_open = false;
    }
    debug_ui_pop_layout();
    //debug_ui_layout_row();

    debug_ui_push_clip_rect(dialog_rect);

    char *dir = g_debug_ui.file_dialog_dir;
    u32 dir_len;
    if (panel->flags & PANEL_FLAG_FIRST_FRAME)
    {
        u32 dir_len = GetCurrentDirectoryA(MAX_PATH, dir);
        SY_ASSERT(dir_len <= (MAX_PATH - 3));
        char *p = dir + dir_len;
        *p++ = '\\';
        *p++ = '*';
        *p = '\0';
    }
    else
    {
        dir_len = (u32)strlen(dir) - 2;
    }

    if (debug_ui_button("../"))
    {
        u32 len = dir_len;
        char *p = dir + len;
        //--len;
        SY_ASSERT(*p == '\\');
        while (len--)
        {
            char *c = dir + len;
            if (*c == '\\')
            {
                dir[len + 1] = '*';
                dir[len + 2] = '\0';
                dir_len = len;
                break;
            }
        }
        //printf("%s\n", dir);
    }

    b8 result = false;
    
    WIN32_FIND_DATAA data;
    HANDLE find = FindFirstFileA(dir, &data);
    SY_ASSERT(find != INVALID_HANDLE_VALUE);
    do
    {
        char name[MAX_PATH];
        b8 is_dir = false;
        int file_type = -1;
        const char *file;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if ((strcmp(data.cFileName, ".") == 0) || (strcmp(data.cFileName, "..") == 0))
                continue;
            is_dir = true;
            snprintf(name, ARRAYCOUNT(name), "%s/", data.cFileName);
            file = &name[0];
        }
        else 
        {
            if (file_types)
            {
                for (u32 i = 0; i < num_file_types; ++i)
                {
                    if (string_ends_with_ignore_case(data.cFileName, file_types[i]))
                    {
                        file_type = i;
                        break;
                    }
                }

                if (file_type < 0)
                    continue;
            }
            file = &data.cFileName[0];
        }
        
        if (debug_ui_button(file))
        {
            if (is_dir)
            {
                // append folder to path
                char *p = dir + dir_len + 1;
                snprintf(p, MAX_UI_FILE_PATH - dir_len - 2, "%s\\*", data.cFileName);
                dir_len = strlen(dir) - 2;
            }
            else
            {
                out_file->index = file_type;
                
                u32 path_len = dir_len + 1;
                SY_ASSERT((path_len + strlen(data.cFileName)) < MAX_PATH);
                memcpy(out_file->path, dir, path_len);
                strcpy(out_file->path + path_len, data.cFileName);
                result = true;
                break;
            }
        }
    } while (FindNextFileA(find, &data) != FALSE);
    FindClose(find);

    debug_ui_pop_clip_rect();

    debug_ui_pop_layout();

    if (result) 
    {
        panel->flags &= ~(PANEL_FLAG_IS_ENABLED);
        g_debug_ui.dialog_is_open = false;
    }

    //g_debug_ui.current_panel = g_debug_ui.current_panel->prev;

    return result;
}

static void bring_panel_to_front(struct debug_ui_panel *panel)
{
    panel->next->prev = panel->prev;
    panel->prev->next = panel->next;

    struct debug_ui_panel *top = g_debug_ui.sentinel->prev;
    top->next = panel;
    panel->prev = top;
    panel->next = g_debug_ui.sentinel;
    g_debug_ui.sentinel->prev = panel;
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

    if (window->flags & PANEL_FLAG_FIRST_FRAME)
    {
        window->flags |= PANEL_FLAG_IS_ENABLED;
        window->pos.x = rect.left;
        window->pos.y = rect.top;
        window->size.x = rect.right;
        window->size.y = rect.bottom;

        struct debug_ui_panel *top = g_debug_ui.sentinel->prev;
        top->next = window;
        window->prev = top;
        window->next = g_debug_ui.sentinel;
        g_debug_ui.sentinel->prev = window;

        if (point_in_rect(mousep, expand_rect(rect, WINDOW_BORDER_SIZE)) && !g_debug_ui.active)
            g_debug_ui.hot_panel = id;
    }

    //if (!(window->flags & PANEL_FLAG_IS_ENABLED))
    //    return false;

    rect2 window_rect = {.left = window->pos.x, .right = window->pos.x + window->size.x, .top = window->pos.y, .bottom = window->pos.y + window->size.y};

    debug_ui_quad(0x1e1e1e, window->pos.x - 1, window->pos.y - 1, window->size.x + 2, window->size.y + 2);

    debug_ui_quad(0x363636, window->pos.x, window->pos.y, window->size.x, window->size.y);
#if 0
    debug_ui_quad(0xff0000, window_rect.right, window_rect.top, 5, window->size.y);
    debug_ui_quad(0x00ff00, window_rect.left - 5, window_rect.top, 5, window->size.y);
    debug_ui_quad(0x0000ff, window_rect.left, window_rect.top - 5, window->size.x, 5);
    debug_ui_quad(0x0000ff, window_rect.left, window_rect.bottom, window->size.x, 5);
#endif
    debug_ui_push_clip_rect(window_rect);

    int title_bar_height = font_line_height + 5;
    debug_ui_push_layout(DIR_VERTICAL, window->pos.x, window->pos.y + title_bar_height);

    rect2 title_bar_rect = {window->pos.x, window->pos.y, window->pos.x + window->size.x, window->pos.y + title_bar_height};
    debug_ui_quad(0x1e1e1e, window->pos.x, window->pos.y, window->size.x, title_bar_height);

    if (p_open)
    {
        debug_ui_push_layout(DIR_HORIZONTAL, window_rect.right - 13, title_bar_rect.top);
        if (debug_ui_button("X"))
        {
            *p_open = false;
        }
        debug_ui_pop_layout();
    }

    enum window_interaction
    {
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
                g_debug_ui.active = id;
                interaction = WINDOW_INTERACTION_MOVE;
            }
            else if (point_in_rect(mousep, r2(window_rect.right, window_rect.top, window_rect.right + 5, window_rect.bottom)))
            {
                g_debug_ui.active = id;
                interaction = WINDOW_INTERACTION_SIZE_RIGHT;
                mouse_grab_offset.x = g_debug_ui.mouse_pos.x - window_rect.left;
                window_size_on_grab.x = window->size.x - mouse_grab_offset.x;
            }
            else if (point_in_rect(mousep, r2(window_rect.left - 5, window_rect.top, window_rect.left, window_rect.bottom)))
            {
                g_debug_ui.active = id;
                interaction = WINDOW_INTERACTION_SIZE_LEFT;
                mouse_grab_offset.x = g_debug_ui.mouse_pos.x - window_rect.left;
                window_size_on_grab.x = window->size.x - mouse_grab_offset.x;
            }
            else if (point_in_rect(mousep, r2(window_rect.left, window_rect.top - 5, window_rect.right, window_rect.top)))
            {
                g_debug_ui.active = id;
                interaction = WINDOW_INTERACTION_SIZE_TOP;
                mouse_grab_offset.y = g_debug_ui.mouse_pos.y - window_rect.top;
                window_size_on_grab.y = window->size.y - mouse_grab_offset.y;
            }
            else if (point_in_rect(mousep, r2(window_rect.left, window_rect.bottom, window_rect.right, window_rect.bottom + 5)))
            {
                g_debug_ui.active = id;
                interaction = WINDOW_INTERACTION_SIZE_BOTTOM;
                mouse_grab_offset.y = g_debug_ui.mouse_pos.y - window_rect.top;
                window_size_on_grab.y = window->size.y - mouse_grab_offset.y;
            }
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
        }

        if (!(g_debug_ui.mouse_buttons & DEBUG_UI_MOUSE_LEFT))
        {
            g_debug_ui.active = 0;
        }
    }

    return true;
}

void debug_ui_end_window(void)
{
    struct debug_ui_layout *window_layout = &g_debug_ui.layout_stack[g_debug_ui.layout_stack_index - 1];
    struct debug_ui_panel *window = g_debug_ui.current;
    debug_ui_pop_clip_rect(); // window_rect
    window->end_jmp = push_jump(0);
    //window->size.x = window_layout->width;
    //window->size.y = window_layout->height + (font_line_height + 5); // TODO: remove
    debug_ui_pop_layout();
    debug_ui_pop_id();
    g_debug_ui.current = NULL;
}
