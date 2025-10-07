#include "platform/platform.h"

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

b8 platform_open_file(const char *path, u32 flags, platform_file *out_file)
{
    WCHAR wpath[MAX_PATH];

    int size = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (size > MAX_PATH) 
        return false;

    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 512);

    DWORD access_flags = 0;
    if (flags & FILE_OPEN_READ)
        access_flags |= GENERIC_READ;
    if (flags & FILE_OPEN_WRITE)
        access_flags |= GENERIC_WRITE;

    DWORD create_if_not_found = flags & FILE_OPEN_CREATE ? OPEN_ALWAYS : OPEN_EXISTING;

    HANDLE file = CreateFileW(wpath, access_flags, FILE_SHARE_READ, NULL, create_if_not_found, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) 
        return false;

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

    if (ReadFile(hfile, dst_buffer, bytes_to_read, &bytes_read, &overlapped))
    {
        return bytes_read;
    }
    return 0;
}

u32 platform_write_file(platform_file *file, u64 offset, void *src_buffer, u32 bytes_to_write)
{
    HANDLE hfile = file->handle;
    DWORD bytes_written;
    OVERLAPPED overlapped = {0};
    overlapped.Offset = (DWORD)offset;
    overlapped.OffsetHigh = (DWORD)(offset >> 32);

    if (WriteFile(hfile, src_buffer, bytes_to_write, &bytes_written, &overlapped))
    {
        return bytes_written;
    }
    return 0;
}

u64 platform_get_file_size(platform_file *file)
{
    LARGE_INTEGER result;
    GetFileSizeEx(file->handle, &result);
    return result.QuadPart;
}

void platform_set_file_size(platform_file *file, u64 size)
{
    LARGE_INTEGER dist;
    dist.QuadPart = size;
    SetFilePointerEx(file->handle, dist, NULL, FILE_BEGIN);
    SetEndOfFile(file->handle);
}

void platform_close_file(platform_file *file)
{
    if (file->is_valid)
    {
        CloseHandle(file->handle);
        ZERO_STRUCT(file);
    }
}

u64 platform_get_current_dir(char *buffer, u64 buffer_len)
{
    u64 len = buffer_len / sizeof(WCHAR);
    DWORD size = GetCurrentDirectoryW(len, (LPWSTR)buffer);
    if (size)
    {
        // TODO: temp
        u64 sz_bytes = size * sizeof(WCHAR);
        WCHAR *temp = malloc(sz_bytes);
        if (!temp)
            return 0;
        memcpy(temp, buffer, sz_bytes);
        int result = WideCharToMultiByte(CP_UTF8, 0, temp, size, buffer, buffer_len - 1, NULL, NULL);
        if (result)
            buffer[result] = '\0';
        free(temp);
        return result;
    }
    return 0;
}

b8 platform_create_dir(const char *dir_name)
{
    if (!CreateDirectoryA(dir_name, NULL))
    {
        DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS)
            return false;
    }
    return true;
}
