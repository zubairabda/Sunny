#include "stream.h"
#include "allocator.h"
#include <stdio.h>
#include <stdlib.h>

#pragma pack(push, 1)
struct wav_header
{
    u8 riff[4];
    u32 chunk_size;
    u8 wave[4];
};

struct wav_format_chunk
{
    u8 fmt[4];
    u32 chunk_size;
    u16 format_tag;
    u16 num_channels;
    u32 samples_per_second;
    u32 bytes_per_second;
    u16 block_align;
    u16 bits_per_sample;
};

struct wav_data_chunk
{
    u8 data[4];
    u32 chunk_size;
};
#pragma pack(pop)

enum cue_context_type
{
    CUE_CONTEXT_NONE,
    CUE_CONTEXT_FILE,
    CUE_CONTEXT_TRACK
};

typedef struct
{
    const char *str;
    u64 length;
} cue_string;

struct cue_parser
{
    const char *at;
    b8 done;
    b8 error;
    const char *msg;
};

struct cue_track
{
    u32 file_index;
    u32 pregap;
    u32 file_offset;
    u32 flags;
};

typedef struct
{
    const char **files;
    u32 file_count;
    struct cue_track *tracks;
    u32 track_count;
    struct memory_arena arena;
} cue_data;

static inline b8 is_whitespace(char c)
{
    return (c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r');
}

static inline b8 is_space(char c)
{
    return (c == ' ' || c == '\t');
}

static inline b8 is_eol(char c)
{
    return (c == '\n' || c == '\r');
}

static inline char to_lower(char c)
{
    return c + 32;
}

static inline s32 to_digit(char c)
{
    return (s32)c - 48;
}

b8 allocate_and_read_file(const char *path, u32 flags, struct file_dat *out_file)
{
    b8 result = false;

    FILE* f = fopen(path, "rb");
    if (!f)
        return false;

    fseek(f, 0, SEEK_END);
    u64 size = ftell(f);
    fseek(f, 0, SEEK_SET);

    u64 alloc_sz = (flags & FILE_FLAG_NULL_TERMINATE) ? size + 1 : size;
    u8 *buf = malloc(alloc_sz);
    if (fread(buf, 1, size, f) != size)
    {
        printf("Failed to read from file %s\n", path);
        goto end;
    }
    result = true;
    out_file->memory = buf;
    out_file->size = alloc_sz;
    if (flags & FILE_FLAG_NULL_TERMINATE)
    {
        buf[size] = '\0';
    }
end:
    fclose(f);
    return result;
}

b8 write_file(struct file_dat *file, char* out)
{
    b8 result = false;
    FILE* f = fopen(out, "wb");
    if (fwrite(file->memory, 1, file->size, f) != file->size)
    {
        printf("Failed to write to file %s\n", out);
        goto end;
    }
end:
    fclose(f);
    return result;
}

static b8 token_equals(cue_string token, const char *match)
{
    u32 i;
    for (i = 0; i < token.length; ++i)
    {
        if (token.str[i] != match[i] || match[i] == '\0')
            return false;
    }
    return (match[i] == '\0');
}

static cue_string get_token(struct cue_parser *parser)
{
    cue_string result = {0};
    char c;
    while ((c = parser->at[0]))
    {
        if (is_whitespace(c))
        {
            ++parser->at;
        }
        else
        {
            result.str = parser->at;
            while (parser->at[0] && !is_whitespace(parser->at[0]))
                ++parser->at;
            result.length = parser->at - result.str;
            return result;
        }
    }
    return result;
}

static b8 parse_number(struct cue_parser *parser, s32 *value)
{
    // assumes parser is already at a digit
    const char *start = parser->at;
    do
    {
        ++parser->at;
    } while (is_digit(parser->at[0]));

    if (!parser->at[0] || is_whitespace(parser->at[0]) || parser->at[0] == ':')
    {
        u32 len = parser->at - start;
        s32 place = 1;
        s32 result = 0;
        for (u32 i = 0; i < len; ++i)
        {
            char c = start[len - i - 1];
            result += to_digit(c) * place;
            place *= 10;
        }
        *value = result;
        return true;
    }
    return false;
}

static void parser_skip_line(struct cue_parser *parser)
{
    while (parser->at[0] && !is_eol(parser->at[0]))
        ++parser->at;
}

static b8 parse_cue_file(struct cue_parser *parser, cue_string *file_path)
{
    char c;
    while ((c = parser->at[0]))
    {
        if (is_space(c))
        {
            ++parser->at;
        }
        else if (c == '"')
        {
            ++parser->at;
            const char *start = parser->at;
            while (parser->at[0])
            {
                if (is_eol(parser->at[0]))
                {
                    return false;
                }
                else if (parser->at[0] == '"')
                {
                    file_path->str = start;
                    file_path->length = parser->at - start;
                    return true;
                }
                ++parser->at;
            }
            break;
        }
        else if (!is_whitespace(c))
        {
            const char *start = parser->at;
            while (parser->at[0] && !is_whitespace(parser->at[0]))
                ++parser->at;
            file_path->str = start;
            file_path->length = parser->at - start;
            return true;
        }
        else
        {
            break;
        }
    }
    return false;
}

static b8 parse_index(struct cue_parser *parser, s32 *out_index)
{
    char c;
    while ((c = parser->at[0]))
    {
        if (is_space(c))
        {
            ++parser->at;
        }
        else if (is_digit(c))
        {
            s32 index;
            if (!parse_number(parser, &index))
                break;
            *out_index = index;
            return true;
        }
        else
        {
            break;
        }
    }
    return false;
}

static b8 parse_msf(struct cue_parser *parser, s32 *out_lba)
{
    char c;
    while ((c = parser->at[0]))
    {
        if (is_space(c))
        {
            ++parser->at;
        }
        else if (is_digit(c))
        {
            s32 mm, ss, ff;
            b8 res[3];
            res[0] = parse_number(parser, &mm);
            if (parser->at[0] != ':')
                return false;
            ++parser->at;
            res[1] = parse_number(parser, &ss);
            if (parser->at[0] != ':')
                return false;
            ++parser->at;
            res[2] = parse_number(parser, &ff);

            if (!(res[0] & res[1] & res[2]))
                return false;

            *out_lba = (((mm * 60) + ss) * 75 + ff);

            return true;
        }
        else
        {
            break;
        }
    }
    return false;
}

static inline char *push_cue_string(struct memory_arena *arena, cue_string string)
{
    char *dst = push_arena(arena, string.length + 1);
    memcpy(dst, string.str, string.length);
    dst[string.length] = '\0';
    return dst;
}

#define MAX_CUE_TRACKS 99
#define MAX_CUE_FILES 99

enum
{
    CUE_TRACK_FLAG_LAST_TRACK = (1 << 0)
};

static void free_cue_sheet(cue_data *data)
{
    free_arena(&data->arena);
    free(data->files);
    free(data->tracks);
    free(data);
}

static inline void parser_error(struct cue_parser *parser, const char *msg)
{
    printf("%s", msg);
    parser->error = true;
}

static b8 parse_cue_sheet(const char *path, cue_data **data)
{
    struct file_dat cue_sheet;
    if (!allocate_and_read_file(path, FILE_FLAG_NULL_TERMINATE, &cue_sheet))
        return false;

    cue_string cue_dir = {0};
    // TODO: append dir

    struct cue_parser parser = {0};
    parser.at = cue_sheet.memory;

    u32 file_count = 0;
    u32 current_file_index = 0;

    s32 track_count = 0;

    s32 current_track_number = -1;
    s32 current_track_index = -1;
    s32 prev_track_offset = 0;

    cue_data *result = malloc(sizeof(cue_data));
    result->tracks = malloc(sizeof(struct cue_track) * MAX_CUE_TRACKS);
    result->files = malloc(sizeof(const char *) * MAX_CUE_FILES);
    result->arena = allocate_arena(KILOBYTES(8));

    enum cue_context_type context = CUE_CONTEXT_NONE;

    while (parser.at[0])
    {
        cue_string token = get_token(&parser);

        if (token_equals(token, "FILE"))
        {
            if (context == CUE_CONTEXT_FILE)
            {
                printf("Error: each file requires at least one track\n");
                break;
            }
            context = CUE_CONTEXT_FILE;
            
            if (file_count >= MAX_CUE_FILES)
            {
                parser_error(&parser, "Error: maximum file count reached\n");
                break;
            }

            cue_string file;
            if (!parse_cue_file(&parser, &file))
            {
                parser_error(&parser, "Error: failed to parse FILE in cue sheet\n");
                break;
            }

            b8 found = false;
            for (u32 i = 0; i < file_count; ++i)
            {
                if (token_equals(file, result->files[i]))
                {
                    current_file_index = i;
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                current_file_index = file_count;
                result->files[file_count++] = push_cue_string(&result->arena, file);
            }

            printf("FILE: %.*s\n", (int)file.length, file.str);
        }
        else if (token_equals(token, "TRACK"))
        {
            if (context == CUE_CONTEXT_NONE)
            {
                parser_error(&parser, "Error: TRACK command must be under a FILE command\n");
                break;
            }

            context = CUE_CONTEXT_TRACK;

            if (track_count >= MAX_CUE_TRACKS)
            {
                parser_error(&parser, "Error: maximum track count reached\n");
                break;
            }

            s32 index;
            if (!parse_index(&parser, &index))
            {
                parser_error(&parser, "Error: failed to parse track index\n");
                break;
            }

            if (index < 1 || index > 99)
            {
                parser_error(&parser, "Error: track number must be from 1-99\n");
                break;
            }

            if (current_track_number < 0)
            {
                current_track_number = index;
            }
            else if (index != ++current_track_number)
            {
                parser_error(&parser, "Error: track number did not increment by 1\n");
                break;
            }

            // reset the index counter
            current_track_index = -1;

            struct cue_track *track = &result->tracks[track_count++];
            track->file_index = current_file_index;
            track->flags = 0;

            printf("  Track: %d\n", index);
        }
        else if (token_equals(token, "INDEX"))
        {
            if (context != CUE_CONTEXT_TRACK)
            {
                parser_error(&parser, "Error: INDEX command found in incorrect context\n");
                break;
            }

            s32 index;
            if (!parse_index(&parser, &index))
            {
                parser_error(&parser, "Error: failed to parse index number\n");
                break;
            }

            if (index < 0 || index > 99)
            {
                parser_error(&parser, "Error: index number must be from 0-99\n");
                break;
            }

            s32 prev_track_index = current_track_index;
            if (current_track_index < 0)
            {
                if (index > 1)
                {
                    parser_error(&parser, "Error: index number must begin at either 0 or 1\n");
                    break;
                }
                current_track_index = index;
            }
            else if (index != ++current_track_index)
            {
                parser_error(&parser, "Error: index number did not increment by 1\n");
                break;
            }

            s32 offset;
            if (!parse_msf(&parser, &offset))
            {
                parser_error(&parser, "Error: failed to parse mm:ss:ff location\n");
                break;
            }

            struct cue_track *track = &result->tracks[track_count - 1];
            if (index == 1)
            {
                if (prev_track_index == 0)
                {
                    track->pregap = offset - prev_track_offset; // pregap is defined
                    track->file_offset = prev_track_offset;   
                }
                else
                {
                    track->pregap = 0;
                    track->file_offset = offset; // TODO: is this right?
                }
            }

            if (index <= 1 && track_count > 1)
            {
                struct cue_track *prev_track = &result->tracks[track_count - 2];
                if (prev_track->file_index != current_file_index)
                {
                    track->file_offset = 0;
                    prev_track->flags |= CUE_TRACK_FLAG_LAST_TRACK;
                }
            }
            prev_track_offset = offset;

            printf("    Index: %d\n", index);
        }
        else
        {
            parser_skip_line(&parser);
        }
    }

    free(cue_sheet.memory);

    if (parser.error)
    {
        printf("Failed to parse cue sheet\n");
        goto error;
    }
    else if (context == CUE_CONTEXT_FILE)
    {
        printf("Error: each file requires at least one track\n");
        goto error;
    }

    if (track_count)
    {
        result->tracks[track_count - 1].flags |= CUE_TRACK_FLAG_LAST_TRACK;
    }

    result->track_count = track_count;
    result->file_count = file_count;
    *data = result;
    return true;

error:
    free_cue_sheet(result);
    return false;
}

disk_image *open_disk(const char *path, psx_image_type type)
{
    disk_image *result = NULL;
    switch (type)
    {
    case BIN:
    {
        result = malloc(sizeof(disk_image));
        result->track_count = 1;
        result->tracks = malloc(sizeof(struct disk_track));
        result->files = malloc(sizeof(platform_file));
        if (!platform_open_file(path, &result->files[0]))
        {
            return NULL;
        }
        result->tracks[0].file = &result->files[0];
        result->tracks[0].end = platform_get_file_size(&result->files[0]) / DISK_SECTOR_SIZE;
        result->tracks[0].start = 0;
        break;
    }
    case CUE:
    {
        cue_data *data = NULL;
        if (parse_cue_sheet(path, &data))
        {
            // TODO: check for absolute paths?
            char file_path[1024];
            file_path[0] = '\0';
            s32 dir_index = -1;
            u32 i = 0;
            for (const char *p = path; p[0] != '\0'; ++p, ++i)
            {
                if (p[0] == '/' || p[0] == '\\')
                    dir_index = i;
            }
            if (dir_index >= 0)
            {
                memcpy(file_path, path, dir_index + 1);
                file_path[dir_index + 1] = '\0';
            }

            result = calloc(1, sizeof(disk_image));
            result->tracks = calloc(data->track_count, sizeof(struct disk_track));
            result->files = calloc(data->file_count, sizeof(platform_file));
            for (u32 i = 0; i < data->file_count; ++i)
            {
                strcat(file_path, data->files[i]);
                if (!platform_open_file(file_path, &result->files[i]))
                {
                    close_disk(result); // TODO:
                    return NULL;
                }
                // reset string to contain dir name
                file_path[dir_index + 1] = '\0';
            }

            u32 track_offset = 0;

            for (u32 i = 0; i < data->track_count; ++i)
            {
                platform_file *file = &result->files[data->tracks[i].file_index];
                result->tracks[i].file = file;
                u32 file_size = safe_truncate32(platform_get_file_size(file));
                result->tracks[i].pregap = data->tracks[i].pregap;
                u32 track_length = 0;
                // if this is the last track within a file, the size is calculated differently
                if (data->tracks[i].flags & CUE_TRACK_FLAG_LAST_TRACK)
                {
                    track_length = (file_size / DISK_SECTOR_SIZE) - data->tracks[i].file_offset;
                }
                else
                {
                    track_length = data->tracks[i + 1].file_offset - data->tracks[i].file_offset;
                }
                result->tracks[i].start = track_offset;
                result->tracks[i].end = track_offset + track_length;
                //prev_track_offset = data->tracks[i].file_offset;
                track_offset += track_length;
                printf("Track #%2d, start: %d, end: %d, len: %d\n", (i + 1), result->tracks[i].start, result->tracks[i].end, track_length);
            }
            result->file_count = data->file_count;
            result->track_count = data->track_count;
            free_cue_sheet(data);
        }

        break;
    }
    default:
        break;
    }
    return result;
}

void close_disk(disk_image *disk)
{
    for (u32 i = 0; i < disk->file_count; ++i)
    {
        platform_close_file(&disk->files[i]);
    }
    free(disk->files);
    free(disk->tracks);
    free(disk);
}

b8 read_disk_sector(disk_image *disk, u32 lba, void *buffer)
{
    for (u32 i = 0; i < disk->track_count; ++i)
    {
        struct disk_track *track = &disk->tracks[i];
        if (lba >= track->start && lba < track->end)
        {
            u32 offset = (lba - track->start) * DISK_SECTOR_SIZE;
            platform_read_file(track->file, offset, buffer, DISK_SECTOR_SIZE);
            return true;
        }
    }
    // out of bounds seek
    return false;
}

void write_bmp(int width, int height, void *data, const char *path)
{
#pragma pack(push, 1)
    typedef struct
    {
        u8 signature[2];
        u32 filesize;
        u32 reserved;
        u32 offset;
    } BmpHeader;

    typedef struct
    {
        u32 size;
        s32 width;
        s32 height;
        u16 planes;
        u16 bpp;
        u32 compression;
        u32 image_size;
        u32 pixels_per_m_x;
        u32 pixels_per_m_y;
        u32 colors_used;
        u32 important_colors;
    } BmpInfo;
#pragma pack(pop)
    FILE *file = fopen(path, "wb");

    u32 image_width = width;
    // if the width is odd, must pad scanlines to 4-byte boundary
    if (width & 0x1)
        image_width += 1;

    u32 px_data_size = image_width * height * 2;

    u16 *pixels = calloc(image_width * height, 2);
    if (!pixels)
        return;
    
    u16 *src = data;

    for (int i = 0; i < height; ++i)
        memcpy(&pixels[i * image_width], &src[i * width], width * 2);
    
    u32 filesize = px_data_size + sizeof(BmpHeader) + sizeof(BmpInfo);
    
    BmpHeader header = {0};
    header.signature[0] = 'B';
    header.signature[1] = 'M';
    header.filesize = filesize;
    header.offset = sizeof(BmpHeader) + sizeof(BmpInfo);

    BmpInfo info = {0};
    info.size = 40;
    info.width = width;
    info.height = -height;
    info.planes = 1;
    info.bpp = 16;

    fwrite(&header, sizeof(BmpHeader), 1, file);
    fwrite(&info, sizeof(BmpInfo), 1, file);
    fwrite(pixels, 1, px_data_size, file);

    fclose(file);

    free(pixels);
}

void write_wav_file(void *sample_data, u32 size_in_bytes, const char *path)
{
    FILE *f = fopen(path, "wb");
    struct wav_header header = {0};
    header.riff[0] = 'R';
    header.riff[1] = 'I';
    header.riff[2] = 'F';
    header.riff[3] = 'F';

    header.wave[0] = 'W';
    header.wave[1] = 'A';
    header.wave[2] = 'V';
    header.wave[3] = 'E';

    header.chunk_size = size_in_bytes + 36;

    fwrite(&header, sizeof(struct wav_header), 1, f);

    struct wav_format_chunk format = {0};
    format.fmt[0] = 'f';
    format.fmt[1] = 'm';
    format.fmt[2] = 't';
    format.fmt[3] = ' ';
    format.bits_per_sample = 16;
    format.block_align = 4;
    format.bytes_per_second = 44100 * 4;
    format.chunk_size = 16;
    format.format_tag = 1;
    format.num_channels = 2;
    format.samples_per_second = 44100;
    
    fwrite(&format, sizeof(struct wav_format_chunk), 1, f);

    struct wav_data_chunk chunk = {0};
    chunk.data[0] = 'd';
    chunk.data[1] = 'a';
    chunk.data[2] = 't';
    chunk.data[3] = 'a';
    chunk.chunk_size = size_in_bytes;

    fwrite(&chunk, sizeof(struct wav_data_chunk), 1, f);
    fwrite(sample_data, 1, size_in_bytes, f);
    fclose(f);
}
