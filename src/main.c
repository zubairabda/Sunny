#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <shellapi.h>

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
#include "dma.h"
#include "spu.h"
#include "memory.h"
#include "audio/audio.h"
#include "renderer/sw_renderer.h"
#include "renderer/win32_renderer.h"

static volatile b32 g_running = true;

static b8 show_menu = false;

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
    case WM_CHAR:
        debug_ui_keydown(wParam);
        break;
    case WM_KEYDOWN:
        if (wParam == 'M')
            show_menu = !show_menu;
    case WM_KEYUP:
        g_keystates[wParam] = !((u32)lParam >> 31);
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

static const char *spu_voice_state_to_str(int voice)
{
    switch (g_spu.voice_data[voice].stage)
    {
    case ADSR_OFF:
        return "Off";
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

static b8 show_voices = false;
static b8 show_debug = false;
static b8 show_files = false;
static b8 show_dma = false;
static rect2 bounds = {0};
static char current_dir[MAX_PATH];
static u32 drive_mask;
static char textbuf[16];

static void pause_button(void)
{
    if (debug_ui_button("Pause"))
    {
        if (g_psx.state == SYSTEM_STATE_PAUSED)
            g_psx.state = SYSTEM_STATE_RUNNING;
        else if (g_psx.state == SYSTEM_STATE_RUNNING)
            g_psx.state = SYSTEM_STATE_PAUSED;
    }
}

static void file_browser_window(u32 width, u32 height)
{
    int panel_w = 0.65f * width;
    int panel_h = 0.65f * height;
    int panel_x = (width - panel_w) / 2;
    int panel_y = (height - panel_h) / 2;

    debug_ui_set_window_rect("Image Select", r2(panel_x, panel_y, panel_w, panel_h));
    if (debug_ui_begin_window("Image Select", 0, &show_files))
    {
        char path[MAX_PATH];

        char *dir = current_dir;
        u32 dir_len = 0;
        if (current_dir[0] == '\0')
        {
            dir_len = GetCurrentDirectoryA(MAX_PATH, dir);
            SY_ASSERT(dir_len <= (MAX_PATH - 3));
            // we are probably given an absolute path, but we'll do an extra check here anyway
            if (dir_len >= 3 && is_alpha(dir[0]) && dir[1] == ':')
            {
                // add the * wildcard, only adding a backslash if the dir doesn't end in one (which is possible if the current dir is the root drive)
                char *p = dir + dir_len;
                if (path[dir_len - 1] != '\\')
                {
                    *p++ = '\\';
                }
                else
                {
                    --dir_len;
                }
                *p++ = '*';
                *p = '\0';
            }
            else
            {
                SY_ASSERT(0);
            }
        }
        else
        {
            dir_len = (u32)strlen(dir) - 2;
        }

        debug_ui_begin_group("Files");

        if (debug_ui_button("../"))
        {
            u32 len = dir_len;
            char *p = dir + len;
            b8 found = false;

            SY_ASSERT(*p == '\\');
            while (len--)
            {
                char *c = dir + len;
                if (*c == '\\')
                {
                    dir[len + 1] = '*';
                    dir[len + 2] = '\0';
                    dir_len = len;
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                // on Windows, list the logical drives, since there is no real 'root' dir
                drive_mask = GetLogicalDrives();
            }
        }

        b8 result = false;

        if (drive_mask)
        {
            char name[4] = "?:\\";
            for (int i = 0; i < 32; ++i)
            {
                if (drive_mask & (1 << i))
                {
                    name[0] = 'A' + i;
                    if (debug_ui_button(name))
                    {
                        strcpy(dir, name);
                        char *p = dir + 3;
                        *p++ = '*';
                        *p++ = '\0';
                        drive_mask = 0;
                    }
                }
            }
            goto exit;
        }

        WIN32_FIND_DATAA data;
        HANDLE find = FindFirstFileA(dir, &data);
        if (find == INVALID_HANDLE_VALUE)
            goto exit;
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
exit:
        debug_ui_end_group();

        if (result)
        {
            show_files = false;
            if (psx_can_boot())
            {
                if (psx_load_image(path))
                {
                    g_psx.state = SYSTEM_STATE_RUNNING;
                }
                else
                {
                    printf("Error loading file: %s\n", path);
                }
            }
        }

        debug_ui_end_window();
    }
}

static void update_debug_ui(u32 width, u32 height)
{
    debug_ui_begin(width, height);

    if (debug_ui_begin_window("Menu", 0, &show_menu))
    {
        pause_button();

        if (debug_ui_button("Debug"))
            show_debug = true;

        if (debug_ui_button("Voice"))
            show_voices = true;

        if (debug_ui_button("Load"))
            show_files = true;

        if (debug_ui_button("Reset"))
        {
            if (psx_can_boot())
            {
                psx_reset();
                g_psx.state = SYSTEM_STATE_RUNNING;
            }
        }

        if (debug_ui_button("Mute"))
        {
            b8 muted = audio_is_muted();
            audio_set_mute(!muted);
        }

        if (debug_ui_button("DMA"))
            show_dma = true;

        if (debug_ui_button("Record"))
        {
            if (g_debug.recording)
            {
                g_debug.recording = false;
                write_wav_file(g_debug.sound_buffer, g_debug.sound_buffer_len * 2, "output.wav");
            }
            else
            {
                g_debug.recording = true;
                g_debug.sound_buffer_len = 0;
            }
        }

        debug_ui_end_window();
    }

    if (debug_ui_begin_window("Debugger", 0, &show_debug))
    {
        static b8 follow_pc = true;
        b8 scroll_to_pc = false;
        b8 scroll_to_addr = false;
        u32 address;

        debug_ui_push_layout(DIR_HORIZONTAL);

        pause_button();

        static u32 last_reg_values[32];
        if (debug_ui_button("Step Into"))
        {
            if (g_psx.state == SYSTEM_STATE_PAUSED)
            {
                memcpy(&last_reg_values[0], &g_cpu.registers[0], sizeof(u32) * 32);
                psx_step();
                if (follow_pc)
                    scroll_to_pc = true;
            }
        }

        if (debug_ui_button("Show PC"))
            scroll_to_pc = true;
        
        debug_ui_checkbox(&follow_pc, "Follow PC");

        if (debug_ui_textbox(textbuf, sizeof(textbuf), TEXTBOX_FLAG_RESET_ON_EDIT))
        {
            if (str_to_hex(textbuf, &address))
            {
                scroll_to_addr = true;
                debug_ui_set_scroll(((address & 0x1fffffff) >> 2) * 18);
            }
        }

        debug_ui_labelf("pc %08X", g_cpu.pc);

        debug_ui_layout_row();

        u8 row_count = 0;
        for (u8 i = 0; i < 32; ++i)
        {
            u32 reg = g_cpu.registers[i];
            if (g_psx.state == SYSTEM_STATE_PAUSED)
                debug_ui_color_labelf(last_reg_values[i] != reg ? 0x7575fa : 0xffffff, "%s %08X", register_names[i], reg);
            else
                debug_ui_labelf("%s %08X", register_names[i], g_cpu.registers[i]);

            if (row_count++ == 7)
            {
                debug_ui_layout_row();
                row_count = 0;
            }
        }
        debug_ui_layout_row();

        debug_ui_pop_layout();

        debug_ui_begin_group("Disassembly");

        rect2 rect = debug_ui_get_panel_rect();
        s32 width = rect.right - rect.left;

        char params[64];
        u32 pc = g_cpu.pc & 0x1fffffff;
        b8 pc_is_visible = false;

        struct debug_ui_list_clipper clipper = debug_ui_begin_list_clipper(18, RAM_SIZE);
        for (u32 i = clipper.start_index; i < clipper.end_index; ++i)
        {
            u32 addr = i * 4;
            
            vec2i pos = debug_ui_next_pos();
            rect2 item_rect = r2(pos.x, pos.y, width, 18);
            if (addr == pc)
            {
                pc_is_visible = true;
                debug_ui_quad(0xb0af5b, item_rect);
            }
            else if (addr & 0x4)
            {
                debug_ui_quad(0x3f3f3f, item_rect);
            }
            
            u32 press = debug_ui_button_behavior(addr, item_rect);
            if (press & DEBUG_UI_INTERACTION_PRESSED)
            {
                // add breakpoint
                if (breakpoint_get(addr))
                    breakpoint_remove(addr);
                else
                    breakpoint_set(addr);
            }

            if (breakpoint_get(addr))
            {
                debug_ui_quad(0xff0000, item_rect);
            }

            u32 instruction = fetch_instruction(addr);
            const char *op = instr_to_string(instruction, params, sizeof(params));
            debug_ui_labelf("%08X    %08X    %s %s", addr, instruction, op, params);
        }
        debug_ui_end_list_clipper(&clipper);

#if 1
        if (scroll_to_pc && !pc_is_visible)
            debug_ui_set_scroll((pc >> 2) * 18);
        else if (scroll_to_addr)
            debug_ui_set_scroll(((address & 0x1fffffff) >> 2) * 18);
#endif
        debug_ui_end_group();
        
        debug_ui_end_window();
    }

    if (debug_ui_begin_window("Voices", 0, &show_voices))
    {
        vec2i pos = debug_ui_next_pos();
        char buf[256];
        for (int i = 0; i < 24; ++i)
        {
            snprintf(buf, sizeof(buf), "Voice #%-4d: %-10s | ADSR: %-8d | ENDX: %s", i, spu_voice_state_to_str(i), g_spu.regs.voices[i].adsr_volume, g_spu.regs.control.endx & (1 << i) ? "true" : "false");
            debug_ui_label(buf);
        }
        debug_ui_end_window();
    }

    file_browser_window(width, height);

    if (debug_ui_begin_window("DMA", 0, &show_dma))
    {
        if (g_dma.active_channel >= 0)
        {
            struct dma_transfer *transfer = &g_dma.transfers[g_dma.active_channel];
            const char *modes[3] = {"burst", "slice", "linked-list"};
            const char *channels[7] = {"MDEC in", "MDEC out", "GPU", "CDROM", "SPU", "PIO", "OTC"};

            u32 addr = g_dma.ports[g_dma.active_channel].madr;

            debug_ui_labelf("Active channel: %s", channels[g_dma.active_channel]);
            debug_ui_labelf("Mode: %s", modes[transfer->transfer_mode]);
            debug_ui_labelf("Transfer address: %08X", addr);
            debug_ui_labelf("Current address: %08X", transfer->current_addr);
            if (transfer->transfer_mode != DMA_MODE_LINKEDLIST)
            {
                debug_ui_labelf("Remaining words: %u", transfer->words_left);
            }
            else
            {
                debug_ui_begin_group("transfer");
                u32 next_entry = addr;
                u32 timeout = 4096;
                for (; timeout; --timeout)
                {
                    u32 current = next_entry;
                    u32 entry = U32FromPtr(g_ram + next_entry);
                    u32 transfer_size = entry >> 24;
                    next_entry = entry & 0xffffff;
                    debug_ui_labelf("Address: %08X, Count: %u, Next: %08X", current, transfer_size, next_entry);
                    if (next_entry & 0x800000)
                        break;
                }

                debug_ui_end_group();
            }
        }
        else
        {
            debug_ui_label("No active channel.");
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

    RECT desired_rect = {0, 0, 640, 480};
    AdjustWindowRectEx(&desired_rect, dwStyle, FALSE, 0);

    HWND hwnd = CreateWindowExA(0, class_name, "Sunny", dwStyle, CW_USEDEFAULT, CW_USEDEFAULT,
        desired_rect.right - desired_rect.left, desired_rect.bottom - desired_rect.top, NULL, NULL, hInstance, NULL);
    if (!hwnd)
    {
        debug_log("Could not create the window handle!\n");
        return -1;
    }

    load_config();

    psx_init();

    g_psx.controllers[0] = malloc(sizeof(struct input_device_base));
    g_psx.controllers[0]->type = INPUT_DEVICE_DIGITAL_PAD;
    g_psx.controllers[0]->input_get_data = keyboard_get_digital_pad_input;

    psx_load_bios(g_config.bios_path);

    platform_window window = {0};
    window.handle = hwnd;

    if (!g_config.software_rendering)
    {
        MessageBoxA(hwnd, "Hardware rendering is currently unsupported.\n", "Renderer error", MB_ICONERROR | MB_OK);   
    }
    g_renderer = (renderer_context *)platform_init_software_renderer(&window);

    audio_init();
    audio_set_volume(0.5f);

    debug_ui_init();
    debug_ui_set_window_rect("Menu", r2(0, 0, 315, 200));
    debug_ui_set_window_rect("Voices", r2(100, 50, 500, 500));
    debug_ui_set_window_rect("Debugger", r2(0, 0, 600, 400));

    LARGE_INTEGER begin_counter, end_counter, frequency;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&begin_counter);

    if (psx_can_boot())
    {
        if (g_config.boot_file[0])
            psx_load_image(g_config.boot_file);
        else
            psx_reset();
        g_psx.state = SYSTEM_STATE_RUNNING;
    }

    g_debug.breakpoints_enabled = true; // TODO: temp
    g_debug.log_level = LOG_DEBUG;

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

        update_debug_ui(window_w, window_h);

        if (g_psx.state == SYSTEM_STATE_RUNNING)
        {
            emulate_from_audio();
        }
        else
        {
            Sleep(5);
        }

        g_renderer->update_display();
        
        QueryPerformanceCounter(&end_counter);

        u64 ticks_elapsed = (end_counter.QuadPart - begin_counter.QuadPart);

        u64 elapsed_us = (ticks_elapsed * 1000000) / frequency.QuadPart;

        begin_counter = end_counter;
    }

    g_renderer->shutdown();

    free(g_psx.controllers[0]);

    psx_shutdown();

    debug_ui_shutdown();

    return 0;
}
