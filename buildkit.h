#ifndef BUILDKIT_H
#define BUILDKIT_H

#if defined(_WIN32)
    #define HOST_WINDOWS
#elif defined(__linux__)
    #define HOST_LINUX
#elif defined(__FreeBSD__)
    #define HOST_FREEBSD
#elif defined(__APPLE__)
    #define HOST_MACOS
#else
    #define HOST_OTHER
#endif

#ifdef HOST_WINDOWS

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#define OS_PATH_MAX MAX_PATH

#else
    #include <dirent.h>
    #include <fcntl.h>
    #include <unistd.h>
    #include <sys/wait.h>
    #include <sys/stat.h>
    #include <errno.h>
#ifdef PATH_MAX
    #define OS_PATH_MAX PATH_MAX
#else
    #define OS_PATH_MAX 1024
#endif

#endif /* HOST_WINDOWS */

#define BK_COUNT(arr) (sizeof(arr) / sizeof(arr[0]))

#define BK_ZERO(s) memset(&(s), 0, sizeof(s))

#define BK_ASSERT(expr) do { if (!(expr)) { *(volatile int *)0 = 0; } } while (0)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define BK_COMMAND_LENGTH 0x400

#define BK_ALLOC(size) malloc(size)
#define BK_CALLOC(count, size) calloc(count, size);
#define BK_REALLOC(p, size) realloc(p, size)
#define BK_FREE(p) free(p)

typedef struct OSFile OSFile;
struct OSFile
{
    uintptr_t handle;
};

typedef uint64_t TimeStamp;

typedef struct DirIterator DirIterator;

typedef struct DirEntry DirEntry;
struct DirEntry
{
    const char *name;
    int is_dir;
};

typedef struct String String;
struct String
{
    char *data;
    size_t length;
};

typedef struct Slice Slice;
struct Slice
{
    uint32_t pos;
    uint32_t length;
};

typedef struct StringArray StringArray;
struct StringArray
{
    String *v;
    size_t front;
    size_t back;
    size_t size;
    size_t count;
};

typedef struct StringNode StringNode;
struct StringNode
{
    StringNode *next;
    String str;
};

typedef struct StringList StringList;
struct StringList
{
    StringNode *head;
    StringNode *tail;
    size_t total_length;
    size_t count;
};

typedef enum
{
    OS_WINDOWS,
    OS_LINUX,
    OS_FREEBSD,
    OS_MACOS,
    OS_OTHER
} HostOSType;

typedef enum
{
    DEPS_KIND_NONE,
    DEPS_KIND_FORCE,
    DEPS_KIND_SCAN,
    DEPS_KIND_MAKE,
    DEPS_KIND_MSVC
} DependencyKind;

typedef struct BuildRule BuildRule;
struct BuildRule
{
    const char *command;
    const char *output_ext;
};

static inline BuildRule build_rule(const char *command, const char *output_ext)
{
    BuildRule result;
    result.command = command;
    result.output_ext = output_ext;
    return result;
}

typedef struct BuildOptions BuildOptions;
struct BuildOptions
{
    BuildRule cc_rule;
    BuildRule output_rule;
    StringArray *include_paths;
    DependencyKind dependency_kind;
    const char *output_dir;
    int generate_compile_commands;
};

/* returns the host platform that buildkit is running on */
HostOSType get_host_os(void);

/* returns the index of the last path separator character within a string */
int path_last_sep(const char *path, int len);

/* returns the first index of the character 'c' in the string 'str' or -1 if the search fails */
int strfirst(const char *str, char c);

/* returns the last index of the character 'c' in the string 'str' or -1 if the search fails */
int strlast(const char *str, char c);

/* returns the number of occurences of 'old_str' that were replaced by 'new_str' */
int strreplace(char *buffer, size_t buffer_size, const char *old_str, const char *new_str);

/* adds the specified file(s) to the string list, supports globbing */
int add_files(StringArray *list, const char *pattern);

/* builds a target given a collection of source files */
int build_target(const char *name, StringArray *sources, BuildOptions *opt);

/* begins a directory iterator */
DirIterator *dir_iter_begin(const char *dir);

/* retrieves the next directory entry */
DirEntry *dir_iter_next(DirIterator *iter);

/* ends a directory iterator */
void dir_iter_end(DirIterator *iter);

/* allocates an indexable array of null-terminated strings allocated from a pool with size 'pool_size' */
StringArray *string_array_alloc(size_t pool_size);

/* frees the string_list stored in address 'arr' */
void string_array_free(StringArray *arr);

/* appends a null-terminated string to the list */
int string_array_push(StringArray *arr, const char *string);

/* appends a string with a given length to the list */
int string_array_push_len(StringArray *arr, const char *string, size_t length);

/* resets a string list to 0 length */
void string_array_reset(StringArray *arr);

/* run a command on the host, using 'file_out' as output and 'file_in' as input */
int os_run_cmd(char *command, OSFile *file_out, OSFile *file_in);

/* create a 'pipe' that can be read and written to */
int os_create_pipe(OSFile *out_read, OSFile *out_write);

enum
{
    OS_FILE_ACCESS_READ = 0x1,
    OS_FILE_ACCESS_WRITE = 0x2,
    OS_FILE_SHARE_READ = 0x4,
    OS_FILE_SHARE_WRITE = 0x8,
    OS_FILE_TRUNCATE = 0x10,
    OS_FILE_CREATE_IF_NOT_EXIST = 0x20
};

int os_open_file(const char *path, OSFile *out_file, int flags);

void os_close_file(OSFile file);

int os_read_file(OSFile file, size_t offset, size_t size, void *buffer);

size_t os_get_file_size(OSFile file);

static inline int is_whitespace(char c)
{
    return (c == ' ' || (c >= '\t' && c <= '\r'));
}

static inline int is_eol(char c) 
{
    return (c == '\r' || c == '\n');
}

static inline int is_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '$';
}

static inline int is_alpha(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static inline int is_number(char c)
{
    return (c >= '0' && c <= '9');
}

static inline int is_alphanumeric(char c) 
{
    return is_alpha(c) || is_number(c);
}

#endif /* BUILDKIT_H */

#ifdef BUILDKIT_IMPLEMENTATION

#define MAX_PATH_SEGMENTS 16

HostOSType get_host_os(void)
{
#if defined(HOST_WINDOWS)
    return OS_WINDOWS;
#elif defined(HOST_LINUX)
    return OS_LINUX;
#elif defined(HOST_FREEBSD)
    return OS_FREEBSD;
#elif defined(HOST_MACOS)
    return OS_MACOS;
#else
    return OS_OTHER;
#endif
}

static inline size_t greater_size(size_t a, size_t b)
{
    return a > b ? a : b;
}

static inline int greater_int(int a, int b)
{
    return a > b ? a : b;
}

static inline int lesser_int(int a, int b)
{
    return a < b ? a : b;
}

enum
{
    ARENA_FLAG_NO_CHAIN = 0x1
};

typedef struct Arena Arena;
struct Arena
{
    Arena *current;
    Arena *prev;
    size_t pos; /* global position */
    size_t at; /* local position */
    size_t size;
    int flags;
    int reserved;
};

#define ARENA_CHUNK_SIZE 0x10000
#define ARENA_HEADER_SIZE 64

Arena *arena_alloc(size_t size)
{
    size_t allocsz = ARENA_HEADER_SIZE + size;
    Arena *arena = (Arena *)BK_ALLOC(allocsz);
    if (!arena)
        return NULL;
    memset(arena, 0, allocsz);
    arena->at = ARENA_HEADER_SIZE;
    arena->pos = 0;
    arena->size = allocsz;
    arena->current = arena;
    return arena;
}

void arena_free(Arena *arena)
{
    Arena *current = arena->current;
    while (current)
    {
        Arena *prev = current->prev;
        BK_FREE(current);
        current = prev;
    }
}

void *arena_push(Arena *arena, size_t size)
{
    Arena *current = arena->current;
    if (current->at + size > current->size)
    {
        if (arena->flags & ARENA_FLAG_NO_CHAIN)
            return NULL;
        
        size_t allocsz = greater_size(ARENA_CHUNK_SIZE, size * 2);
        Arena *next = arena_alloc(allocsz);
        if (!next)
            return NULL;
        next->pos = current->pos + current->size;
        next->prev = current;
        arena->current = next;
        current = next;
    }

    void *result = (uint8_t *)current + current->at;
    current->at += size;
    return result;
}

#define arena_push_array(arena, type, count) (type *)arena_push(arena, (count) * sizeof(type))

size_t arena_pos(Arena *arena)
{
    Arena *current = arena->current;
    size_t result = current->pos + current->at;
    return result;
}

void arena_pop_to(Arena *arena, size_t pos)
{
    Arena *current = arena->current;
    /* prevent freeing first chunk */
    if (pos < ARENA_HEADER_SIZE)
        pos = ARENA_HEADER_SIZE;
    while (current->pos >= pos)
    {
        Arena *prev = current->prev;
        BK_FREE(current);
        current = prev;
    }
    size_t pop = pos - current->pos;
    current->at = pop;
}

void arena_pop(Arena *arena, size_t size)
{
    size_t pos = arena_pos(arena);
    if (size <= pos)
    {
        pos -= size;
    }
    arena_pop_to(arena, pos);
}

void arena_clear(Arena *arena)
{
    arena_pop_to(arena, 0);
}

String string_from_cstr(const char *cstr)
{
    String result;
    result.data = (char *)cstr;
    result.length = strlen(cstr);
    return result;
}

int strings_are_equal(String a, String b)
{
    if (a.length != b.length)
        return 0;
    for (size_t i = 0; i < a.length; ++i)
    {
        if (a.data[i] != b.data[i])
            return 0;
    }
    return 1;
}

struct DirIterator
{
#ifdef HOST_WINDOWS
    HANDLE handle;
    WIN32_FIND_DATAW data;
    int first_found;
    char conversion_buffer[OS_PATH_MAX * 3]; /* conversion to utf8 */
#else
    DIR *handle;
    struct dirent *entry;
#endif
    DirEntry current;
};

DirIterator *dir_iter_begin(const char *dir)
{
#ifdef HOST_WINDOWS
    WIN32_FIND_DATAW data;

    WCHAR path[OS_PATH_MAX];
    path[0] = L'\0';

    size_t dir_len = strlen(dir);

    int wlen = MultiByteToWideChar(CP_UTF8, 0, dir, dir_len, path, OS_PATH_MAX);
#if 0
    char last = starting_dir[dir_len - 1];
    // strip trailing slashes
    if (last == '/' || last == '\\') {
        --dir_len;
    }
#endif
    if (wlen > (MAX_PATH - 3))
        return NULL;

    //snprintf(path, MAX_PATH, "%s\\*", dir);
    path[wlen++] = L'\\';
    path[wlen++] = L'*';
    path[wlen] = L'\0';

    HANDLE find = FindFirstFileW(path, &data);
    if (find == INVALID_HANDLE_VALUE)
        return NULL;

    DirIterator *iter = (DirIterator *)BK_ALLOC(sizeof(DirIterator));
    memset(iter, 0, sizeof(DirIterator));

    iter->data = data;
    iter->handle = find;
    WideCharToMultiByte(CP_UTF8, 0, data.cFileName, -1, iter->conversion_buffer, sizeof(iter->conversion_buffer), NULL, NULL);
    iter->current.name = iter->conversion_buffer;
    iter->current.is_dir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

    return iter;
#else
    DIR *handle = opendir(dir);
    if (!handle)
        return NULL;
    DirIterator *iter = (DirIterator *)BK_ALLOC(sizeof(DirIterator));
    memset(iter, 0, sizeof(DirIterator));
    iter->handle = handle;
    return iter;
#endif
}

DirEntry *dir_iter_next(DirIterator *iter)
{
#ifdef HOST_WINDOWS
    if (!iter->first_found)
    {
        iter->first_found = 1;
        return &iter->current;
    }
    
    if (FindNextFileW(iter->handle, &iter->data))
    {
        WideCharToMultiByte(CP_UTF8, 0, iter->data.cFileName, -1, iter->conversion_buffer, sizeof(iter->conversion_buffer), NULL, NULL);
        //iter->current.name = iter->data.cFileName;
        iter->current.is_dir = (iter->data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        return &iter->current;
    }

    return NULL;
#else
    struct dirent *entry = readdir(iter->handle);
    if (entry)
    {
        iter->current.name = entry->d_name;
        struct stat st;
        stat(entry->d_name, &st);
        iter->current.is_dir = (st.st_mode & S_IFDIR) != 0;
        return &iter->current;
    }
    return NULL;
#endif
}

void dir_iter_end(DirIterator *iter)
{
#ifdef HOST_WINDOWS
    FindClose(iter->handle);
    BK_FREE(iter);
#else
    closedir(iter->handle);
    BK_FREE(iter);
#endif
}

typedef struct OSFileStat OSFileStat;
struct OSFileStat
{
    TimeStamp last_write_time;
    TimeStamp last_access_time;
    uint64_t file_size;
};

int os_file_stat(const char *path, OSFileStat *out_stat)
{
#ifdef HOST_WINDOWS
    WIN32_FILE_ATTRIBUTE_DATA data;

    WCHAR wide_path[OS_PATH_MAX];
    int required_length = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (required_length > OS_PATH_MAX)
        return 0;
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wide_path, required_length);

    if (GetFileAttributesExW(wide_path, GetFileExInfoStandard, &data))
    {
        LARGE_INTEGER fsize;
        fsize.LowPart = data.nFileSizeLow;
        fsize.HighPart = data.nFileSizeHigh;

        out_stat->file_size = fsize.QuadPart;

        ULARGE_INTEGER access_time;
        access_time.LowPart = data.ftLastAccessTime.dwLowDateTime;
        access_time.HighPart = data.ftLastAccessTime.dwHighDateTime;

        ULARGE_INTEGER write_time;
        write_time.LowPart = data.ftLastWriteTime.dwLowDateTime;
        write_time.HighPart = data.ftLastWriteTime.dwHighDateTime;

        out_stat->last_access_time = access_time.QuadPart;
        out_stat->last_write_time = write_time.QuadPart;

        return 1;
    }
    return 0;
#else
    struct stat st;
    if (!stat(path, &st))
    {
        out_stat->file_size = st.st_size;
        out_stat->last_write_time = st.st_mtime;
        out_stat->last_access_time = st.st_atime;
        return 1;
    }
    return 0;
#endif
}

int os_open_file(const char *path, OSFile *out_file, int flags)
{
#ifdef HOST_WINDOWS
    DWORD dwDesiredAccess = 0;
    if (flags & OS_FILE_ACCESS_READ)
        dwDesiredAccess |= GENERIC_READ;
    if (flags & OS_FILE_ACCESS_WRITE)
        dwDesiredAccess |= GENERIC_WRITE; 

    DWORD dwShareMode = 0;
    if (flags & OS_FILE_SHARE_READ)
        dwShareMode |= FILE_SHARE_READ;
    if (flags & OS_FILE_SHARE_WRITE)
        dwShareMode |= (FILE_SHARE_WRITE | FILE_SHARE_DELETE);

    DWORD dwCreationDisposition = OPEN_EXISTING;
    if (flags & OS_FILE_CREATE_IF_NOT_EXIST)
    {
        if (flags & OS_FILE_TRUNCATE)
            dwCreationDisposition = CREATE_ALWAYS;
        else
            dwCreationDisposition = OPEN_ALWAYS;
    }
    else if (flags & OS_FILE_TRUNCATE)
        dwCreationDisposition = TRUNCATE_EXISTING;

    WCHAR wide_path[OS_PATH_MAX];
    int required_length = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (required_length > OS_PATH_MAX)
        return 0;
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wide_path, required_length);

    HANDLE hfile = CreateFileW(wide_path, dwDesiredAccess, dwShareMode, NULL, dwCreationDisposition, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hfile == INVALID_HANDLE_VALUE)
        return 0;
    out_file->handle = (uintptr_t)hfile;
    return 1;
#else
    int opt = 0;
    if (flags & OS_FILE_ACCESS_READ && flags & OS_FILE_ACCESS_WRITE)
        opt |= O_RDWR;
    else if (flags & OS_FILE_ACCESS_READ)
        opt |= O_RDONLY;
    else if (flags & OS_FILE_ACCESS_WRITE)
        opt |= O_WRONLY;

    if (flags & OS_FILE_CREATE_IF_NOT_EXIST)
        opt |= O_CREAT;

    if (flags & OS_FILE_TRUNCATE)
        opt |= O_TRUNC;

    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    int fd = open(path, opt, mode);
    if (fd < 0)
        return 0;
    out_file->handle = (uintptr_t)fd;
    return 1;
#endif
}

void os_close_file(OSFile file)
{
#ifdef HOST_WINDOWS
    CloseHandle((HANDLE)file.handle);
#else
    close((int)file.handle);
#endif
}

size_t os_get_file_size(OSFile file)
{
#ifdef HOST_WINDOWS
    LARGE_INTEGER li;
    GetFileSizeEx((HANDLE)file.handle, &li);
    return li.QuadPart;
#else
    struct stat st;
    if (!fstat((int)file.handle, &st))
        return st.st_size;
    return 0;
#endif
}

int os_write_file(OSFile file, void *buffer, size_t size)
{
#ifdef HOST_WINDOWS
    HANDLE hfile = (HANDLE)file.handle;
    DWORD bytes_written = 0;
    return WriteFile(hfile, buffer, size, &bytes_written, NULL);
#else
    return write((int)file.handle, buffer, size);
#endif
}

TimeStamp os_get_file_write_time(OSFile file)
{
#ifdef HOST_WINDOWS
    FILETIME write_time;
    GetFileTime((HANDLE)file.handle, NULL, NULL, &write_time);
    ULARGE_INTEGER li;
    li.LowPart = write_time.dwLowDateTime;
    li.HighPart = write_time.dwHighDateTime;
    TimeStamp result = li.QuadPart;
    return result;
#else
    struct stat st;
    if (!fstat((int)file.handle, &st))
        return st.st_mtime;
    return 0;
#endif
}

int os_create_directory(const char *dir)
{
#ifdef HOST_WINDOWS
    WCHAR wide_path[OS_PATH_MAX];
    int required_length = MultiByteToWideChar(CP_UTF8, 0, dir, -1, NULL, 0);
    if (required_length > OS_PATH_MAX)
        return -1;
    MultiByteToWideChar(CP_UTF8, 0, dir, -1, wide_path, required_length);

    if (!CreateDirectoryW(wide_path, NULL))
    {
        DWORD err = GetLastError();
        if (err == ERROR_ALREADY_EXISTS)
            return 0;
        else
            return -1;
    }
    return 1;
#else
    mode_t mode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
    if (!mkdir(dir, mode))
        return 1;
    else if (errno == EEXIST)
        return 0;
    else
        return -1;
#endif
}

int os_get_current_directory(char *buffer, size_t len)
{
#ifdef HOST_WINDOWS
    WCHAR widecwd[OS_PATH_MAX];
    DWORD size = GetCurrentDirectoryW(OS_PATH_MAX, widecwd);
    if (size > OS_PATH_MAX)
        return 0;
    int length = WideCharToMultiByte(CP_UTF8, 0, widecwd, size + 1, NULL, 0, NULL, NULL);
    if (length + 1 > len)
        return 0;
    WideCharToMultiByte(CP_UTF8, 0, widecwd, size + 1, buffer, length + 1, NULL, NULL);
    strreplace(buffer, len, "\\", "/");
    return 1;
#else
    char *result = getcwd(buffer, len);
    return (result != NULL);
#endif
}

size_t os_normalize_path(Arena *arena, const char *path, int path_len, char *buffer, size_t size)
{
#ifdef HOST_WINDOWS
    if (path_len < 0)
        path_len = (int)strlen(path);
    size_t min_size = path_len * 2 + 1;
    size_t pos = arena_pos(arena);
    WCHAR *widepath = (WCHAR *)arena_push(arena, min_size);
    WCHAR *fullpath = (WCHAR *)arena_push(arena, OS_PATH_MAX * sizeof(WCHAR));
    MultiByteToWideChar(CP_UTF8, 0, path, path_len, widepath, min_size / sizeof(WCHAR));
    DWORD pathsize = GetFullPathNameW(widepath, OS_PATH_MAX, fullpath, NULL);
    if (pathsize >= OS_PATH_MAX)
        goto exit_free;
    size_t output_size = pathsize * 3 + 1;
    if (size < output_size)
        goto exit_free;
    WideCharToMultiByte(CP_UTF8, 0, fullpath, pathsize, buffer, size, NULL, NULL);
    arena_pop_to(arena, pos);
    return pathsize;
exit_free:
    arena_pop_to(arena, pos);
    return 0;
#else
    #warning unimplemented call os_normalize_path
    return 0;
#endif
}

int os_path_exists(const char *path)
{
#ifdef HOST_WINDOWS
    WCHAR wide_path[OS_PATH_MAX];
    int required_length = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (required_length > OS_PATH_MAX)
        return -1;
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wide_path, required_length);
    DWORD attr = GetFileAttributesW(wide_path);
    return (attr != INVALID_FILE_ATTRIBUTES);
#else
    struct stat st;
    if (!stat(path, &st))
        return 1;
    return 0;
#endif
}

int os_get_thread_count(void)
{
#ifdef HOST_WINDOWS
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return sysinfo.dwNumberOfProcessors;
#else
    return sysconf(_SC_NPROCESSORS_ONLN);
#endif
}

static inline int is_path_absolute(const char *path, int len)
{
#ifdef HOST_WINDOWS
    if (len && path[0] == '/' || path[0] == '\\')
        return 1;

    else if (len >= 3 && is_alpha(path[0]) && path[1] == ':')
    {
        if (path[2] == '/' || path[2] == '\\')
            return 1;
    }
    return 0;
#else
    return (len && path[0] == '/');
#endif
}

static inline int is_path_separator(char c)
{
#ifdef HOST_WINDOWS
    return (c == '\\' || c == '/');
#else
    return (c == '/');
#endif
}

static inline const char *get_path_separator(void)
{
#ifdef HOST_WINDOWS
    return "\\";
#else
    return "/";
#endif
}

static inline int is_valid_file_char(char c)
{
    return (c >= ' ' && c <= '~') && !(is_path_separator(c) || c == '"' || c == '*' || c == ':' || c == '<' || c == '>' || c == '?' || c == '|');
}

#define MAX_PATH_TOKENS 32

typedef enum
{
    TOKEN_LITERAL,
    TOKEN_WILDCARD
} PathTokenType;

enum
{
    SEGMENT_WILDCARD = 0x1,
    SEGMENT_DIRECTORY = 0x2
};

typedef struct RangeInt RangeInt;
struct RangeInt
{
    int start;
    int end;
};

typedef struct PathToken PathToken;
struct PathToken
{
    PathTokenType type;
    int reserved;
    Slice str;
};

typedef struct PathSegment PathSegment;
struct PathSegment
{
    int token_count;
    int flags;
};

static inline RangeInt irange(int start, int length)
{
    RangeInt result;
    result.start = start;
    result.end = start + length;
    return result;
}

void string_array_reset(StringArray *arr)
{
    arr->back = arr->size;
    arr->front = sizeof(StringArray);
    arr->count = 0;
}

StringArray *string_array_alloc(size_t pool_size)
{
    if (pool_size < sizeof(StringArray))
        return NULL;
    char *buffer = (char *)BK_ALLOC(pool_size);
    if (buffer)
    {
        StringArray *result = (StringArray *)buffer;
        result->size = pool_size;
        string_array_reset(result);
        result->v = (String *)((char *)result + result->front);
        return result;
    }
    return NULL;
}

int string_array_push_len(StringArray *arr, const char *string, size_t length)
{
    size_t front = arr->front + sizeof(String);
    size_t back = arr->back - length - 1;
    if (front > back)
        return 0;
    String *str = &arr->v[arr->count++];
    str->data = (char *)arr + back;
    str->length = length;

    memcpy(str->data, string, length);
    str->data[length] = '\0';
    arr->back = back;
    arr->front = front;

    return 1;
}

int string_array_push(StringArray *arr, const char *string)
{
    return string_array_push_len(arr, string, strlen(string));
}

void string_array_free(StringArray *arr)
{
    BK_FREE(arr);
}

StringNode *string_push_node(Arena *arena, String str)
{
    StringNode *node = (StringNode *)arena_push(arena, sizeof(StringNode));
    node->next = NULL;
    node->str = str;
    return node;
}

StringNode *string_list_push(Arena *arena, StringList *list, String str)
{
    StringNode *node = string_push_node(arena, str);
    list->total_length += str.length;
    ++list->count;
    if (list->tail)
    {
        list->tail->next = node;
        list->tail = node;
    }
    else
    {
        list->head = list->tail = node;
    }
    return node;
}

StringNode *string_list_pop(StringList *list)
{
    StringNode *result = list->head;
    if (result)
    {
        list->total_length -= result->str.length;
        --list->count;
        list->head = list->head->next;
        if (list->head == NULL)
            list->tail = NULL;
    }
    return result;
}

String string_list_join(Arena *arena, StringList *list, char sep)
{
    size_t length = list->total_length + list->count - 1;
    String result;
    result.length = length;
    result.data = (char *)arena_push(arena, length);
    StringNode *node = list->head;
    size_t index = 0;
    for (int i = 0; i < list->count; ++i)
    {
        memcpy(result.data + index, node->str.data, node->str.length);
        index += node->str.length;
        if (i < list->count - 1)
            result.data[index++] = sep;
        node = node->next;
    }
    return result;
}

enum
{
    STRING_BUILDER_NO_REALLOC = 0x1
};

typedef struct StringBuilder StringBuilder;
struct StringBuilder
{
    size_t capacity;
    size_t len;
    char *data;
    int flags;
    int reserved;
};

String string_from_builder(StringBuilder *sb)
{
    String result;
    result.data = sb->data;
    result.length = sb->len;
    return result;
}

String string_copy_builder(Arena *arena, StringBuilder *sb)
{
    String result;
    result.data = (char *)arena_push(arena, sb->len + 1);
    result.length = sb->len;
    memcpy(result.data, sb->data, sb->len);
    result.data[sb->len] = '\0';
    return result;
}

int string_alloc(StringBuilder *sb, size_t capacity, char *optional_buffer)
{
    char *buf = NULL;
    int flags = 0;
    size_t allocsz;
    if (optional_buffer)
    {
        /* if a buffer is provided, capacity represents the length of the buffer, in bytes */
        if (!capacity)
            return 0;
        allocsz = capacity;
        buf = optional_buffer;
        flags |= STRING_BUILDER_NO_REALLOC;
    }
    else
    {
        /* if a buffer is not provided, the capacity represents the initial maximum character count, not including the null terminator */
        allocsz = capacity ? (capacity + 1) : 1024;
        buf = (char *)BK_ALLOC(allocsz);
        if (!buf)
            return 0;
    }

    sb->capacity = allocsz - 1;
    sb->data = buf;
    sb->data[0] = '\0';
    sb->len = 0;
    sb->flags = flags;
    return 1;
}

void string_free(StringBuilder *sb)
{
    BK_FREE(sb->data);
    sb->data = NULL;
    sb->capacity = 0;
    sb->len = 0;
}

static inline int string_grow(StringBuilder *sb, size_t amount)
{
    size_t cap = sb->len + amount;
    if (cap > sb->capacity)
    {
        if (sb->flags & STRING_BUILDER_NO_REALLOC)
            return 0;
        
        size_t new_size = cap * 2;
        char *data = (char *)BK_REALLOC(sb->data, new_size + 1);
        if (!data)
            return 0;
        sb->data = data;
        sb->capacity = new_size;
    }
    return 1;
}

int string_append_len(StringBuilder *sb, const char *str, size_t len)
{
    BK_ASSERT(str);
    if (!string_grow(sb, len))
        return 0;
    memcpy(sb->data + sb->len, str, len);
    sb->len += len;
    sb->data[sb->len] = '\0';
    return 1;
}

int string_append(StringBuilder *sb, const char *str)
{
    if (str)
    {
        size_t len = strlen(str);
        return string_append_len(sb, str, len);
    }
    return 0;
}

int string_append_char(StringBuilder *sb, char c)
{
    if (!string_grow(sb, 1))
        return 0;
    size_t at = sb->len;
    sb->data[sb->len++] = c;
    sb->data[sb->len] = '\0';
    return 1;
}

static inline int string_concat(StringBuilder *sb, String str)
{
    return string_append_len(sb, str.data, str.length);
}

void string_trunc(StringBuilder *sb, size_t length)
{
    if (sb->len > length)
    {
        sb->len = length;
        sb->data[sb->len] = '\0';
    }
}

char *arena_push_cstr(Arena *arena, String str)
{
    char *result = (char *)arena_push(arena, str.length + 1);
    memcpy(result, str.data, str.length);
    result[str.length] = '\0';
    return result;
}

String string_copy_zero(Arena *arena, String str)
{
    char *data = arena_push_cstr(arena, str);
    String result;
    result.data = data;
    result.length = str.length;
    return result;
}

String string_concat_cstr(Arena *arena, String str, const char *cstr)
{
    size_t len = strlen(cstr);
    size_t total = str.length + len;
    char *data = (char *)arena_push(arena, total + 1);
    memcpy(data, str.data, str.length);
    memcpy(data + str.length, cstr, len);
    data[total] = '\0';
    String result;
    result.data = data;
    result.length = total;
    return result;
}

String concat_cstr(Arena *arena, const char *a, const char *b)
{
    size_t len_a = strlen(a);
    size_t len_b = strlen(b);
    size_t size = len_a + len_b;
    char *data = (char *)arena_push(arena, size + 1);
    memcpy(data, a, len_a);
    memcpy(data + len_a, b, len_b);
    data[size] = '\0';
    String result;
    result.data = data;
    result.length = size;
    return result;
}

static inline void glob_push_segment(StringBuilder *current, const char *dir, size_t dir_len)
{
    size_t len = current->len;
    if (!len)
    {
        string_append_len(current, dir, dir_len);
    }
    else
    {
        //char *end = &current[len - 1];
        //if (is_path_separator(*end))
        string_append_char(current, '/');
        string_append_len(current, dir, dir_len);
    }
}

static inline void glob_pop_segment(StringBuilder *current)
{
    size_t len = current->len;
    while (len--)
    {
        char c = current->data[len];
        if (is_path_separator(c))
        {
            string_trunc(current, len);
            //current[len] = '\0';
            break;
        }
    }
}

typedef struct GlobData GlobData;
struct GlobData
{
    StringArray *list;
    PathToken *tokens;
    const char *pattern;
    PathSegment *segments;
    int segment_count;
};

static int glob_match_pattern(GlobData *data, RangeInt tokens, const char *input)
{
    const char *pattern = data->pattern;
    size_t len = strlen(input);
    int index = 0;
    PathTokenType prev_token_type = TOKEN_LITERAL;
    while (tokens.start < tokens.end)
    {
        PathToken *token = &data->tokens[tokens.start++];
        switch (token->type)
        {
        case TOKEN_LITERAL:
        {
            const char *at = &pattern[token->str.pos];
            const char *src = &input[index];
            if (prev_token_type == TOKEN_WILDCARD)
            {
                int match = 0;
                size_t t = 0;
                while (index < len)
                {
                    char a = input[index++];
                    if (a == '\0')
                        break;
                    char b = pattern[token->str.pos + t];

                    if (a != b)
                    {
                        t = 0;
                        continue;
                    }

                    ++t;

                    if (t == token->str.length)
                    {
                        match = 1;
                        break;
                    }
                }

                if (!match)
                    return 0;
            }
            else
            {
                if (strncmp(&input[index], at, token->str.length) != 0)
                {
                    return 0;
                }
                index += token->str.length;
            }
            break;
        }
        case TOKEN_WILDCARD:
        {

            break;
        }
        }

        //last_token_type = token->type;
        prev_token_type = token->type;
        //++token_index;
    }
    
    // ensure suffixes are matched correctly
    if ((prev_token_type == TOKEN_LITERAL) && (index < len))
        return 0;

    return 1;
}

static void glob_recurse(GlobData *data, StringBuilder *dir, int segment_index, int token_index)
{
    int directory_count = 0; /* number of literal path segments traversed, so that they can be popped in order */
    while (segment_index < data->segment_count)
    {
        PathSegment *segment = &data->segments[segment_index];
        int is_directory = segment_index < (data->segment_count - 1);
        if (segment->flags & SEGMENT_WILDCARD)
        {
            int dir_filter = is_directory || segment->flags & SEGMENT_DIRECTORY;
            DirEntry *entry;
            DirIterator *iter = dir_iter_begin(dir->data);
            if (!iter)
                break;

            while ((entry = dir_iter_next(iter)) != NULL)
            {
                if (dir_filter && !entry->is_dir)
                    continue;

                const char *name = entry->name;
                if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
                    continue;
                
                if (glob_match_pattern(data, irange(token_index, segment->token_count), name))
                {
                    if (is_directory)
                    {
                        if (entry->is_dir)
                        {
                            glob_push_segment(dir, name, strlen(name));
                            glob_recurse(data, dir, segment_index + 1, token_index + segment->token_count);
                            glob_pop_segment(dir);
                        }
                    }
                    else
                    {
                        // match?
                        //printf("%s\n", name);
                        glob_push_segment(dir, name, strlen(name));
                        string_array_push_len(data->list, dir->data, dir->len);
                        glob_pop_segment(dir);
                    }
                }
            }
            dir_iter_end(iter);
            break;
        }
        else
        {
            PathToken *token = &data->tokens[token_index];
            if (is_directory)
            {
                glob_push_segment(dir, &data->pattern[token->str.pos], token->str.length);
                ++directory_count;
            }
            else
            {
                // match?
                //printf("%.*s\n", (int)token->str.length, &data->pattern[token->str.pos]);
                glob_push_segment(dir, &data->pattern[token->str.pos], token->str.length);
                string_array_push_len(data->list, dir->data, dir->len);
                glob_pop_segment(dir);
            }
        }

        //glob_pop_directory(dir);
        token_index += segment->token_count;
        ++segment_index;
    }

    for (int i = 0; i < directory_count; ++i)
        glob_pop_segment(dir);
}

static int glob_sources(GlobData *data)
{
    char current_dir[OS_PATH_MAX];
    StringBuilder sb;
    string_alloc(&sb, OS_PATH_MAX, current_dir);

    if (!is_path_absolute(data->pattern, strlen(data->pattern)))
    {
        if (data->segments[0].flags & SEGMENT_WILDCARD)
            string_append_char(&sb, '.');
    }

    glob_recurse(data, &sb, 0, 0);

    return 0;
}

static inline void set_path_token(PathTokenType type, size_t start, size_t length, PathToken *out_token)
{
    out_token->type = type;
    out_token->str.pos = start;
    out_token->str.length = length;
}

int add_files(StringArray *list, const char *pattern)
{
    size_t len = strlen(pattern);
    if (!len)
        return 0;

    PathSegment segments[MAX_PATH_SEGMENTS];
    memset(segments, 0, sizeof(segments));
    PathToken *token_buffer = (PathToken *)BK_ALLOC(sizeof(PathToken) * MAX_PATH_TOKENS);
    int token_count = 0;

    //int starting_index = is_absolute_path(pattern, len);
    int result = 0;
    int segment_count = 1;
    size_t at = 0;
    int segment_index = 0;
    while (pattern[at] != '\0')
    {
        char c = pattern[at];
        if (is_path_separator(c))
        {
            /* NOTE: this is here for now to account for POSIX path names that start with a '/' separator */
            /* since we ignore the first '/' */
            if (segments[segment_index].token_count > 0)
            {
                ++segment_count;
                ++segment_index;
                BK_ASSERT(segment_count < MAX_PATH_SEGMENTS);
            }

            ++at;
            //size_t start = at++;
            while (is_path_separator(pattern[at]))
                ++at;
            //add_token(TOKEN_PATH_SEPARATOR, start, at - start, ++segment);
        }
        else if (c == '*')
        {
            segments[segment_index].flags |= SEGMENT_WILDCARD;
            set_path_token(TOKEN_WILDCARD, at++, 1, &token_buffer[token_count++]);
            ++segments[segment_index].token_count;
        }
        else if (is_valid_file_char(c))
        {
            size_t start = at;
            while (is_valid_file_char(pattern[at]))
                ++at;
            set_path_token(TOKEN_LITERAL, start, at - start, &token_buffer[token_count++]);
            ++segments[segment_index].token_count;
        }
        else
        {
            goto exit_free;
        }
    }

    if (!segments[segment_index].token_count)
    {
        segments[segment_index].flags |= SEGMENT_DIRECTORY;
    }

    BK_ASSERT(token_count <= MAX_PATH_TOKENS);

    GlobData data;
    data.list = list;
    data.tokens = token_buffer;
    data.pattern = pattern;
    data.segments = segments;
    data.segment_count = segment_count;
    result = glob_sources(&data);
exit_free:
    BK_FREE(token_buffer);

    return result;
}

#define vec_define(T) \
typedef struct T##Vec T##Vec; \
struct T##Vec \
{ \
    T *data; \
    int length; \
    int capacity; \
}; \
\
int T##_vec_alloc(T##Vec *out_vec, int initial_size) \
{ \
    T *data = (T *)BK_ALLOC(sizeof(T) * initial_size); \
    if (!data) \
        return 0; \
    out_vec->data = data; \
    out_vec->length = 0; \
    out_vec->capacity = initial_size; \
    return 1; \
} \
\
void T##_vec_free(T##Vec *vec) \
{ \
    BK_FREE(vec->data); \
    vec->data = NULL; \
    vec->length = 0; \
    vec->capacity = 0; \
} \
\
static inline int T##_vec_maybe_grow(T##Vec *vec) \
{ \
    if (vec->length == vec->capacity) \
    { \
        T *data = (T *)BK_REALLOC(vec->data, sizeof(T) * (vec->capacity * 2)); \
        if (!data) \
            return 0; \
        vec->capacity *= 2; \
    } \
    return 1; \
} \
\
static inline int T##_vec_push(T##Vec *vec, T value) \
{ \
    if (!T##_vec_maybe_grow(vec)) \
        return 0; \
    vec->data[vec->length++] = value; \
    return 1; \
} \
\
static inline int T##_vec_emplace(T##Vec *vec) \
{ \
    if (!T##_vec_maybe_grow(vec)) \
        return -1; \
    return vec->length++; \
}

#define BK_FILE_COUNT 512

enum
{
    FILE_ENTRY_SCANNED = 0x1
};

typedef struct FileEntry FileEntry;
struct FileEntry
{
    uint32_t path_hash;
    int id;
};

typedef struct FileNode FileNode;
struct FileNode
{
    String path;
    int flags;
    int visit_counter;
    int adjacency_offset;
    int adjacency_count;
    TimeStamp timestamp;
};

vec_define(int)
vec_define(FileNode)

typedef struct FileGraph FileGraph;
struct FileGraph
{
    FileEntry *file_table;
    FileNodeVec *files;
    intVec *adjacencies;
    intVec *queue;
    int table_capacity;
    int table_size;
    int visit_index;
    String file_contents;
    StringList *dependencies;
};

typedef struct PP_Parser PP_Parser;
struct PP_Parser
{
    char *at;
    int error;
};

static inline uint32_t fnv1a(char *data, size_t length)
{
    uint32_t hash = 0x811c9dc5;
    while (length--)
    {
        unsigned char c = data[length];
        hash ^= c;
        hash *= 0x01000193;
    }
    return hash;
}

static size_t pp_parse_until(PP_Parser *parser, char c)
{
    size_t length = 0;
    while (parser->at[0])
    {
        if (parser->at[0] == c)
            return length;
        else if (is_eol(c))
            break;

        ++parser->at;
        ++length;
    }
    return 0;
}

static inline char *parse_line_comment(char *at)
{
    ++at;
    while (*at && !is_eol(*at)) {
        ++at;
    }
    return at;
}

static inline char *parse_block_comment(char *at) 
{
    ++at;
    while (*at)
    {
        if (*at == '*')
        {
            ++at;
            if (*at == '/')
            {
                ++at;
                break;
            }
        }
        else
        {
            ++at;
        }
    }
    return at;
}

void pp_clear_whitespace(PP_Parser *parser)
{
    for (;;)
    {
        if (parser->at[0] == ' ' || parser->at[0] == '\t')
        {
            ++parser->at;
        }
        else if (parser->at[0] == '/')
        {
            if (parser->at[1] == '/')
            {
                parser->at += 2;
                parser->at = parse_line_comment(parser->at);
            }
            else if (parser->at[1] == '*')
            {
                parser->at += 2;
                parser->at = parse_block_comment(parser->at);
            }
            else
            {
                break;
            }
        }
        else
        {
            break;
        }
    }
}

int pp_next_directive(PP_Parser *parser)
{
    while (parser->at[0])
    {
        if (is_whitespace(parser->at[0]))
        {
#if 0
            if (*parser->at == '\n')
            {
                ++parser->line;
            }
#endif
            ++parser->at;
        }
        else if (parser->at[0] == '#')
        {
            ++parser->at;
            return 1;
        }
        else 
        {
            while (!is_eol(*parser->at++) && parser->at[0]);
        }
    }
    return 0;
}

void pp_splice_lines(char *text)
{
    char *at = text;
    char *marker = at;

    while (*at)
    {
        if (*at == '\\') {
            marker = at;
            //int diff = at - marker;
            ++at;
            /* 
            if 0 below is to allow for the case where some compilers allow whitespace
            between the backslash and EOL sequence
            */
#if 0
            while (is_whitespace(*at) && !is_eol(*at))
            {
                ++at;
            }
#endif
            if (*at == '\r')
            {
                ++at;
                if (*at == '\n')
                {
                    ++at;
                }
            }
            else if (*at == '\n')
            {
                ++at;
            }
            else
            {
                continue;
            }

            memmove(marker, at, strlen(at) + 1);
        }
        else {
            ++at;
        }
    }
}

typedef struct IncludeSearchArgs IncludeSearchArgs;
struct IncludeSearchArgs
{
    StringArray *include_paths;
    StringBuilder *current_dir;
    int local_search;
};

static int table_put(FileEntry *table, uint32_t hash, int capacity, int id)
{
    int mask = capacity - 1;
    int index = hash & mask;
    for (;;)
    {
        FileEntry *entry = table + index;
        if (!entry->path_hash)
        {
            entry->path_hash = hash;
            entry->id = id;
            return index;
        }

        index = (index + 1) & mask;
    }
}

static int table_resize(FileGraph *graph)
{
    int cap = graph->table_capacity;
    FileEntry *table = (FileEntry *)BK_CALLOC(cap * 2, sizeof(FileEntry));
    if (!table)
        return 0;
    graph->table_capacity *= 2;
    for (int i = 0; i < cap; ++i)
    {
        FileEntry entry = graph->file_table[i];
        if (entry.path_hash != 0)
        {
            table_put(table, entry.path_hash, graph->table_capacity, entry.id);
        }
    }
    BK_FREE(graph->file_table);
    graph->file_table = table;
    return 1;
}

static int table_find(FileGraph *graph, String key, FileNode **out_file)
{
    uint32_t hash = fnv1a(key.data, key.length);
    if (!hash)
        hash = 0x80000000;
    int mask = graph->table_capacity - 1;
    int index = hash & mask;
    int trials = graph->table_capacity;
    while (trials--)
    {
        FileEntry *entry = graph->file_table + index;
        if (!entry->path_hash)
        {
            entry->path_hash = hash;
            entry->id = FileNode_vec_emplace(graph->files);
            memset(graph->files->data + entry->id, 0, sizeof(FileNode));
            *out_file = graph->files->data + entry->id;
            ++graph->table_size;
            if (graph->table_size == graph->table_capacity)
                table_resize(graph);
            return entry->id;
        }
        
        if (hash == entry->path_hash)
        {
            FileNode *file = graph->files->data + entry->id;
            if (strings_are_equal(key, file->path))
            {
                *out_file = file;
                return entry->id;
            }
        }
        index = (index + 1) & mask;
    }
    return -1;
}

static FileNode *table_get_entry(FileGraph *graph, String key)
{
    FileNode *result = NULL;
    if (table_find(graph, key, &result) < 0)
    {
        return NULL;
    }
    return result;
}

static void resolve_include_path(StringBuilder *current_dir, String path, StringBuilder *out_path)
{
    string_append_len(out_path, current_dir->data, current_dir->len);

    const char *sep = get_path_separator();
    
    for (size_t i = 0; i < path.length; ++i)
    {
        if (path.data[i] == '.')
        {
            if ((i + 1) > path.length)
                break;
            char next = path.data[i + 1];
            if (is_path_separator(next))
            {
                ++i;
                continue;
            }
            else if (next == '.')
            {
                if ((i + 2) > path.length || is_path_separator(path.data[i + 2]))
                {
                    i += 2;
                    // pop directory
                    int index = path_last_sep(out_path->data, out_path->len);
                    if (index > 0)
                    {
                        string_trunc(out_path, index);
                    }
                    else
                    {
                        string_append(out_path, sep);
                        string_append(out_path, "..");
                    }
                    continue;
                }
            }
        }
        // parse until next sep
        size_t at = i;
        while (!is_path_separator(path.data[at]) && at < path.length)
            ++at;
        size_t length = at - i;
        if (length)
        {
            string_append(out_path, sep);
            string_append_len(out_path, path.data + i, length);
        }
        i += length;
    }
}

static int search_include_file(IncludeSearchArgs *args, String name, StringBuilder *out_path)
{
    /* append current dir if path is not absolute */
    if (args->local_search)
    {
        if (is_path_absolute(name.data, name.length))
        {
            string_concat(out_path, name);
        }
        else
        {
            resolve_include_path(args->current_dir, name, out_path);
        }

        if (os_path_exists(out_path->data))
        {
            return 1;
        }
    }

    if (!args->include_paths)
    {
        return 0;
    }

    for (int i = 0; i < args->include_paths->count; ++i)
    {
        String include_path = args->include_paths->v[i];
        const char *end = include_path.data + include_path.length;
        size_t real_length;
        /* strip trailing slashes */
        for (real_length = include_path.length; real_length >= 0; --real_length)
        {
            // TODO: verify
            char c = include_path.data[real_length - 1];
            if (!is_path_separator(c))
            {
                break;
            }
        }
        //snprintf(path, MAX_PATH, "%.*s/%.*s", end)
        string_trunc(out_path, 0);
        string_append_len(out_path, include_path.data, real_length);
        string_append_len(out_path, "/", 1); // TODO: platform separator?
        string_concat(out_path, name);
        
        if (os_path_exists(out_path->data))
        {
            return 1;
        }
    }

    return 0;
}

static int get_include_file_path(Arena *arena, IncludeSearchArgs *args, String name, String *out_path)
{
    size_t pos = arena_pos(arena);
    char *data = (char *)arena_alloc(greater_size(OS_PATH_MAX, name.length + args->current_dir->len + 1));

    StringBuilder file_path;
    string_alloc(&file_path, OS_PATH_MAX, data);

    if (search_include_file(args, name, &file_path))
    {
        out_path->data = file_path.data;
        out_path->length = file_path.len;
        arena_pop_to(arena, pos + out_path->length + 1); // shrink to fit resulting string
        return 1;
    }
    arena_pop_to(arena, pos);
    return 0;
}

int scan_file(Arena *arena, FileNode *node, FileGraph *graph, StringArray *includes)
{
    char buf[OS_PATH_MAX];
    StringBuilder current_path;
    string_alloc(&current_path, OS_PATH_MAX, buf);
    //os_normalize_path(path, paths->at, left);

    IncludeSearchArgs args = {0};
    args.include_paths = includes;
    args.current_dir = &current_path;

    node->adjacency_offset = graph->adjacencies->length;
    
    String path = node->path;
    OSFile file;
    if (!os_open_file(path.data, &file, OS_FILE_ACCESS_READ | OS_FILE_SHARE_READ))
        return 1;

    node->timestamp = os_get_file_write_time(file);
    
    size_t file_size = os_get_file_size(file);
    if (file_size + 1 > graph->file_contents.length)
    {
        char *buf = graph->file_contents.data;
        size_t buffer_size = file_size + 1;
        if (buf != NULL)
            BK_FREE(buf);
        buf = (char *)BK_ALLOC(buffer_size);
        if (!buf)
        {
            printf("Failed to allocate memory for file contents\n");
            os_close_file(file);
            return 0;
        }
        graph->file_contents.data = buf;
        graph->file_contents.length = buffer_size;
    }

    if (!os_read_file(file, 0, file_size, graph->file_contents.data))
    {
        printf("Error reading file %s\n", path.data);
        return 0;
    }

    os_close_file(file);

    graph->file_contents.data[file_size] = '\0';

    string_trunc(&current_path, 0);
    int dir = path_last_sep(path.data, path.length);
    if (dir > 0)
        string_append_len(&current_path, path.data, dir);

    pp_splice_lines(graph->file_contents.data);

    PP_Parser parser = {0};

    char include[] = "include";
    size_t count = sizeof(include) - 1;
    
    parser.at = graph->file_contents.data;
    parser.error = 0;

    while (pp_next_directive(&parser))
    {
        pp_clear_whitespace(&parser);
        if (strncmp(include, parser.at, count) == 0)
        {
            parser.at += count;
            pp_clear_whitespace(&parser);
            String filename;
            filename.data = parser.at + 1;

            int local_search = 0;
            if (parser.at[0] == '"')
            {
                ++parser.at;
                local_search = 1;
                filename.length = pp_parse_until(&parser, '"');
            }
            else if (parser.at[0] == '<')
            {
                ++parser.at;
                filename.length = pp_parse_until(&parser, '>');
            }
            
            if (!filename.length)
            {
                // TODO: handling this case? Maybe warn?
                continue;
            }
            
            args.local_search = local_search;

            String filepath;
            size_t pos = arena_pos(arena);
            if (get_include_file_path(arena, &args, filename, &filepath))
            {
                FileNode *next;
                int next_id = table_find(graph, filepath, &next);

                ++node->adjacency_count;
                int_vec_push(graph->adjacencies, next_id);

                if (next->path.data)
                {
                    arena_pop_to(arena, pos); // File has existing entry in the table -> free string
                }
                else
                {
                    next->path = filepath;
                }
                int_vec_push(graph->queue, next_id);
            }
        }
    }
    return 1;
}

int walk_source_file(Arena *arena, String source, FileGraph *graph, StringArray *include_paths)
{
    FileNode *src = NULL;
    int src_id = table_find(graph, source, &src);
    if (!src->path.data)
        src->path = source;

    graph->queue->length = 0;
    int head = 0;
    int_vec_push(graph->queue, src_id);

    StringList *deps = &graph->dependencies[graph->visit_index++];

    while (head < graph->queue->length)
    {
        int id = graph->queue->data[head++];

        FileNode *node = &graph->files->data[id];
        if (node->visit_counter == graph->visit_index)
            continue;

        node->visit_counter = graph->visit_index;

        string_list_push(arena, deps, node->path);

        if (node->flags & FILE_ENTRY_SCANNED)
        {
            int *adj = graph->adjacencies->data + node->adjacency_offset;
            for (int i = 0; i < node->adjacency_count; ++i)
            {
                int_vec_push(graph->queue, adj[i]);
            }
        }
        else
        {
            if (!scan_file(arena, node, graph, include_paths))
                return 0;
            node->flags |= FILE_ENTRY_SCANNED;
        }
    }

    return 1;
}

typedef enum
{
    COMMAND_TOKEN_STRING,
    COMMAND_TOKEN_INPUT,
    COMMAND_TOKEN_OUTPUT
} CommandTokenType;

typedef struct CommandToken CommandToken;
struct CommandToken
{
    CommandTokenType type;
    String string;
    CommandToken *next;
};

typedef struct CommandTokenList CommandTokenList;
struct CommandTokenList
{
    CommandToken *head;
    CommandToken *tail;
    size_t count;
};

static inline void add_cmd_token(Arena *arena, CommandTokenList *list, CommandTokenType type, int length, const char *data)
{
    CommandToken *token = (CommandToken *)arena_push(arena, sizeof(CommandToken));
    token->type = type;
    token->string.length = length;
    token->string.data = (char *)data;
    token->next = NULL;

    if (list->head != NULL)
    {
        list->tail->next = token;
        list->tail = token;
        ++list->count;
    }
    else
    {
        list->head = token;
        list->tail = token;
        list->count = 1;
    }
}

CommandTokenList tokenize_build_command(Arena *arena, const char *command)
{
    CommandTokenList result = {0};

    int last_index = 0;
    int i;
    for (i = 0; command[i] != '\0'; ++i)
    {
        char c = command[i];
        if (c == '{')
        {
            if (command[i + 1] == '{')
            {
                ++i;
                int range = i - last_index;
                add_cmd_token(arena, &result, COMMAND_TOKEN_STRING, range, &command[last_index]);
                last_index = i + 1;
            }
            else
            {
                if (last_index < i)
                {
                    // add leading string
                    int range = i - last_index;
                    add_cmd_token(arena, &result, COMMAND_TOKEN_STRING, range, &command[last_index]);
                    last_index = i + 1;
                }

                int start = i + 1;
                const char *substring = command + start;
                int length = strfirst(substring, '}');
                
                if (length >= 0)
                {
                    ++i;
                    if (strncmp(substring, "in", length) == 0)
                    {
                        add_cmd_token(arena, &result, COMMAND_TOKEN_INPUT, 0, NULL);
                    }
                    else if (strncmp(substring, "out", length) == 0)
                    {
                        add_cmd_token(arena, &result, COMMAND_TOKEN_OUTPUT, 0, NULL);
                    }
                    else
                    {
                        printf("Warning: unknown identifier: %*.s\n", length, substring);
                        continue;
                    }
                    i += length;
                    last_index = i + 1;
                }
                else
                {
                    add_cmd_token(arena, &result, COMMAND_TOKEN_STRING, (int)strlen(substring), substring);
                    break;
                }
            }
        }
        else if (c == '}')
        {
            if (command[i + 1] == '}')
            {
                ++i;
                int range = i - last_index;
                add_cmd_token(arena, &result, COMMAND_TOKEN_STRING, range, &command[last_index]);
                last_index = i + 1;
            }
        }
    }

    // add any trailing string as a last token
    if (last_index < i)
    {
        int range = i - last_index;
        add_cmd_token(arena, &result, COMMAND_TOKEN_STRING, range, &command[last_index]);
    }

    return result;
}

static inline void push_command_string(StringBuilder *sb, CommandTokenList *tokens, String input, String output)
{
    CommandToken *current = tokens->head;
    for (size_t i = 0; i < tokens->count; ++i)
    {
        switch (current->type)
        {
        case COMMAND_TOKEN_STRING:
            string_concat(sb, current->string);
            break;
        case COMMAND_TOKEN_INPUT:
            string_concat(sb, input);
            break;
        case COMMAND_TOKEN_OUTPUT:
            string_concat(sb, output);
            break;
        default:
            break;
        }
        current = current->next;
    }
}

static int run_build_rule(char *command_line, String input, String output, CommandTokenList *cc_list)
{
    StringBuilder command;
    string_alloc(&command, BK_COMMAND_LENGTH, command_line);
    
    string_trunc(&command, 0);
    push_command_string(&command, cc_list, input, output);
    puts(command.data);

    int err = os_run_cmd(command.data, NULL, NULL);
    if (err != 0)
    {
        printf("Error running build command: %s\n", command.data);
        return 0;
    }
    return 1;
}

typedef struct BufferReader BufferReader;
struct BufferReader
{
    uint64_t pos;
    uint64_t size;
    uint8_t *data;
};

BufferReader buffer_reader_init(uint8_t *buf, uint64_t size)
{
    BufferReader result;
    result.pos = 0;
    result.size = size;
    result.data = buf;
    return result;
}

BufferReader buffer_reader_from_arena(Arena *arena, uint64_t size)
{
    uint8_t *buf = (uint8_t *)arena_push(arena, size);
    return buffer_reader_init(buf, size);
}

static inline int buffer_reader_has(BufferReader *buffer, uint64_t amount)
{
    return buffer->pos + amount <= buffer->size;
}

void *buffer_consume(BufferReader *buffer, uint64_t size)
{
    void *result = buffer->data + buffer->pos;
    uint64_t left = buffer->size - buffer->pos;
    if (size > left)
    {
        size = left;
    }
    buffer->pos += size;
    return result;
}

uint32_t read_u32le(BufferReader *buffer)
{
    uint32_t result = 0;
    if (buffer_reader_has(buffer, sizeof(uint32_t)))
    {
        uint8_t *data = (buffer->data + buffer->pos);
        result = (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
        buffer->pos += 4;
    }
    return result;
}

String read_string(BufferReader *buffer, size_t length)
{
    String result = {0};
    if (buffer_reader_has(buffer, length))
    {
        result.data = (char *)(buffer->data + buffer->pos);
        result.length = length;
        buffer->pos += length;
    }
    return result;
}

// TODO: might remove this and just have a single BufferStream struct..
typedef struct BufferWriter BufferWriter;
struct BufferWriter
{
    uint64_t pos;
    uint64_t size;
    uint8_t *data;
};

BufferWriter buffer_writer_init(uint8_t *buf, uint64_t size)
{
    BufferWriter result;
    result.pos = 0;
    result.size = size;
    result.data = buf;
    return result;
}

BufferWriter buffer_writer_from_arena(Arena *arena, uint64_t size)
{
    uint8_t *buf = (uint8_t *)arena_push(arena, size);
    return buffer_writer_init(buf, size);
}

static inline int buffer_writer_has(BufferWriter *buffer, uint64_t amount)
{
    return buffer->pos + amount <= buffer->size;
}

void write_u32le(BufferWriter *buffer, uint32_t value)
{
    if (buffer_writer_has(buffer, sizeof(uint32_t)))
    {
        uint8_t *data = (buffer->data + buffer->pos);
        data[0] = (uint8_t)value;
        data[1] = (uint8_t)(value >> 8);
        data[2] = (uint8_t)(value >> 16);
        data[3] = (uint8_t)(value >> 24);
        buffer->pos += 4;
    }
}

void write_string(BufferWriter *buffer, String string)
{
    if (buffer_writer_has(buffer, string.length))
    {
        char *data = (char *)(buffer->data + buffer->pos);
        memcpy(data, string.data, string.length);
        buffer->pos += string.length;
    }
}

#define MAKE_VERSION(major, minor) ((minor) | ((major) << 16))
#define CACHE_FILE_VERSION MAKE_VERSION(0, 1)
#define CACHE_HEADER_SIZE 16

static uint32_t get_cache_file(Arena *arena, const char *path, BufferReader *out_reader)
{
    uint32_t result = 0;
    OSFile cache_file;
    if (os_open_file(path, &cache_file, OS_FILE_ACCESS_READ | OS_FILE_SHARE_READ))
    {
        size_t pos = arena_pos(arena);

        BufferReader header = buffer_reader_from_arena(arena, CACHE_HEADER_SIZE);
        os_read_file(cache_file, 0, CACHE_HEADER_SIZE, header.data);
        uint8_t *magic = (uint8_t *)buffer_consume(&header, 4);
        uint32_t ver = read_u32le(&header);
        
        if (memcmp("BDK$", magic, 4) != 0 || ver != CACHE_FILE_VERSION)
            goto fail;

        uint32_t target_count = read_u32le(&header);
        uint32_t total_file_size = read_u32le(&header); // TODO: validation

        arena_pop_to(arena, pos);
        BufferReader contents = buffer_reader_from_arena(arena, total_file_size);
        os_read_file(cache_file, 0, total_file_size, contents.data);
        *out_reader = contents;

        result = target_count;
        goto done;
fail:
        arena_pop_to(arena, pos);
done:
        os_close_file(cache_file);
    }
    return result;
}

static uint32_t cache_lookup(BufferReader *cache, uint32_t target_count, String target)
{
    cache->pos = CACHE_HEADER_SIZE;
    for (size_t i = 0; i < target_count; ++i)
    {
        uint32_t offset = read_u32le(cache);
        uint32_t name_length = read_u32le(cache);
        String name = read_string(cache, name_length);
        if (strings_are_equal(name, target))
        {
            cache->pos = offset;
            return offset;
        }
    }
    return 0;
}

static int check_cache_dependencies(Arena *arena, FileGraph *graph, BufferReader *cache, TimeStamp target_time)
{
    uint32_t item_count = read_u32le(cache);
    for (uint32_t i = 0; i < item_count; ++i)
    {
        uint32_t dep_name_length = read_u32le(cache);
        String dependency = read_string(cache, dep_name_length);

        FileNode *file = table_get_entry(graph, dependency);
        if (!file->path.data)
        {
            size_t pos = arena_pos(arena);
            String path = string_copy_zero(arena, dependency);
            OSFileStat stat;
            if (os_file_stat(path.data, &stat))
            {
                file->timestamp = stat.last_write_time;
                file->path = path;
            }
            else
            {
                arena_pop_to(arena, pos);
                return 1; // if we can't access the file, rescan/rebuild
            }
        }

        if (file->timestamp > target_time)
            return 1;
    }

    return 0;
}

typedef struct CacheEntry CacheEntry;
struct CacheEntry
{
    String target;
    uint32_t offset; // offset == 0 means it was scanned and has a dependency chain
    uint32_t size;
};

typedef struct CacheInfo CacheInfo;
struct CacheInfo
{
    CacheEntry *entries;
    BufferReader *contents;
    size_t total_entry_sizes;
    StringList *targets;
};

int serialize_cache_file(Arena *arena, FileGraph *graph, const char *path, CacheInfo *info)
{
    // NOTE: this can be counted in the prior loop
    size_t cache_dep_length = 0;
    size_t cache_item_count = 0;
    for (int i = 0; i < graph->visit_index; ++i)
    {
        cache_dep_length += graph->dependencies[i].total_length;
        cache_item_count += graph->dependencies[i].count;
    }

    size_t cache_toc_size = (8 * info->targets->count) + info->targets->total_length;
    size_t cache_body_size = (4 * graph->visit_index) + (4 * cache_item_count) + cache_dep_length + info->total_entry_sizes;
    size_t total_cache_size = CACHE_HEADER_SIZE + cache_toc_size + cache_body_size;

    BufferWriter cache_writer = buffer_writer_from_arena(arena, total_cache_size);
    write_string(&cache_writer, string_from_cstr("BDK$"));
    write_u32le(&cache_writer, CACHE_FILE_VERSION);
    write_u32le(&cache_writer, info->targets->count);
    write_u32le(&cache_writer, total_cache_size);
    uint64_t body_offset = CACHE_HEADER_SIZE + cache_toc_size;
    uint64_t toc_offset = CACHE_HEADER_SIZE;
    int scanned_index = 0;
    for (int i = 0; i < info->targets->count; ++i)
    {
        CacheEntry entry = info->entries[i];
        uint32_t current_offset = body_offset;
        if (entry.offset)
        {
            // copy directly from existing cache
            uint8_t *dst = cache_writer.data + body_offset;
            uint8_t *src = info->contents->data + entry.offset;
            memcpy(dst, src, entry.size);
            body_offset += entry.size;
        }
        else
        {
            // iterate dependencies
            StringList *deps = &graph->dependencies[scanned_index++];
            BK_ASSERT(deps->count);
            cache_writer.pos = body_offset;
            write_u32le(&cache_writer, deps->count);
            StringNode *current = deps->head;
            for (int j = 0; j < deps->count; ++j)
            {
                write_u32le(&cache_writer, current->str.length);
                write_string(&cache_writer, current->str);
                current = current->next;
            }
            body_offset = cache_writer.pos;
        }
        cache_writer.pos = toc_offset;
        write_u32le(&cache_writer, current_offset);
        write_u32le(&cache_writer, entry.target.length);
        write_string(&cache_writer, entry.target);
        toc_offset = cache_writer.pos;
    }

    OSFile cache_file;
    os_open_file(path, &cache_file, OS_FILE_ACCESS_WRITE | OS_FILE_CREATE_IF_NOT_EXIST | OS_FILE_TRUNCATE);
    os_write_file(cache_file, cache_writer.data, cache_writer.size);
    os_close_file(cache_file);
    return 1;
}

int build_target(const char *name, StringArray *sources, BuildOptions *opt)
{
    int result = 0;
    char path_buffer[OS_PATH_MAX];
    StringBuilder buf;
    string_alloc(&buf, OS_PATH_MAX, path_buffer);

    int thread_count = 1;

    Arena *arena = arena_alloc(0x100000);
    if (!arena)
        return 0;

    TimeStamp *target_filetimes = arena_push_array(arena, TimeStamp, sources->count);
    char *commands = (char *)arena_push(arena, BK_COMMAND_LENGTH * thread_count);
    FileGraph *graph = (FileGraph *)arena_push(arena, sizeof(FileGraph));
    graph->dependencies = arena_push_array(arena, StringList, sources->count);

    FileNodeVec files = {0};
    intVec adjacencies = {0};
    intVec queue = {0};

    FileEntry *file_table = (FileEntry *)BK_CALLOC(BK_FILE_COUNT, sizeof(FileEntry));
    if (!file_table)
        goto exit_free;

    if (!FileNode_vec_alloc(&files, BK_FILE_COUNT))
        goto exit_free;
    if (!int_vec_alloc(&adjacencies, BK_FILE_COUNT))
        goto exit_free;
    if (!int_vec_alloc(&queue, BK_FILE_COUNT))
        goto exit_free;

    graph->file_table = file_table;
    graph->files = &files;
    graph->adjacencies = &adjacencies;
    graph->queue = &queue;
    graph->table_capacity = BK_FILE_COUNT;

    StringList target_files = {0};

    string_append(&buf, "build");
    if (os_create_directory(buf.data) == -1)
    {
        printf("Error creating build directory.\n");
        goto exit_free;
    }

    /* We are in the process of normalizing all path separators to use forward slash */
    const char *sep = "/";
    string_append(&buf, sep);
    string_append(&buf, name);

    if (os_create_directory(buf.data) == -1)
    {
        printf("Error creating target directory.\n");
        goto exit_free;
    }
    string_append(&buf, sep);
    // current is 'build/$target/'

    CommandTokenList cc_cmd = tokenize_build_command(arena, opt->cc_rule.command);

    size_t marker = buf.len;
    string_append(&buf, name);
    String cache_path = concat_cstr(arena, buf.data, ".cache");
    string_trunc(&buf, marker);

    BufferReader cache_contents;
    uint32_t target_count = get_cache_file(arena, cache_path.data, &cache_contents);

    size_t ext_len = strlen(opt->cc_rule.output_ext);

    CacheEntry *entries = arena_push_array(arena, CacheEntry, sources->count);

    size_t total_entry_sizes = 0;
    for (int i = 0; i < sources->count; ++i)
    {
        String path = sources->v[i];

        int index = path_last_sep(path.data, path.length);
        const char *filename = &path.data[index + 1];
        int last = strlast(filename, '.');
        if (last < 0)
        {
            last = path.length - index - 1; // get length of filename
        }

        string_trunc(&buf, marker);
        string_append_len(&buf, filename, last);
        string_append_len(&buf, opt->cc_rule.output_ext, ext_len);

        String target = string_copy_builder(arena, &buf);
        string_list_push(arena, &target_files, target);

        CacheEntry *entry = &entries[i];
        entry->target = target;

        int rebuild = 1;
        OSFileStat target_stat;
        
        int result = os_file_stat(target.data, &target_stat);

        uint32_t cache_entry_offset = 0;
        uint64_t cache_entry_pos = 0;
        if (result)
        {
            TimeStamp target_time = target_stat.last_write_time;
            target_filetimes[i] = target_time;

            if (target_count)
            {
                // check if target is in cache, the lookup will place the reader at the target if it is found
                cache_entry_offset = cache_lookup(&cache_contents, target_count, target);
                if (cache_entry_offset)
                {
                    cache_entry_pos = cache_contents.pos;
                    rebuild = check_cache_dependencies(arena, graph, &cache_contents, target_time);
                }
            }
        }

        if (rebuild)
        {
            if (!walk_source_file(arena, path, graph, opt->include_paths))
                goto exit_free;
            if (!run_build_rule(commands, path, target, &cc_cmd))
                goto exit_free;
        }
        else if (cache_entry_offset)
        {
            uint64_t cache_entry_size = cache_contents.pos - cache_entry_pos;
            total_entry_sizes += cache_entry_size;
            entry->size = (uint32_t)cache_entry_size;
            entry->offset = cache_entry_offset;
        }
    }

    // write cache contents to disk
    CacheInfo info;
    info.entries = entries;
    info.contents = &cache_contents;
    info.total_entry_sizes = total_entry_sizes;
    info.targets = &target_files;
    serialize_cache_file(arena, graph, cache_path.data, &info);
    int relink = 0;
    string_trunc(&buf, 0);
    if (string_append(&buf, opt->output_dir))
        string_append(&buf, sep);
    string_append(&buf, name);
    string_append(&buf, opt->output_rule.output_ext);
    String output_file = string_from_builder(&buf);

    if (!graph->visit_index)
    {
        OSFileStat output_stat;
        if (os_file_stat(output_file.data, &output_stat))
        {
            for (int i = 0; i < sources->count; ++i)
            {
                if (target_filetimes[i] > output_stat.last_write_time)
                {
                    // target is outdated
                    relink = 1;
                    break;
                }
            }
        }
        else
        {
            // Can't stat file
            relink = 1;
        }
    }
    else
    {
        // A file was recompiled
        relink = 1;
    }

    if (relink)
    {
        CommandTokenList out_cmd = tokenize_build_command(arena, opt->output_rule.command);
        run_build_rule(commands, string_list_join(arena, &target_files, ' '), output_file, &out_cmd);
    }

    if (opt->generate_compile_commands)
    {
        if (os_get_current_directory(path_buffer, OS_PATH_MAX))
        {
            const char *commands_file = "compile_commands.json";
            // TODO: arena allocate
            StringBuilder json;
            string_alloc(&json, 0, NULL);
            string_append(&json, "[\n");
            StringNode *target = target_files.head;
            for (int i = 0; i < sources->count; ++i)
            {
                string_append(&json, "  {\n    \"directory\": \"");
                string_append(&json, path_buffer);
                string_append(&json, "\",\n");
                string_append(&json, "    \"command\": ");
                string_append(&json, "\"");
                push_command_string(&json, &cc_cmd, sources->v[i], target->str);
                string_append(&json, "\",\n");
                string_append(&json, "    \"file\": \"");
                string_concat(&json, sources->v[i]);
                string_append(&json, "\"\n  }");
                if (i < sources->count - 1)
                    string_append(&json, ",");
                string_append(&json, "\n");
                target = target->next;
            }
            string_append(&json, "]\n");
            OSFile json_file;
            os_open_file("compile_commands.json", &json_file, OS_FILE_ACCESS_WRITE | OS_FILE_CREATE_IF_NOT_EXIST | OS_FILE_TRUNCATE);
            os_write_file(json_file, json.data, json.len);
            os_close_file(json_file);
            string_free(&json);
        }
        else
        {
            printf("Failed to get the current directory to generate compile commands.\n");
        }
    }

    result = 1;

exit_free:
    int_vec_free(&adjacencies);
    int_vec_free(&queue);
    FileNode_vec_free(&files);
    BK_FREE(file_table);
    arena_free(arena);
    return result;
}

int strfirst(const char *str, char c)
{
    for (int i = 0; str[i] != '\0'; ++i)
    {
        if (str[i] == c)
            return i;
    }
    return -1;
}

int strlast(const char *str, char c)
{
    int last_index = -1;
    for (int i = 0; str[i] != '\0'; ++i)
    {
        if (str[i] == c)
            last_index = i;
    }
    return last_index;
}

int strreplace(char *buffer, size_t buffer_size, const char *old_str, const char *new_str)
{
    size_t old_len = strlen(old_str);
    size_t new_len = strlen(new_str);
    size_t len = strlen(buffer);

    int delta = new_len - old_len;

    char *at = buffer;
    int occurences = 0;

    while (*at)
    {
        if (strncmp(at, old_str, old_len) == 0)
        {
            ++occurences;
            at += old_len;
            continue;
        }
        ++at;
    }

    if (!occurences)
        return 0;

    if (len + (delta * occurences) > buffer_size)
        return -1;

    at = buffer;

    while (*at)
    {
        if (strncmp(at, old_str, old_len) == 0)
        {
            if (delta)
                memmove(at + new_len, at + old_len, strlen(at) + 1);

            memcpy(at, new_str, new_len);
            at += new_len;
            //at += delta;
            continue;
        }
        ++at;
    }

    return occurences;
}

int path_last_sep(const char *path, int len)
{
    for (int i = len; i > 0; --i)
    {
        const char *at = path + i;
        if (is_path_separator(*at))
            return i;
    }
    return -1;
}

int path_first_sep(const char *path, int len)
{
    for (int i = 0; i < len; ++i)
    {
        const char *at = path + i;
        if (is_path_separator(*at))
            return i;
    }
    return -1;
}

int os_run_cmd(char *command, OSFile *file_out, OSFile *file_in)
{
#ifdef HOST_WINDOWS
    STARTUPINFOA startup_info = {0};
    startup_info.cb = sizeof(STARTUPINFOA);
    startup_info.hStdInput = file_in ? (HANDLE)file_in->handle : GetStdHandle(STD_INPUT_HANDLE);
    startup_info.hStdOutput = file_out ? (HANDLE)file_out->handle : GetStdHandle(STD_OUTPUT_HANDLE);
    startup_info.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    startup_info.dwFlags = STARTF_USESTDHANDLES;

    PROCESS_INFORMATION process_info = {0};
    BOOL success = CreateProcessA(NULL, command, NULL, NULL, TRUE, 0, NULL, NULL, &startup_info, &process_info);
    if (success != TRUE)
    {
        printf("Error creating process\n");
        return -1;
    }

    WaitForSingleObject(process_info.hProcess, INFINITE);

    DWORD ec;

    GetExitCodeProcess(process_info.hProcess, &ec);

    CloseHandle(process_info.hProcess);
    CloseHandle(process_info.hThread);

    return ec;
#else
    pid_t pid;
    switch (pid = fork())
    {
    case -1:
        return -1;
    case 0:
        execl("/bin/sh", "sh", "-c", command, NULL);
        exit(-1);
    default:
        break;
    }
#if 0
    if (file_out)
    {
        dup2((int)file_out.handle, STDOUT_FILENO);
    }
    if (file_in)
    {
        dup2((int)file_in.handle, STDIN_FILENO);
    }
#endif
    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status))
    {
        return WEXITSTATUS(status);
    }
    return -1; 
#endif
}

int os_create_pipe(OSFile *out_read, OSFile *out_write)
{
#ifdef HOST_WINDOWS
    SECURITY_ATTRIBUTES secur_attrib = {0};
    secur_attrib.bInheritHandle = TRUE;
    secur_attrib.nLength = sizeof(SECURITY_ATTRIBUTES);
    
    if (!CreatePipe((PHANDLE)&out_read->handle, (PHANDLE)&out_write->handle, &secur_attrib, 0))
    {
        printf("Failed to create pipe\n");
        return 0;
    }
#else
    #warning unimplemented call os_create_pipe
    return 0;
#endif
    return 1;
}

int os_read_file(OSFile file, size_t offset, size_t size, void *buffer)
{
#ifdef HOST_WINDOWS
    DWORD bytes_read;
    LARGE_INTEGER file_size;

    HANDLE hfile = (HANDLE)file.handle;

    if (size > 0xffffffff)
        return 0;

    DWORD offsetlo = offset & 0xffffffff;
    DWORD offsethi = (offset >> 32) & 0xffffffff;

    OVERLAPPED overlapped = {0};
    overlapped.Offset = offsetlo;
    overlapped.OffsetHigh = offsethi;

    if (ReadFile(hfile, buffer, (DWORD)size, &bytes_read, &overlapped))
    {
        if (bytes_read == size)
        {
            return 1;
        }
    }
    
    return 0;
#else
    size_t bytes_read = pread((int)file.handle, buffer, size, offset);
    if (bytes_read == size)
        return 1;
    return 0;
#endif
}

#endif /* BUILDKIT_IMPLEMENTATION */
