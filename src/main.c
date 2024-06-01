#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <immintrin.h>
#include <math.h>

#include "fileio.h"
#include "platform/sync.h"

static s16 *debug_sound_buffer;
static u32 debug_sound_buffer_index;
static b8 stopkeypressed;

signal_event_handle g_present_thread_handle;
u32 g_vblank_counter;

#include "psx.h"
#include "debug.h"
#include "allocator.h"
#include "gpu.h"
#include "event.h"
#include "audio/audio.h"
#include "renderer/win32_renderer.h"
#include "input/input.h"


static volatile b32 g_running = 1;
// stub functions for the sw rasterizer
static void renderer_handle_resize(u32 width, u32 height)
{
    
}

static void renderer_update_display(HDC context, int x, int y, int width, int height)
{
   
}

struct win32_window_data
{
    struct input_state *input;
    renderer_interface *renderer;
};

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    LRESULT result = 0;
    switch (msg)
    {
    case WM_CREATE:
    {
        CREATESTRUCT *createstruct = (CREATESTRUCT *)lParam;
        SetLastError(0);
        if (!SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)createstruct->lpCreateParams) && GetLastError())
        {
            debug_log("Failed to set window attribute\n");
            result = -1;
        }
    } break;
    case WM_CLOSE:
        g_running = SY_FALSE;
        break;
    case WM_SIZE:
    {
        u32 width = lParam & 0xffff;
        u32 height = (lParam >> 16) & 0xffff;
        struct win32_window_data* data = (struct win32_window_data *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (data->renderer) // TODO: likely want to check if the renderer is hooked in some other way
        {
            data->renderer->handle_resize(data->renderer, width, height);
        }
        //renderer_handle_resize(width, height);
    } break;
#if 0
    case WM_PAINT:
    {

        PAINTSTRUCT paint;
        HDC context = BeginPaint(hwnd, &paint);
        int x = paint.rcPaint.left;
        int y = paint.rcPaint.top;
        int width = paint.rcPaint.right - paint.rcPaint.left;
        int height = paint.rcPaint.bottom - paint.rcPaint.top;
        renderer_update_display(context, x, y, width, height);
        //PatBlt(context, x, y, width, height, BLACKNESS);
        EndPaint(hwnd, &paint);
        
    }   break;
#endif
    case WM_KEYDOWN:
    case WM_KEYUP:

        if (wParam < 128)
        {
            struct win32_window_data* data = (struct win32_window_data *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
            data->input->keystates[wParam] = !((u32)lParam >> 31);
        }
        break;
    default:
        result = DefWindowProcA(hwnd, msg, wParam, lParam);
        break;
    }
    return result;
}

struct win32_thread_params
{
    renderer_interface *renderer;
};

DWORD WINAPI ThreadProc(LPVOID lpParameter)
{
    struct win32_thread_params *params = (struct win32_thread_params *)lpParameter;
    HANDLE event = (HANDLE)g_present_thread_handle;
    renderer_interface *renderer = params->renderer;
    // TODO: resizing?
    for (;;)
    {
        DWORD result = WaitForSingleObject(event, INFINITE);
        if (result == WAIT_OBJECT_0)
        {
            renderer->present(renderer);
            // NOTE: main thread can set event again before we reset it here
            ResetEvent(event);
        }
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow)
{
    AllocConsole();
    FILE *output;
    freopen_s(&output, "CONOUT$", "w", stderr);
    g_debug.output = output;

    SetConsoleTitleA("Sunny Debug Console");
    HANDLE conout = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD current_mode = 0;
    GetConsoleMode(conout, &current_mode);
    current_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(conout, current_mode);

    debug_sound_buffer = VirtualAlloc(0, MEGABYTES(16), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    char *class_name = "SunnyWindowClass";
    WNDCLASSA wc = {0};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = class_name;
    RegisterClassA(&wc);

    struct input_state input = {0};
#if 0
    if (!dinput_init(hInstance, &input)) {
        debug_log("Failed to initialize DirectInput.\n");
        //SY_ASSERT(0);
    }
#endif
    struct win32_window_data window_data = {0};
    window_data.input = &input;

    HWND window = CreateWindowExA(0, class_name, "Sunny", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, hInstance, &window_data);
    if (!window) {
        debug_log("Could not create the window handle!\n");
        return -1;
    }

#if SOFTWARE_RENDERING
    HDC hdc = GetDC(window);

    BITMAPINFO bitmap_info = {0};
    bitmap_info.bmiHeader.biSize = sizeof(bitmap_info.bmiHeader);
    bitmap_info.bmiHeader.biWidth = VRAM_WIDTH;
    bitmap_info.bmiHeader.biHeight = -VRAM_HEIGHT; // negative height so we can use 0,0 as top-left
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 16;
    bitmap_info.bmiHeader.biCompression = BI_RGB;

    g_app.window = window;
    g_app.bitmap_info = bitmap_info;
    g_app.hdc = hdc;
#else
    renderer_interface *renderer = win32_load_renderer_from_dll(window, hInstance, "vulkan_renderer.dll");
    window_data.renderer = renderer;
#endif

    struct FileInfo bios;
    char* bios_name = "SCPH1001.BIN";
    read_file(bios_name, &bios);
    // test exe's provided by amidog and Jakub
    char *exes[] = {"psxtest_cpu", "otc-test", "gp0-e1", "dpcr", "chopping", "MemoryTransfer24BPP", "clipping", "padtest", "PlayADPCMSample", "PlaySong", "timing", "access-time"};
    char filename[64];
    //snprintf(filename, 64, "exes/%s.exe", exes[11]);
    snprintf(filename, 64, "exes/timers.exe");
    //snprintf(filename, 64, "exes/cdrom.ps-exe");
    struct FileInfo test;

    read_file(filename, &test); 
    g_debug.loaded_exe = test.memory;

    struct memory_arena main_arena = allocate_arena(MEGABYTES(16));
    memset(main_arena.base, 0, main_arena.size);

    struct psx_state psx;
    psx_init(&psx, &main_arena, bios.memory);
    // TODO: reschedule gpu events when display settings are changed
    schedule_event(gpu_scanline_complete, &psx, 0, (s32)video_to_cpu_cycles(NTSC_VIDEO_CYCLES_PER_SCANLINE), EVENT_ID_GPU_SCANLINE_COMPLETE);
    //schedule_event(cpu, gpu_hblank_event, 0, cpu->gpu.horizontal_display_x2 - cpu->gpu.horizontal_display_x1, EVENT_ID_GPU_HBLANK);
    psx.gpu->renderer = renderer;

#if !SOFTWARE_RENDERING
    RECT rect;
    GetClientRect(window, &rect);
#endif

    audio_player *audio = audio_init();

    g_present_thread_handle = CreateEvent(NULL, TRUE, FALSE, NULL);
    struct win32_thread_params thread_params = {0};
    thread_params.renderer = renderer;
    HANDLE present_thread_handle = CreateThread(NULL, 0, ThreadProc, &thread_params, 0, NULL);

    u64 target_dt_us = (u64)((1 / 59.94f) * 1000000);

    u64 accum_dt_us = 0;

    HANDLE timer = CreateWaitableTimerExA(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer) {
        debug_log("Failed to create timer object!\n");
        return -1;
    }

    LARGE_INTEGER begin_counter, end_counter, frequency;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&begin_counter);
    //IAudioClient_Start(audio->client);
    while (g_running)
    {
        MSG msg = {0};
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
#if 0
        psx.pad->buttons.value = 0xffff;
        
        if (input.keystates['P'] && !stopkeypressed)
        {
            stopkeypressed = 1;
            write_wav_file(debug_sound_buffer, debug_sound_buffer_index * 2, "debugoutput0.wav");
            win32_play_sound(audio, (u8 *)debug_sound_buffer);
        }
#endif
#if 1
        emulate_from_audio(audio, &psx);
#else
        // get input
        #if 0
        dinput_get_data(input);
        cpu->pad.buttons = input->pad;
        #endif

        f32 cycles_to_run = 384.0f;
        // NOTE: leftover_cycles are extra cycles that the cpu ran ahead of the cycles to run
        f32 leftover_cycles = execute_instruction(cpu, cycles_to_run);

        f32 tick_count = cycles_to_run + leftover_cycles;

        cpu->spu.ticks += tick_count;
        if (cpu->spu.ticks > 768.0f) {
            spu_tick(&cpu->spu);
            cpu->spu.ticks -= 768.0f;
        }
        b8 vsync = 0;
        cpu->gpu.ticks += tick_count * (715909.0f / 451584.0f);
        if (gpu_run(&cpu->gpu)) {
            vsync = 1;
            cpu->i_stat |= INTERRUPT_VBLANK;
            SetEvent(g_present_thread_handle);
        }
#endif


        //play_sound_test(audio);

#if 0
        execute_instruction(cpu, 100);
        
        b8 vsync = 0;
        if (gpu_tick(&cpu->gpu, 300))
        {
            vsync = 1;
            cpu->i_stat |= INTERRUPT_VBLANK;
            SetEvent(event_handle);
        }
#endif  
        QueryPerformanceCounter(&end_counter);

        u64 ticks_elapsed = (end_counter.QuadPart - begin_counter.QuadPart);

        u64 elapsed_us = (ticks_elapsed * 1000000) / frequency.QuadPart;
#if 0
        accum_dt_us += elapsed_us;
        if (accum_dt_us >= 1000000)
        {
            accum_dt_us -= 1000000;
            //f32 dt_ms = 1000.0f / g_vblank_counter;
            char buffer[32];
            snprintf(buffer, 32, "Sunny | VPS: %d", g_vblank_counter);
            SetWindowText(window, buffer); 
            g_vblank_counter = 0;
        }
#endif
#if 0
        accum_dt_us += elapsed_us;

        if (vsync)
        {
            f32 dt_ms = 0.0f;
            if (accum_dt_us < target_dt_us)
            {
                //u64 target_ticks = end_counter.QuadPart + 
                LARGE_INTEGER due;
                LONGLONG target = (target_dt_us - accum_dt_us) * 10;
                due.QuadPart = -target;
                SetWaitableTimer(timer, &due, 0, NULL, NULL, 0);
                WaitForSingleObject(timer, INFINITE);

                QueryPerformanceCounter(&end_counter);

                dt_ms = (accum_dt_us / 1000.0f) + (((end_counter.QuadPart - begin_counter.QuadPart) * 1000000) / frequency.QuadPart) / 1000.0f;
            
            }
            else
            {
                dt_ms = accum_dt_us / 1000.0f;
            }
            accum_dt_us = 0;
            char buffer[32];
            snprintf(buffer, 32, "ms: %.2f", dt_ms);
            SetWindowText(window, buffer);
            //debug_log("%f\n", dt_ms);
        }
#endif
        begin_counter = end_counter;
    }
    //IAudioClient_Stop(audio->client);
    CloseHandle(timer);
#if SOFTWARE_RENDERING
    ReleaseDC(window, hdc);
#else
    
    renderer->shutdown(renderer);
    //FreeLibrary(rendererdll);
#endif

    free_arena(&main_arena);

    return 0;
}
