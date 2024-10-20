#include "fileio.h"
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

enum cue_file_type
{
    CUE_FILE_TYPE_BINARY,
    CUE_FILE_TYPE_COUNT
};

enum cue_track_type
{
    CUE_TRACK_AUDIO,
    CUE_TRACK_MODE2_FORM1,
    CUE_TRACK_MODE2_FORM2
};

struct CueTable
{
    int foo;
};

struct cue_parser
{
    char* at;
    char* end;
};

struct cue_token
{
    char *text;
    u32 length;
    //enum cue_token_type type;
};

static inline b8 is_whitespace(char c)
{
    return (c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r');
}

static inline b8 is_eol(char c)
{
    return (c == '\n' || c == '\r');
}

static inline b8 is_letter(char c)
{
    return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
}

static inline char to_lower(char c)
{
    return c + 32;
}

static inline b8 is_digit(char c)
{
    return (c >= '0' && c <= '9');
}

static inline s32 to_digit(char c)
{
    return c - 48;
}

b8 allocate_and_read_file(const char* path, struct file_dat* out_file)
{
    b8 result = SY_FALSE;

    FILE* f = fopen(path, "rb");
    if (!f) {
        return SY_FALSE;
    }

    fseek(f, 0, SEEK_END);
    u64 size = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8* buf = malloc(size);
    if (fread(buf, 1, size, f) != size)
    {
        printf("Failed to read from file %s\n", path);
        goto end;
    }
    result = SY_TRUE;
    out_file->memory = buf;
    out_file->size = size;
end:
    fclose(f);
    return result;
}

b8 allocate_and_read_file_null_terminated(const char *path, struct file_dat *out_file)
{
    FILE* f = fopen(path, "rb");
    if (!f) {
        return SY_FALSE;
    }
    fseek(f, 0, SEEK_END);
    u64 size = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8 *buf = malloc(size + 1);

    if (fread(buf, 1, size, f) != size)
    {
        printf("Failed to read from file %s\n", path);
        fclose(f);
        return SY_FALSE;
    }

    buf[size] = '\0';
    out_file->memory = buf;
    out_file->size = size + 1;
    return SY_TRUE;
}

b8 write_file(struct file_dat* file, char* out)
{
    b8 result = SY_FALSE;
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

static b8 token_compare(struct cue_token token, const char *match)
{
    for (u32 i = 0; i < token.length; ++i)
    {
        if (token.text[i] != match[i] || match[i] == '\0')
            return SY_FALSE;
    }
    return SY_TRUE;
}

static struct cue_token get_token(struct cue_parser *parser)
{
    struct cue_token result = {0};
    result.length = 1;

    for (;;)
    {
        if (parser->at[0] == '\0') {
            return result;
        }
        else if (is_whitespace(parser->at[0]))
        {
            if (is_eol(parser->at[0])) {
                return result;
            }
            ++parser->at;
        }
        else
        {
            break;
        }
    }

    if (parser->at[0] == '"')
    {
        //result.type = CUE_TOKEN_TYPE_STRING;
        result.text = ++parser->at;
        for (;;)
        {
            if (parser->at[0] == '"')
            {
                result.length = parser->at - result.text;
                ++parser->at;
                return result;
            }
            else if (parser->at[0] == '\0')
            {
                printf("Error: closing quote not found!\n");
                result.text = NULL;
                return result;
            }
            else if (is_eol(parser->at[0])) {
                printf("Error: found newline in string\n");
                result.text = NULL;
                return result;
            }
            ++parser->at;

        }
    }
    else
    {
        result.text = parser->at;
        while (parser->at[0] && !is_whitespace(parser->at[0])) {
            ++parser->at;
        }
        result.length = parser->at - result.text;
        return result;
    }
}

static s32 parse_number(struct cue_token token)
{
    s32 place = 1;
    s32 result = 0;
    for (u32 i = 0; i < token.length; ++i)
    {
        char c = token.text[token.length - i - 1];
        if (!is_digit(c)) {
            return -1;
        }
        result += to_digit(c) * place;
        place *= 10;
    }
    return result;
}

static void get_next_line(struct cue_parser *parser)
{
    while (parser->at[0])
    {
        if (is_eol(parser->at[0]))
        {
            while (is_eol(parser->at[0])) 
            {
                ++parser->at;
            }
            break;
        }
        ++parser->at;
    }
}

static const char *cue_context_to_str(enum cue_context_type context)
{
    switch (context)
    {
    case CUE_CONTEXT_NONE:
        return "None";
    case CUE_CONTEXT_FILE:
        return "File";
    case CUE_CONTEXT_TRACK:
        return "Track";
    }
}

void parse_cue_file(const char *path)
{
    struct file_dat file;
    if (!allocate_and_read_file_null_terminated(path, &file))
        return;

    struct cue_parser parser = {0};
    parser.at = file.memory;

    enum cue_context_type context = CUE_CONTEXT_NONE;

    while (parser.at[0]) 
    {
        struct cue_token token = get_token(&parser);
        if (!token.text) {
            printf("Error parsing cue file\n");
            break;
        }

        if (token_compare(token, "FILE"))
        {
            if (context != CUE_CONTEXT_NONE) {
                printf("Error: command 'FILE' found in context '%s'\n", cue_context_to_str(context));
                break;
            }
            context = CUE_CONTEXT_FILE;

            struct cue_token file_name = get_token(&parser);

            struct cue_token file_type = get_token(&parser); // TODO: handle filenames that begin with a number
            if (!token_compare(file_type, "BINARY")) { // NOTE: the only file type we support
                printf("Error: Unexpected identifier %.*s, valid file-types are: BINARY\n", file_type.length, file_type.text);
                break;
            }
        }
        else if (token_compare(token, "REM"))
        {
            get_next_line(&parser);
            continue;
        }
        else if (token_compare(token, "TRACK"))
        {
            if (context != CUE_CONTEXT_FILE) {
                printf("Error: command 'TRACK' found in context '%s'\n", cue_context_to_str(context));
                break;
            }
            context = CUE_CONTEXT_TRACK;

            token = get_token(&parser);
            s32 index = parse_number(token);
            if (index < 0 || index > 99) {
                printf("Error: invalid track index\n");
                break;
            }
            token = get_token(&parser);
            enum cue_track_type mode;
            if (token_compare(token, "AUDIO")) {
                mode = CUE_TRACK_AUDIO;
            }
            else if (token_compare(token, "MODE2/2048")) {
                mode = CUE_TRACK_MODE2_FORM1;
            }
            else if (token_compare(token, "MODE2/2352")) {
                mode = CUE_TRACK_MODE2_FORM2;
            }
            else {
                printf("Error: unsupported track type '%.*s'\n", token.length, token.text);
                break;
            }

            printf("Track: %d, mode: %s\n", index, mode == CUE_TRACK_AUDIO ? "AUDIO" : mode == CUE_TRACK_MODE2_FORM1 ? "MODE2/2048" : "MODE2/2352");
        }
        else if (token_compare(token, "INDEX"))
        {
            if (context != CUE_CONTEXT_TRACK) {
                printf("Error: command 'INDEX' found in context '%s'\n", cue_context_to_str(context));
                break;
            }


        }
        else
        {
            printf("Error: unknown cue sheet command '%.*s'\n", token.length, token.text);
        }

        //printf("%.*s\n", token.length, token.text);

        get_next_line(&parser);
    }

}

void write_bmp(u32 width, u32 height, u8* data, char* filename)
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
