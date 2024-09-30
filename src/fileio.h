#ifndef FILEIO_H
#define FILEIO_H

#include "common.h"

struct file_dat
{
    void *memory;
    u64 size;
};

typedef struct
{
    uintptr_t handle;
} platform_file;

platform_file open_file(const char *path);
b8 file_is_valid(platform_file file);
void close_file(platform_file file);
s64 read_file(platform_file file, u64 offset, void *dst_buffer, u32 bytes_to_read);

b8 allocate_and_read_file(const char* path, struct file_dat* out_file);
b8 allocate_and_read_file_null_terminated(const char *path, struct file_dat *out_file);
b8 write_file(struct file_dat* file, char *out);
void parse_cue_file(const char *path);
void write_bmp(u32 width, u32 height, u8* data, char* filename);

#endif /* FILEIO_H */
