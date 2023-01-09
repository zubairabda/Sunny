#include <stdio.h>
#include <stdlib.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define SY_PLATFORM_WIN32
#include <immintrin.h>
#include "common.h"
#include "sy_math.h"
#include "allocator.h"
#include "filetypes.c"
#include "renderer/renderer.h"

static Renderer* g_renderer;

struct application_state
{
    HWND window;
    HDC hdc;
    BITMAPINFO bitmap_info;
};

struct application_state g_app;

#include "psx.h"

#include "debug.c"

#include "bus.c"
#include "cdrom.c"
#include "cpu.c"
#include "gpu.c"
#include "dma.c"
#include "timers.c"
#include "memory.c"

static volatile b32 running = SY_TRUE;

void* platform_alloc(size_t size)
{
    return VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

// stub functions for the sw rasterizer
static void renderer_handle_resize(u32 width, u32 height)
{
    
}

static void renderer_update_display(HDC context, int x, int y, int width, int height)
{
   
}

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    LRESULT result = 0;
    switch (msg)
    {
    case WM_ACTIVATE:
        /* code */
        break;
    case WM_CLOSE:
        running = SY_FALSE;
        break;
    case WM_SIZE:
    {
        u32 width = lParam & 0xffff;
        u32 height = (lParam >> 16) & 0xffff;
        if (g_renderer) // TODO: likely want to check if the renderer is hooked in some other way
        {
            g_renderer->handle_resize(width, height);
        }
        //renderer_handle_resize(width, height);
    }   break;
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
    case WM_KEYUP:
        //write_bmp(1024, 512, (u8*)g_debug.psx->gpu.vram, "VRAM.bmp");
        break;
    default:
        result = DefWindowProcA(hwnd, msg, wParam, lParam);
        break;
    }
    return result;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine, int nCmdShow)
{
    AllocConsole();
    AttachConsole(GetCurrentProcessId());
    FILE* fDummy;
    freopen_s(&fDummy, "CONIN$", "r", stdin);
    freopen_s(&fDummy, "CONOUT$", "w", stderr);
    freopen_s(&fDummy, "CONOUT$", "w", stdout);
    SetConsoleTitleA("Sunny Debugger");

    char* class_name = "SunnyWindowClass";
    WNDCLASSA wc = {0};
    //wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = class_name;
    RegisterClassA(&wc);

    HWND window = CreateWindowExA(0, class_name, "Sunny", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, hInstance, NULL);
    if (!window)
    {
        printf("Could not create the window handle!\n");
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
    HMODULE rendererdll = LoadLibrary("vulkan_renderer.dll");
    fp_load_renderer load_renderer = (fp_load_renderer)GetProcAddress(rendererdll, "load_renderer");
    if (!load_renderer)
    {
        MessageBoxA(window, "Please ensure the renderer dll file is present in the bin directory.\n", "Missing DLL", MB_ICONERROR | MB_OK);
        return -1;
    }

    g_renderer = load_renderer();
#endif

    struct FileInfo bios;
    char* bios_name = "SCPH1001.BIN";
    read_file(bios_name, &bios);

    char* exes[] = {""};

    struct FileInfo test;
    //read_file(exes[1], &test);
    g_debug.loaded_exe = test.memory;

    struct memory_arena main_arena = allocate_arena(megabytes(16));
    memset(main_arena.base, 0, main_arena.size);
    struct cpu_state* cpu = init_cpu(&main_arena, bios.memory);
    g_debug.psx = cpu;

    //const s32 target_miliseconds = (s32)((1.0f/59.94f) * 1000.0f);
#if !SOFTWARE_RENDERING
    RECT rect;
    GetClientRect(window, &rect);
    g_renderer->initialize(window, hInstance, rect.right, rect.bottom);
#endif
    u64 accumulated_ticks = 0;
    LARGE_INTEGER begin_counter, end_counter, frequency;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&begin_counter);
    
    while (running)
    {
        MSG msg = {0};
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        execute_instruction(cpu, 100);
        accumulated_ticks += 100;
        QueryPerformanceCounter(&end_counter);
#if 1
        u64 ticks_elapsed = end_counter.QuadPart - begin_counter.QuadPart;

        //s32 sleepms = (s32)(target_miliseconds - (((ticks_elapsed)*1000) / (f32)frequency.QuadPart));

        if (accumulated_ticks > CPU_CLOCK / 100) // temp
        {
            Sleep(5);
            accumulated_ticks = 0;
        }
#endif
        begin_counter = end_counter;
    }
#if SOFTWARE_RENDERING
    ReleaseDC(window, hdc);
#else
    g_renderer->shutdown();
    FreeLibrary(rendererdll);
#endif
    
    free_arena(&main_arena);

    return 0;
}