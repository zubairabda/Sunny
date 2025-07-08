#ifndef STREAM_H
#define STREAM_H

#include "platform/platform.h"

#define CDROM_SECTOR_SIZE 2352

struct file_dat
{
    void *memory;
    u64 size;
};

typedef enum
{
    EXE,
    BIN,
    CUE,
    ISO
} psx_image_type;

struct disk_track
{
    platform_file *file;
    u32 size;
    u32 offset;
    u32 pregap;
    u32 reserved;
};

typedef struct
{
    u32 track_count;
    u32 file_count;
    struct disk_track *tracks;
    platform_file *files;
} disk_image;

typedef struct
{
    u8 m;
    u8 s;
    u8 f;
} MSF;

inline MSF lba_to_msf(u32 lba)
{
    MSF result;
    lba += 150;
    result.m = lba / (60 * 75);
    result.s = (lba / 75) % 60;
    result.f = lba % 75;
    return result;
}

inline u32 msf_to_lba(MSF pos)
{
    return (((pos.m * 60) + pos.s) * 75 + pos.f) - 150;
}

enum
{
    FILE_FLAG_NULL_TERMINATE = (1 << 0)
};

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

b8 allocate_and_read_file(const char *path, u32 flags, struct file_dat *out_file);
b8 write_file(struct file_dat *file, char *out);

disk_image *open_disk(const char *path, psx_image_type type);
void close_disk(disk_image *disk);

b8 read_disk_data(disk_image *disk, u32 offset, void *buffer);

void write_bmp(u32 width, u32 height, u8 *data, char *filename);
void write_wav_file(void *sample_data, u32 size_in_bytes, char *dstpath);

#endif /* STREAM_H */
