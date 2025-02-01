#ifndef PLATFORM_H
#define PLATFORM_H

// Platform detection
#if defined(_WIN32)
    #define SY_PLATFORM_WIN32
#else
    #error Unsupported platform
#endif

#include "common.h"

#if defined(SY_PLATFORM_WIN32)
    #include <windows.h>
    typedef HANDLE platform_file_handle;
    typedef HANDLE platform_event_handle;
    typedef CRITICAL_SECTION platform_mutex_handle;
    typedef HWND platform_window_handle;
    #define SY_EXPORT __declspec(dllexport)
    #define SY_IMPORT __declspec(dllimport)
#endif

#ifdef EXPORT_LIB
#define SY_API SY_EXPORT
#else
#define SY_API SY_IMPORT
#endif

typedef struct {
    platform_window_handle handle;
} platform_window;

typedef struct {
    platform_file_handle handle;
    b8 is_valid;
} platform_file;

SY_API b8 platform_open_file(const char *path, platform_file *out_file);
SY_API u32 platform_read_file(platform_file *file, u64 offset, void *dst_buffer, u32 bytes_to_read);
SY_API u64 platform_get_file_size(platform_file *file);
SY_API void platform_close_file(platform_file *file);

typedef struct {
    platform_mutex_handle handle;
} platform_mutex;

SY_API b8 platform_create_mutex(platform_mutex *out_mutex);
SY_API void platform_lock_mutex(platform_mutex *mutex);
SY_API void platform_unlock_mutex(platform_mutex *mutex);
SY_API void platform_destroy_mutex(platform_mutex *mutex);

typedef struct {
    platform_event_handle handle;
} platform_event;

SY_API b8 platform_create_event(platform_event *out_event, b8 signaled);
SY_API b8 platform_set_event(platform_event *event);
SY_API b8 platform_reset_event(platform_event *event);
SY_API b8 platform_wait_event(platform_event *event);
SY_API void platform_destroy_event(platform_event *event);

#define ZERO_STRUCT(str) memset(str, 0, sizeof(*(str)))

#ifdef SY_PLATFORM_IMPL

#if defined(SY_PLATFORM_WIN32)
b8 platform_create_mutex(platform_mutex *out_mutex)
{
    InitializeCriticalSection(&out_mutex->handle);
    return true;
}

void platform_lock_mutex(platform_mutex *mutex)
{
    EnterCriticalSection(&mutex->handle);
}

void platform_unlock_mutex(platform_mutex *mutex)
{
    LeaveCriticalSection(&mutex->handle);
}

void platform_destroy_mutex(platform_mutex *mutex)
{
    DeleteCriticalSection(&mutex->handle);
}

b8 platform_create_event(platform_event *out_event, b8 signaled)
{
    HANDLE event = CreateEventA(NULL, TRUE, signaled, NULL);
    if (event == NULL) {
        return false;
    }
    out_event->handle = event;
    return true;
}

b8 platform_set_event(platform_event *event)
{
    if (SetEvent(event->handle)) {
        return true;
    }
    return false;
}

b8 platform_reset_event(platform_event *event) 
{
    if (ResetEvent(event->handle)) {
        return true;
    }
    return false;
}

b8 platform_wait_event(platform_event *event)
{
    DWORD result = WaitForSingleObject(event->handle, INFINITE);
    if (result == WAIT_OBJECT_0) 
    {
        return true;
    }
    return false;
}

void platform_destroy_event(platform_event *event)
{
    CloseHandle(event->handle);
}

b8 platform_open_file(const char *path, platform_file *out_file)
{
    WCHAR wpath[MAX_PATH];

    int size = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (size > MAX_PATH) 
    {
        return false;
    }

    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 512);

    HANDLE file = CreateFileW(wpath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) 
    {
        return false;
    }

    out_file->handle = file;
    out_file->is_valid = true;
    return true;
}

u32 platform_read_file(platform_file *file, u64 offset, void *dst_buffer, u32 bytes_to_read)
{
    HANDLE hfile = file->handle;
    
    DWORD bytes_read;
    OVERLAPPED overlapped = {0};
    overlapped.Offset = (DWORD)offset;
    overlapped.OffsetHigh = (DWORD)(offset >> 32);

    if (ReadFile(hfile, dst_buffer, bytes_to_read, &bytes_read, &overlapped) == TRUE)
    {
        return bytes_read;
    }
    return 0;
}

u64 platform_get_file_size(platform_file *file)
{
    LARGE_INTEGER result;
    GetFileSizeEx(file->handle, &result);
    return result.QuadPart;
}

void platform_close_file(platform_file *file)
{
    if (file->is_valid)
    {
        CloseHandle(file->handle);
        ZERO_STRUCT(file);
    }
}
#endif

#endif /* SY_PLATFORM_IMPL */

#endif /* PLATFORM_H */