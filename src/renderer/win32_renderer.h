#include "renderer.h"

typedef renderer_context *(*fp_load_renderer_win32)(HWND window, HINSTANCE hinstance);

static inline renderer_context *win32_load_renderer_from_dll(HWND window, HINSTANCE hinstance, char *path)
{
    HMODULE renderer_dll = LoadLibrary(path);
    if (!renderer_dll)
    {
        MessageBoxA(window, "Please ensure the renderer dll file is present in the bin directory.\n", "Missing DLL", MB_ICONERROR | MB_OK);
        return NULL;
    }

    fp_load_renderer_win32 load_renderer_proc = (fp_load_renderer_win32)GetProcAddress(renderer_dll, "win32_load_renderer");
    if (!load_renderer_proc) 
    {
        MessageBoxA(window, "The presented DLL is incompatible or corrupted\n", "DLL Error", MB_ICONERROR | MB_OK);
        return NULL;
    }

    return load_renderer_proc(window, hinstance);;
}
