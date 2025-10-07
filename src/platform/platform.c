#include "platform/platform.h"
#include "allocator.h"

u32 platform_get_file_base_name(const char *path, const char **start)
{
    size_t len = strlen(path);
    if (len > (OS_PATH_MAX - 1))
        return 0;
    s32 end = (s32)len;
    s32 pos = end;
    while (pos--)
    {
        char c = path[pos];
        if (platform_is_path_sep(c))
            break;
    }
    // last char cannot be a path separator
    s32 sublen = end - pos - 1;
    if (sublen)
    {
        // if there is a file extension, subtract it from the length
        s32 ext = string_last_index_of_char(path, '.');
        if (ext > pos)
            sublen -= (end - ext);

        *start = &path[pos + 1];
        return sublen;
    }
    return 0;
}
