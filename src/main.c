#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <shellapi.h>
#include <math.h>

#include "platform/platform.h"
#include "config.h"
#include "stream.h"
#include "input.h"
#include "debug/debug_ui.h"

#include "psx.h"
#include "debug.h"
#include "allocator.h"
#include "disasm.h"
#include "gpu.h"
#include "spu.h"
#include "audio/audio.h"
#include "renderer/sw_renderer.h"
#include "renderer/win32_renderer.h"

static volatile b32 g_running = true;

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    LRESULT result = 0;
    switch (msg)
    {
    case WM_SIZE:
    {
        if (g_renderer)
        {
            u32 width = lParam & 0xffff;
            u32 height = (lParam >> 16) & 0xffff;

            g_renderer->handle_resize(width, height);
        }
    } break;
    case WM_CLOSE:
        g_running = false;
        break;
    case WM_KEYDOWN:
        debug_ui_keydown(wParam);
    case WM_KEYUP:
        if (wParam < 128)
        {
            if (g_sio.devices[0])
            {
                struct keyboard_pad *kbd = (struct keyboard_pad *)g_sio.devices[0];
                kbd->keystates[wParam] = !((u32)lParam >> 31);
            }
        }
        break;
    case WM_MOUSEMOVE:
    {
        int mouse_x = (s32)((s16)lParam);
        int mouse_y = ((s32)lParam) >> 16;
        debug_ui_mousemove(mouse_x, mouse_y);
        break;
    }
    case WM_LBUTTONDOWN: debug_ui_mousedown(DEBUG_UI_MOUSE_LEFT); break;
    case WM_RBUTTONDOWN: debug_ui_mousedown(DEBUG_UI_MOUSE_RIGHT); break;
    case WM_MBUTTONDOWN: debug_ui_mousedown(DEBUG_UI_MOUSE_MIDDLE); break;
    case WM_LBUTTONUP: debug_ui_mouseup(DEBUG_UI_MOUSE_LEFT); break;
    case WM_RBUTTONUP: debug_ui_mouseup(DEBUG_UI_MOUSE_RIGHT); break;
    case WM_MBUTTONUP: debug_ui_mouseup(DEBUG_UI_MOUSE_MIDDLE); break;
    case WM_MOUSEWHEEL:
    {
        s32 amt = ((s32)wParam) >> 16;
        amt /= WHEEL_DELTA;
        debug_ui_mousewheel(amt);
        break;
    }
    default:
        result = DefWindowProcA(hwnd, msg, wParam, lParam);
        break;
    }
    return result;
}

DWORD WINAPI present_thread_func(LPVOID lpParameter)
{
    // TODO: resizing?
    for (;;)
    {
        if (platform_wait_event(&g_present_ready))
        {
            g_renderer->present();
            // NOTE: main thread can set event again before we reset it here
            platform_reset_event(&g_present_ready);
        }
    }
}

static inline const char *spu_voice_state_to_str(int voice)
{
    switch (g_spu.voice.internal[voice].state)
    {
    case ADSR_ATTACK:
        return "Attack";
    case ADSR_DECAY:
        return "Decay";
    case ADSR_SUSTAIN:
        return "Sustain";
    case ADSR_RELEASE:
        return "Release";
    default:
        return "";
    }
}

enum system_state
{
    SYSTEM_STATE_STOPPED,
    SYSTEM_STATE_PAUSED,
    SYSTEM_STATE_RUNNING
} state = SYSTEM_STATE_STOPPED;

static b8 show_menu = false;
static b8 show_voices = false;
static b8 show_debug = false;
static b8 show_files = false;
static char current_dir[MAX_PATH];

void draw_debug_ui(u32 width, u32 height)
{
    debug_ui_begin(0.0f, width, height);
    
    if (debug_ui_begin_window("Menu", r2(100, 100, 300, 300), 0, &show_menu))
    {
        if (debug_ui_button("Pause"))
        {
            if (state == SYSTEM_STATE_PAUSED)
                state = SYSTEM_STATE_RUNNING;
            else if (state == SYSTEM_STATE_RUNNING)
                state = SYSTEM_STATE_PAUSED;
        }

        if (debug_ui_button("Debug")) { show_debug = true; }

        if (debug_ui_button("Voice")) { show_voices = true; }

        if (debug_ui_button("Load")) { show_files = true; }

        if (debug_ui_button("Reset"))
        {
            if (psx_can_boot())
            {
                psx_reset();
                state = SYSTEM_STATE_RUNNING;
            }
        }

        debug_ui_end_window();
    }

    if (debug_ui_begin_window("Debugger", r2(0, 0, 400, 200), 0, &show_debug))
    {
        vec2i size = debug_ui_get_window_size();
        if (debug_ui_button("Step Into"))
        {
            if (state == SYSTEM_STATE_PAUSED)
                psx_step();
        }

        char buf[256];
        char param[64];
        u32 base = g_cpu.pc & 0xfffffff0;
        //f32 num_elements = (f32)size.y / 18;
        //debug_ui_begin_list("Watch", -1, 18);
        for (int i = 0; i < 32; ++i)
        {
            //u32 addr = (base + i) * 4;
            u32 addr = base;
            
            if (addr == g_cpu.pc)
            {
                vec2i pos = debug_ui_next_pos();
                debug_ui_quad(0xb0af5b, pos.x, pos.y, size.x, 18);
            }
                
            const char *op = instr_to_string(fetch_instruction(addr), param, sizeof(param));
            snprintf(buf, sizeof(buf), "%08X    %s %s", addr, op, param);
            //printf("%s\n", buf);
            debug_ui_label(buf);
            //addr += 4;
            base += 4;
        }
        //debug_ui_end_list();
        
        debug_ui_end_window();
    }

    if (debug_ui_begin_window("Voices", r2(100, 50, 500, 500), 0, &show_voices))
    {
        char buf[256];
        for (int i = 0; i < 24; ++i)
        {
            snprintf(buf, sizeof(buf), "Voice #%d: %s | ADSR: %d | ENDX: %s", i, spu_voice_state_to_str(i), g_spu.voice.data[i].adsr_volume, g_spu.cnt.endx & (1 << i) ? "true" : "false");
            debug_ui_label(buf);
            //snprintf(buf, sizeof(buf), "ADSR: %d", g_spu.voice.data[i].adsr_volume);
            //debug_ui_label(buf);
            //debug_ui_layout_row();
        }
        debug_ui_end_window();
    }

    int panel_w = 0.65f * width;
    int panel_h = 0.65f * height;
    int panel_x = (width - panel_w) / 2;
    int panel_y = (height - panel_h) / 2;

    if (debug_ui_begin_window("Image Select", r2(panel_x, panel_y, panel_w, panel_h), 0, &show_files))
    {
        char path[MAX_PATH];

        char *dir = current_dir;
        u32 dir_len = 0;
        if (current_dir[0] == '\0')
        {
            u32 dir_len = GetCurrentDirectoryA(MAX_PATH, dir);
            SY_ASSERT(dir_len <= (MAX_PATH - 3) && (dir_len != 0));
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
                s32 file_type_index = -1;
                const char *file_ext[] = {".exe", ".bin", ".cue"};
                for (u32 i = 0; i < ARRAYCOUNT(file_ext); ++i)
                {
                    if (string_ends_with_ignore_case(data.cFileName, file_ext[i]))
                    {
                        file_type_index = i;
                        break;
                    }
                }

                if (file_type_index < 0)
                    continue;
                file = &data.cFileName[0];
            }
            
            if (debug_ui_button(file))
            {
                if (is_dir)
                {
                    // append folder to path
                    char *p = dir + dir_len + 1;
                    snprintf(p, MAX_PATH - dir_len - 2, "%s\\*", data.cFileName);
                    dir_len = strlen(dir) - 2;
                }
                else
                {   
                    u32 path_len = dir_len + 1;
                    SY_ASSERT((path_len + strlen(data.cFileName)) < MAX_PATH);
                    memcpy(path, dir, path_len);
                    strcpy(path + path_len, data.cFileName);
                    result = true;
                    break;
                }
            }
        } while (FindNextFileA(find, &data) != FALSE);
        FindClose(find);

        if (result)
        {
            show_files = false;
            if (psx_can_boot())
            {
                if (psx_load_image(path))
                {
                    state = SYSTEM_STATE_RUNNING;
                }
                else
                {
                    printf("Error loading file: %s\n", path);
                }
            }
        }

        debug_ui_end_window();
    }

    debug_ui_end();
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow)
{
#if 1
    AllocConsole();
    FILE *output;
    freopen_s(&output, "CONOUT$", "w", stdout);
    freopen_s(&output, "CONOUT$", "w", stderr);
    g_debug.output = output;

    SetConsoleTitleA("Sunny Console");
    HANDLE conout = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD current_mode = 0;
    GetConsoleMode(conout, &current_mode);
    current_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(conout, current_mode);
#endif
    g_debug.sound_buffer = VirtualAlloc(0, MEGABYTES(16), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    char *class_name = "SunnyWindowClass";
    WNDCLASSA wc = {0};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = class_name;
    RegisterClassA(&wc);

    DWORD dwStyle = WS_OVERLAPPEDWINDOW | WS_VISIBLE;

    RECT desired_rect = {0, 0, VRAM_WIDTH, VRAM_HEIGHT};
    AdjustWindowRectEx(&desired_rect, dwStyle, FALSE, 0);

    HWND hwnd = CreateWindowExA(0, class_name, "Sunny", dwStyle, CW_USEDEFAULT, CW_USEDEFAULT,
        desired_rect.right - desired_rect.left, desired_rect.bottom - desired_rect.top, NULL, NULL, hInstance, NULL);
    if (!hwnd)
    {
        debug_log("Could not create the window handle!\n");
        return -1;
    }

    struct memory_arena arena = allocate_arena(MEGABYTES(16));
    memset(arena.base, 0, arena.size);

    load_config();

    struct file_dat bios;
    allocate_and_read_file(g_config.bios_path, 0, &bios);

    psx_init(&arena, bios.memory);

    platform_window window = {0};
    window.handle = hwnd;

    if (g_config.software_rendering)
    {
        g_gpu.software_rendering = true;
        g_renderer = (renderer_context *)platform_init_software_renderer(&window);
    }
    else 
    {
        g_renderer = win32_load_renderer_from_dll(hwnd, hInstance, "vulkan_renderer.dll");
    }

    g_sio.devices[0] = push_arena(&arena, sizeof(struct keyboard_pad));
    g_sio.devices[0]->type = INPUT_DEVICE_DIGITAL_PAD;
    g_sio.devices[0]->input_get_data = keyboard_get_digital_pad_input;

    audio_player *audio = audio_init();

    platform_create_event(&g_present_ready, false);

    HANDLE present_thread_handle = CreateThread(NULL, 0, present_thread_func, NULL, 0, NULL);

    debug_ui_init(&arena);

    LARGE_INTEGER begin_counter, end_counter, frequency;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&begin_counter);

    if (psx_can_boot())
    {
        psx_load_image(g_config.boot_file);
        state = SYSTEM_STATE_RUNNING;
    }

    b8 key_was_down = false;

    while (g_running)
    {
        MSG msg = {0};
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        RECT client;
        GetClientRect(hwnd, &client);
        int window_w = client.right - client.left;
        int window_h = client.bottom - client.top;

        if (GetKeyState('M') & 0x8000)
        {
            if (!key_was_down)
                show_menu = !show_menu;
            key_was_down = true;
        }
        else
        {
            key_was_down = false;
        }

        draw_debug_ui(window_w, window_h);

        if (state == SYSTEM_STATE_RUNNING)
        {
            emulate_from_audio(audio);
        }
#if 1
        if (!g_display_updated) {
            g_renderer->update_display();
        }
        g_display_updated = false;
#endif
        QueryPerformanceCounter(&end_counter);

        u64 ticks_elapsed = (end_counter.QuadPart - begin_counter.QuadPart);

        u64 elapsed_us = (ticks_elapsed * 1000000) / frequency.QuadPart;

        begin_counter = end_counter;
    }

    g_renderer->shutdown();

    free_arena(&arena);

    return 0;
}
