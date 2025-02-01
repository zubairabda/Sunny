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
    //g_debug.sound_buffer = VirtualAlloc(0, MEGABYTES(16), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    char *class_name = "SunnyWindowClass";
    WNDCLASSA wc = {0};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = class_name;
    RegisterClassA(&wc);

    DWORD dwStyle = WS_OVERLAPPEDWINDOW | WS_VISIBLE;

    RECT desired_rect = {0, 0, VRAM_WIDTH, VRAM_HEIGHT};
    AdjustWindowRectEx(&desired_rect, dwStyle, FALSE, 0);

    HWND hwnd = CreateWindowExA(0, class_name, "Sunny", dwStyle, CW_USEDEFAULT, CW_USEDEFAULT,
        desired_rect.right - desired_rect.left, desired_rect.bottom - desired_rect.top, NULL, NULL, hInstance, NULL);
    if (!hwnd) {
        debug_log("Could not create the window handle!\n");
        return -1;
    }

    struct memory_arena arena = allocate_arena(MEGABYTES(16));
    memset(arena.base, 0, arena.size);

    load_config();

    struct file_dat bios;
    allocate_and_read_file(g_config.bios_path, &bios);

    psx_init(&arena, bios.memory);

    platform_window window = {0};
    window.handle = hwnd;

    if (g_config.software_rendering) {
        g_gpu.software_rendering = true;
        g_renderer = (renderer_context *)platform_init_software_renderer(&window);
    }
    else {
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
    
    enum system_state
    {
        SYSTEM_STATE_STOPPED,
        SYSTEM_STATE_PAUSED,
        SYSTEM_STATE_RUNNING
    } state = SYSTEM_STATE_STOPPED;

    b8 show_voices = false;

    if (bios.memory)
    {
        psx_mount_from_file(g_config.boot_file);
        psx_load_image();
        state = SYSTEM_STATE_RUNNING;
    }
    
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

        debug_ui_begin(0.0f, window_w, window_h);

        debug_ui_push_layout(HORIZONTAL, 0, 0);

        if (debug_ui_button("Voice")) { show_voices = !show_voices; }

        if (show_voices)
        {
            debug_ui_push_layout(VERTICAL, 0, 20);
            char buf[256];
            for (int i = 0; i < 24; ++i)
            {
                snprintf(buf, sizeof(buf), "Voice #%d: %s | ADSR: %d", i, spu_voice_state_to_str(i), g_spu.voice.data[i].adsr_volume);
                debug_ui_label(buf);
                //snprintf(buf, sizeof(buf), "ADSR: %d", g_spu.voice.data[i].adsr_volume);
                //debug_ui_label(buf);
                //debug_ui_layout_row();
            }
            debug_ui_pop_layout();
        }

        if (debug_ui_button("Pause")) 
        {
            if (state == SYSTEM_STATE_PAUSED)
                state = SYSTEM_STATE_RUNNING;
            else if (state == SYSTEM_STATE_RUNNING)
                state = SYSTEM_STATE_PAUSED; 
        }

        if (debug_ui_button("Load"))
        {
            debug_ui_open_file_dialog("Image Select");
        }

        const char *file_ext[] = {".exe", ".bin", ".cue"};
        struct debug_ui_file_dialog_result file;
        if (debug_ui_file_dialog("Image Select", file_ext, ARRAYCOUNT(file_ext), &file))
        {
            platform_file image;
            if (!platform_open_file(file.file_name, &image))
            {
                printf("Error loading file: %s\n", file.file_name);
            }
            else
            {
                psx_mount_image(image, (psx_image_type)file.index);
            }
        }

        if (debug_ui_button("Reset"))
        {
            psx_reset();
            // TODO: this is an error, because we dont handle the bios not being loaded
            state = SYSTEM_STATE_RUNNING;
        }

        debug_ui_pop_layout();

        debug_ui_end();

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
