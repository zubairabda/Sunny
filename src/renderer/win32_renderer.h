#include "renderer.h"

typedef renderer_interface *(*fp_load_renderer_win32)(HWND window, HINSTANCE hinstance);

static inline renderer_interface *win32_load_renderer_from_dll(HWND window, HINSTANCE hinstance, char *path)
{
    HMODULE renderer_dll = LoadLibrary(path);
    fp_load_renderer_win32 load_renderer_proc = (fp_load_renderer_win32)GetProcAddress(renderer_dll, "win32_load_renderer");
    if (!load_renderer_proc)
    {
        MessageBoxA(window, "Please ensure the renderer dll file is present in the bin directory.\n", "Missing DLL", MB_ICONERROR | MB_OK);
        return NULL;
    }
    renderer_interface *result = load_renderer_proc(window, hinstance);
    return result;
}

static inline void win32_unload_renderer(renderer_interface *renderer)
{

}
