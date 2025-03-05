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

enum cue_token_type
{
    CUE_TOKEN_INVALID,
    CUE_TOKEN_IDENTIFIER,
    CUE_TOKEN_KEYWORD,
    CUE_TOKEN_NUMBER,
    CUE_TOKEN_STRING,
    CUE_TOKEN_COLON,
    CUE_TOKEN_NEWLINE
};

enum cue_keyword_id
{
    CUE_ID_NONE,
    CUE_ID_FILE,
    CUE_ID_INDEX,
    CUE_ID_TRACK,
    CUE_ID_BINARY,
    CUE_ID_AUDIO,

};

struct cue_token
{
    const char *text;
    u32 length;
    enum cue_token_type type;
};

typedef struct
{
    const char *str;
    u64 length;
} cue_string;

struct cue_parser
{
    const char *at;
    struct cue_token token;
    b8 done;
    b8 error;
    const char *msg;
};

typedef struct
{
    const char **files;
    u32 file_count;
    u32 *track_indices;
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

static struct cue_token consume_token(struct cue_parser *parser, u32 length, enum cue_token_type type)
{
    parser->token.length = length;
    parser->token.type = type;
    parser->token.text = parser->at;
    return parser->token;
}

static struct cue_token parse_string(struct cue_parser *parser)
{
    ++parser->at;
    parser->token.text = parser->at;
    parser->token.type = CUE_TOKEN_STRING;

    while (*parser->at)
    {
        if (*parser->at == '"')
        {
            parser->token.length = parser->at - parser->token.text;
            return parser->token;
        }
    };
    parser->token.type = CUE_TOKEN_INVALID;
    return parser->token;
}

static cue_string get_token(struct cue_parser *parser)
{
#if 0
    char c;
    while ((c = parser->at[0]))
    {
        if (is_whitespace(c))
        {
            ++parser->at;
        }
        else if (is_alpha(c))
        {
            struct cue_token result;
            result.type = CUE_TOKEN_IDENTIFIER;
            result.text = parser->at;
            do
            {
                ++parser->at;
            } while (is_alpha(*parser->at) || is_digit(*parser->at));
            result.length = parser->at - result.text;
            parser->token = result;
            return result;
        }
        else if (is_digit(c))
        {
            struct cue_token result;
            result.type = CUE_TOKEN_NUMBER;
            result.text = parser->at;
            do
            {
                ++parser->at;
            } while (is_digit(*parser->at));
            result.length = parser->at - result.text;
            parser->token = result;
            return result;
        }
        else if (c == '"')
        {
            return parse_string(parser);
        }
        else if (c == ':')
        {
            return consume_token(parser, 1, CUE_TOKEN_COLON);
        }
        else
        {
            printf("Error: unknown identifier '%c'\n", c);
            return consume_token(parser, 1, CUE_TOKEN_INVALID);
        }
    }
    
    parser->done = true;
#else
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
#endif
}

static b8 parse_number(struct cue_parser *parser, s32 *value)
{
    // assumes parser is already at a digit
    const char *start = parser->at;
    do
    {
        ++parser->at;
    } while (is_digit(parser->at[0]));

    if (!parser->at[0] || is_whitespace(parser->at[0]))
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

static void parser_consume_space(struct cue_parser *parser)
{
    while (is_space(parser->at[0]))
        ++parser->at;
}

static void parser_skip_line(struct cue_parser *parser)
{
    while (parser->at[0] && !is_eol(parser->at[0]))
        ++parser->at;
}

struct file_table_entry
{
    const char *name;
    u32 length;
    platform_file file;
};

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

static b8 parse_cue_track(struct cue_parser *parser, s32 *index)
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
            s32 track_index;
            if (!parse_number(parser, &track_index))
                break;
            *index = track_index;
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

static void free_cue_sheet(cue_data *data)
{
    free_arena(&data->arena);
    free(data->files);
    free(data->track_indices);
    free(data);
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

    u32 track_count = 0;

    s32 current_track_number = -1;

    cue_data *result = malloc(sizeof(cue_data));
    result->track_indices = malloc(sizeof(u32) * MAX_CUE_TRACKS);
    result->files = malloc(sizeof(const char *) * MAX_CUE_FILES);
    result->arena = allocate_arena(KILOBYTES(8));
    //s32 current_track_index = -1;

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
                printf("Error: maximum file count reached\n");
                parser.error = true;
                break;
            }

            cue_string file;
            if (!parse_cue_file(&parser, &file))
            {
                printf("Error: failed to parse FILE in cue sheet\n");
                parser.error = true;
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
                printf("Error: TRACK command must be under a FILE command\n");
                break;
            }
            context = CUE_CONTEXT_TRACK;

            if (track_count >= MAX_CUE_TRACKS)
            {
                printf("Error: maximum track count reached\n");
                parser.error = true;
                break;
            }

            s32 index;
            if (!parse_cue_track(&parser, &index))
            {
                printf("Error: failed to parse track\n");
                parser.error = true;
                break;
            }

            if (index < 1 || index > 99)
            {
                printf("Error: track number must be from 1-99\n");
                parser.error = true;
                break;
            }

            if (current_track_number < 0)
            {
                current_track_number = index;
            }
            else if (index != ++current_track_number)
            {
                printf("Error: track number did not increment by 1\n");
                parser.error = true;
                break;
            }

            result->track_indices[track_count++] = current_file_index;

            printf("  Track: %d\n", index);
        }
        else if (token_equals(token, "INDEX"))
        {
            if (context != CUE_CONTEXT_TRACK)
            {
                printf("Error: INDEX command found in incorrect context\n");
                break;
            }
#if 0
            token = get_token(&parser);
            s32 index = parse_number(token);
            if (current_track_index < 0)
            {
                if (index == 0)
                {
                    // get track pregap start time
                }
                else if (index == 1)
                {

                }
                else
                {
                    printf("Error: starting index must be 0 or 1\n");
                    break;
                }
                current_track_index = index;
            }
            else if (index != ++current_track_index)
            {
                printf("Error: track index did not increment by 1\n");
                break;
            }
#endif
        }
        else
        {
            parser_skip_line(&parser);
        }
        //printf("%.*s\n", token.length, token.text);
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
        result->tracks[0].size = platform_get_file_size(&result->files[0]);
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

            result = malloc(sizeof(disk_image));
            result->tracks = malloc(sizeof(struct disk_track) * data->track_count);
            result->files = malloc(sizeof(platform_file) * data->file_count);
            for (u32 i = 0; i < data->file_count; ++i)
            {
                strcat(file_path, data->files[i]);
                if (!platform_open_file(file_path, &result->files[i]))
                {
                    close_disk(result); // TODO:
                    return NULL;
                }
                file_path[dir_index + 1] = '\0';
            }
            for (u32 i = 0; i < data->track_count; ++i)
            {
                platform_file *file = &result->files[data->track_indices[i]];
                result->tracks[i].file = file;
                result->tracks[i].size = platform_get_file_size(file);
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
    memset(disk, 0, sizeof(disk_image));
}

b8 read_disk_data(disk_image *disk, u32 offset, void *buffer)
{
    u32 size = 0;
    for (u32 i = 0; i < disk->track_count; ++i)
    {
        u32 src = size;
        size += disk->tracks[i].size;
        // TODO: not sure if reads at offset == size are allowed?
        if (size > offset)
        {
            platform_read_file(disk->tracks[i].file, (offset - src), buffer, 2352);
            return true;
        }
    }
    // out of bounds seek
    return false;
}

void write_bmp(u32 width, u32 height, u8 *data, char *filename)
{
#pragma pack(push, 1)
    typedef struct
    {
        u16 signature;
        u32 filesize;
        u32 reserved;
        u32 offset;
    } BmpHeader;
    typedef struct
    {
        u32 size;
        u32 width;
        u32 height;
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
    FILE* file = fopen(filename, "wb");

    u32 filesize = (width * height * 2) + sizeof(BmpHeader) + sizeof(BmpInfo);
    BmpHeader header = {0};
    header.signature = *(u16*)"BM";
    header.filesize = filesize;
    header.offset = sizeof(BmpHeader) + sizeof(BmpInfo);

    BmpInfo info = {0};
    info.size = 40;
    info.width = width;
    info.height = height;
    info.planes = 1;
    info.bpp = 16;
    // NOTE: set image size?
    u64 size = width * height * (info.bpp >> 3);
    fwrite(&header, sizeof(BmpHeader), 1, file);
    fwrite(&info, sizeof(BmpInfo), 1, file);
    //u8* buffer = malloc(size);
    //reverse_memcpy(data, buffer, size);
    fwrite(data, 2, (width * height), file);
    fclose(file);
    //free(buffer);
}

void write_wav_file(void *sample_data, u32 size_in_bytes, char *dstpath)
{
    FILE *f = fopen(dstpath, "wb");
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
