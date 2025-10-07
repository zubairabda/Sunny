#ifndef PLATFORM_H
#define PLATFORM_H

// platform detection
#if defined(_WIN32)
    #define SY_PLATFORM_WIN32
#else
    #error Unsupported platform
#endif

#include "common.h"
#include <stdlib.h>

#if defined(SY_PLATFORM_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
    typedef HANDLE platform_file_handle;
    typedef HANDLE platform_event_handle;
    typedef CRITICAL_SECTION platform_mutex_handle;
    typedef HWND platform_window_handle;
    #define OS_PATH_MAX MAX_PATH
#else
    #if defined(PATH_MAX)
        #define OS_PATH_MAX PATH_MAX
    #else
        #define OS_PATH_MAX 1024
    #endif
#endif

typedef struct {
    platform_window_handle handle;
} platform_window;

typedef struct {
    platform_file_handle handle;
    b8 is_valid;
} platform_file;

enum
{
    FILE_OPEN_READ = 0x1,
    FILE_OPEN_WRITE = 0x2,
    FILE_OPEN_CREATE = 0x4
};

b8 platform_open_file(const char *path, u32 flags, platform_file *out_file);
u32 platform_read_file(platform_file *file, u64 offset, void *dst_buffer, u32 bytes_to_read);
u32 platform_write_file(platform_file *file, u64 offset, void *src_buffer, u32 bytes_to_write);
u64 platform_get_file_size(platform_file *file);
void platform_set_file_size(platform_file *file, u64 size);
void platform_close_file(platform_file *file);

u64 platform_get_current_dir(char *buffer, u64 buffer_len);
b8 platform_create_dir(const char *dirname);
u32 platform_get_file_base_name(const char *path, const char **start);

typedef struct {
    platform_mutex_handle handle;
} platform_mutex;

b8 platform_create_mutex(platform_mutex *out_mutex);
void platform_lock_mutex(platform_mutex *mutex);
void platform_unlock_mutex(platform_mutex *mutex);
void platform_destroy_mutex(platform_mutex *mutex);

typedef struct {
    platform_event_handle handle;
} platform_event;

b8 platform_create_event(platform_event *out_event, b8 signaled);
b8 platform_set_event(platform_event *event);
b8 platform_reset_event(platform_event *event);
b8 platform_wait_event(platform_event *event);
void platform_destroy_event(platform_event *event);

#define ZERO_STRUCT(str) memset(str, 0, sizeof(*(str)))

inline b8 platform_is_path_sep(char c)
{
#ifdef SY_PLATFORM_WIN32
    return (c == '\\' || c == '/');
#else
    return (c == '/');
#endif
}

#endif /* PLATFORM_H */
