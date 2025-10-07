#ifndef STREAM_H
#define STREAM_H

#include "platform/platform.h"

#define DISK_SECTOR_SIZE 0x930

struct file_dat
{
    void *memory;
    u64 size;
};

typedef enum
{
    IMAGE_TYPE_NONE,
    IMAGE_TYPE_EXE,
    IMAGE_TYPE_BIN,
    IMAGE_TYPE_CUE,
    IMAGE_TYPE_ISO
} psx_image_type;

struct disk_track
{
    platform_file *file;
    u32 start;
    u32 end;
    u32 pregap;
    u32 file_offset; // NOTE: this is for tracks that have unaddressable content in their file (before the first specified index)
};

typedef struct disk_image
{
    u32 track_count;
    u32 file_count;
    struct disk_track *tracks;
    platform_file *files;
} disk_image;

typedef struct MSF
{
    u8 m;
    u8 s;
    u8 f;
} MSF;

inline MSF lba_to_msf(u32 lba)
{
    MSF result;
    result.m = lba / (60 * 75);
    result.s = (lba / 75) % 60;
    result.f = lba % 75;
    return result;
}

inline u32 msf_to_lba(u8 m, u8 s, u8 f)
{
    return (((m * 60) + s) * 75 + f);
}

inline b8 is_alpha(char c)
{
    return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_');
}

inline b8 is_digit(char c)
{
    return (c >= '0' && c <= '9');
}

inline b8 str_to_hex(const char *string, u32 *value)
{
    u32 result = 0;
    s32 len = strlen(string);
    s32 place = 0;
    while (len--)
    {
        char c = string[len];
        s32 digit = 0;

        if (is_digit(c))
        {
            digit = c - 48;
        }
        else if ((c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))
        {
            if (c <= 'F')
                c |= 0x20;
            digit = c - 87;
        }
        else
        {
            return false;
        }
        result |= (digit << place);
        place += 4;
    }
    *value = result;
    return true;
}

b8 allocate_and_read_file(const char *path, b8 null_terminate, struct file_dat *out_file);
b8 write_file(struct file_dat *file, char *out);

disk_image *open_disk(const char *path, psx_image_type type);
void close_disk(disk_image *disk);

b8 read_disk_sector(disk_image *disk, u32 lba, void *buffer);
b8 read_disk_data(disk_image *disk, u32 lba, size_t size, void *buffer);

void write_bmp(int width, int height, void *data, const char *path);
void write_wav_file(void *sample_data, u32 size_in_bytes, const char *path);

#endif /* STREAM_H */
