#include "debug_ui.h"
#include "atlas.h"

#include <stdlib.h>

//b8 g_update_ui;
#define MAX_UI_PANELS 16
#define MAX_UI_LAYOUT_STACK 8
#define MAX_UI_CLIP_STACK 8

#define MAX_UI_FILE_PATH 260

struct debug_ui_panel
{
    u32 id;
    b8 is_open;
    b8 first_frame;
    vec2i pos;
    vec2i scroll;
    u64 last_frame_touched;
    //struct debug_ui_layout layout;
    struct debug_ui_panel *prev;
};

struct debug_ui_state
{
    u32 hot;
    u32 active;
    vec2i mouse_pos;
    u8 mouse_buttons; /* state of buttons bits: 0 - left, 1 - right, 2 - middle */
    u8 mouse_pressed; /* mouse button went down */
    u8 key_pressed;
    b8 dialog_is_open; // is a dialog currently open; blocks other dialogs from opening
    //vec2 scale;
    f32 dt;
    vec2u screen_dim;
    u8 *commands;
    u32 commands_at;
    u32 next_command;
    u64 frame_counter;
#if 0
    u32 reserved;
    u32 layout_stack_idx;
    struct debug_ui_layout layout_stack[MAX_UI_LAYOUT_STACK];
#endif
    char file_dialog_dir[MAX_UI_FILE_PATH];
    struct debug_ui_panel panels[MAX_UI_PANELS];
    //struct debug_ui_panel *current_panel;
    u32 layout_stack_index;
    struct debug_ui_layout layout_stack[MAX_UI_LAYOUT_STACK];
    u32 clip_stack_index;
    rect2 clip_stack[MAX_UI_CLIP_STACK];
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

void debug_ui_init(struct memory_arena *arena)
{
    g_debug_ui.commands = push_arena(arena, KILOBYTES(256));
    SY_ASSERT(g_debug_ui.commands);
}

void debug_ui_begin(f32 dt, u32 screen_w, u32 screen_h)
{
    g_debug_ui.dt = dt;
    //g_debug_ui.scale.x = (f32)screen_w / canvas_w;
    //g_debug_ui.scale.y = (f32)screen_h / canvas_h;

    g_debug_ui.commands_at = 0;
    g_debug_ui.next_command = 0;

    //g_debug_ui.current_panel = NULL;

    ++g_debug_ui.frame_counter;

    g_debug_ui.screen_dim.x = screen_w;
    g_debug_ui.screen_dim.y = screen_h;

    // NOTE: we do a hidden push here for the window clipping for screen widgets
    rect2 window_rect = {.right = screen_w, .bottom = screen_h};
    debug_ui_push_clip_rect(window_rect);
#if 0
    struct debug_ui_command_set_clip *cmd = push_command(DEBUG_UI_COMMAND_SET_CLIP, sizeof(struct debug_ui_command_set_clip));
    cmd->r.left = cmd->r.top = 0;
    cmd->r.right = screen_w;
    cmd->r.bottom = screen_h;
#endif
}

void debug_ui_end(void)
{
    SY_ASSERT(g_debug_ui.clip_stack_index == 1);
    g_debug_ui.clip_stack_index = 0;
    g_debug_ui.mouse_pressed = 0;
    g_debug_ui.key_pressed = 0;
}

void debug_ui_reset_command_ptr(void)
{
    g_debug_ui.next_command = 0;
}

struct debug_ui_command_header *debug_ui_next_command(void)
{
    if (g_debug_ui.next_command == g_debug_ui.commands_at) {
        return NULL;
    }

    struct debug_ui_command_header *cmd = (struct debug_ui_command_header *)(g_debug_ui.commands + g_debug_ui.next_command);
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

static inline b8 point_in_rect(vec2i p, rect2 r)
{
    return (p.x > r.left && p.x < r.right && p.y > r.top && p.y < r.bottom);
}

static vec2i get_next_layout_pos(int widget_width, int widget_height)
{
    struct debug_ui_layout *layout = &g_debug_ui.layout_stack[g_debug_ui.layout_stack_index - 1];
    vec2i result = {.x = layout->at_x, .y = layout->at_y};
    switch (layout->type)
    {
    case HORIZONTAL:
    {
        layout->at_x += widget_width + layout->column_pad;
        break;
    }
    case VERTICAL:
    {
        layout->at_y += widget_height + layout->row_pad;
        break;
    }
    }
    return result;
}

void debug_ui_push_layout(debug_ui_layout_type type, int x, int y)
{
    struct debug_ui_layout *layout = &g_debug_ui.layout_stack[g_debug_ui.layout_stack_index++];
    layout->type = type;
    layout->at_x = layout->start_x = x;
    layout->at_y = layout->start_y = y;
}

void debug_ui_pop_layout(void)
{
    --g_debug_ui.layout_stack_index;
}

static int get_text_width(const char *text, int *p_len)
{
    int len = 0;
    int width = 0;
    for (const char *p = text; *p; ++p)
    {
        ++len;
        int c = *p;
        if (c > 126) {
            c = '?';
        }
        else if (c == ' ') {
            width += 6; // TODO: font metrics
            continue;
        }
        
        width += glyphs[c].advance;
    }
    *p_len = len;
    return width;
}

b8 debug_ui_button(const char *text)
{
    vec2i mousep = g_debug_ui.mouse_pos;

    int len;
    int text_width = get_text_width(text, &len);

    u32 color = 0x004f4f4f;

    int ascender = 14;
    int descender = 4;
    int height = 18;

    vec2i padding = {{4, 2}};
    int button_w = text_width + padding.x * 2;
    int button_h = height + padding.y * 2;

    vec2i pos = get_next_layout_pos(button_w, button_h);

    rect2 bounds = {pos.x, pos.y, pos.x + button_w, pos.y + button_h};
    rect2 clip_rect = g_debug_ui.clip_stack[g_debug_ui.clip_stack_index - 1];
    // clip interaction area
    rect2 a;
    a.left = MAX(bounds.left, clip_rect.left);
    a.top = MAX(bounds.top, clip_rect.top);
    a.right = MIN(bounds.right, clip_rect.right);
    a.bottom = MIN(bounds.bottom, clip_rect.bottom);

    //layout->at_x += button_w + layout->column_pad;

    b8 result = false;

    u32 id = fnv1a(text);

    if (point_in_rect(mousep, a))
    {
        if (!g_debug_ui.active || g_debug_ui.active == id)
        {
            g_debug_ui.hot = id;
            color = 0x00646464;
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
        color = 0x003f3f3f;
        if (!(g_debug_ui.mouse_buttons & DEBUG_UI_MOUSE_LEFT)) 
        {
            if (g_debug_ui.hot == id)
            {
                result = true;
            }
            g_debug_ui.active = 0;
        }
    }

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
    vec2i pos = get_next_layout_pos(text_width, 18);
    struct debug_ui_command_text *txt = push_command(DEBUG_UI_COMMAND_TEXT, sizeof(struct debug_ui_command_text) + len + 1);
    //txt->pos = v2i(layout->at_x, layout->at_y + font_line_height);
    txt->pos.x = pos.x;
    txt->pos.y = pos.y + font_line_height;
    //layout->at_x += text_width + layout->column_pad;
    char *dst = (char *)(txt + 1);
    memcpy(dst, label, len);
    dst[len] = '\0';
}

void debug_ui_quad(int x, int y, int w, int h) 
{
    u32 color = 0x004f4f4f;
    rect2 bounds = {x, y, x + w, y + h};

    struct debug_ui_command_quad *quad = push_command(DEBUG_UI_COMMAND_QUAD, sizeof(struct debug_ui_command_quad));
    quad->color = color;
    quad->r = bounds;
}

void debug_ui_push_clip_rect(rect2 r)
{
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

static struct debug_ui_panel *get_panel(const char *title)
{
    u32 id = fnv1a(title);
    u32 f = g_debug_ui.frame_counter;
    u32 n;
    
    for (u32 i = 0; i < MAX_UI_PANELS; ++i)
    {
        struct debug_ui_panel *result = &g_debug_ui.panels[i];
        if (result->id == id)
        {
            result->last_frame_touched = g_debug_ui.frame_counter;
            return result;
        }
        else if (result->last_frame_touched < f)
        {
            f = result->last_frame_touched;
            n = i;
        }
    }
    // panel not found, new entry
    struct debug_ui_panel *result = &g_debug_ui.panels[n];
    result->last_frame_touched = g_debug_ui.frame_counter;
    result->id = id;
    return result;
}

void debug_ui_open_file_dialog(const char *title)
{
    if (g_debug_ui.dialog_is_open)
        return;
    struct debug_ui_panel *panel = get_panel(title);
    // TODO: reset scroll
    panel->is_open = true;
    panel->first_frame = true;
    g_debug_ui.dialog_is_open = true;
}
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <fileapi.h>
b8 debug_ui_file_dialog(const char *title, const char **file_types, u32 num_file_types, struct debug_ui_file_dialog_result *out_file)
{
    struct debug_ui_panel *panel = get_panel(title);
    if (g_debug_ui.key_pressed == 27)
    {
        panel->is_open = false;
        g_debug_ui.dialog_is_open = false;
    }

    if (!panel->is_open)
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
    
    debug_ui_push_layout(VERTICAL, panel_x, panel_y);
    rect2 dialog_rect = {panel_x, panel_y, panel_x + panel_width, panel_y + panel_height};
    debug_ui_quad(panel_x, panel_y, panel_width, panel_height);
    debug_ui_label(title);
    //debug_ui_layout_row();

    debug_ui_push_clip_rect(dialog_rect);

    char *dir = g_debug_ui.file_dialog_dir;
    u32 dir_len;
    if (panel->first_frame)
    {
        u32 dir_len = GetCurrentDirectoryA(MAX_PATH, dir);
        SY_ASSERT(dir_len <= (MAX_PATH - 3));
        char *p = dir + dir_len;
        *p++ = '\\';
        *p++ = '*';
        *p = '\0';
        panel->first_frame = false;
    }
    else
    {
        dir_len = (u32)strlen(dir) - 2;
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
                memcpy(out_file->file_name, dir, path_len);
                strcpy(out_file->file_name + path_len, data.cFileName);
                result = true;
                break;
            }
#if 0
                if (strcmp(file, "../") == 0)
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
#endif
        }
    } while (FindNextFileA(find, &data) != FALSE);
    FindClose(find);

    debug_ui_pop_clip_rect();

    debug_ui_pop_layout();

    if (result) 
    {
        panel->is_open = false;
        g_debug_ui.dialog_is_open = false;
    }

    //g_debug_ui.current_panel = g_debug_ui.current_panel->prev;

    return result;
}

void debug_ui_end_panel(void)
{
    
}
