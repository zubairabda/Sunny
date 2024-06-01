#include "fileio.h"
#include <stdio.h>
#include <stdlib.h>

b8 read_file(char* path, struct FileInfo* info)
{
    b8 result = SY_FALSE;

    FILE* f = fopen(path, "rb");
    fseek(f, 0, SEEK_END);
    u64 size = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8* buff = malloc(size);
    if (fread(buff, 1, size, f) != size)
    {
        printf("Failed to read from file %s\n", path);
        goto end;
    }
    result = SY_TRUE;
    info->memory = buff;
    info->size = size;
end:
    fclose(f);
    return result;
}

b8 write_file(struct FileInfo* file, char* out)
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

static inline void clear_spaces(struct Parser* parser)
{
    while (parser->ptr[0] == ' ' || parser->ptr[0] == '\t' || parser->ptr[0] == '\r' || parser->ptr[0] == '\n')
        ++parser->ptr;
}

static b8 token_compare(struct CueToken token, char* string) // TODO: case sensitivity
{
    for (u32 i = 0; i < token.length; ++i)
    {
        if (token.t[i] != string[i])
            return SY_FALSE;
    }
    return SY_TRUE;
}

static inline b8 is_letter(u8 c)
{
    return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
}

static inline b8 is_digit(u8 c)
{
    return (c >= '0' && c <= '9');
}

static struct CueToken get_token(struct Parser* parser)
{
    clear_spaces(parser);
    struct CueToken result = {0};
    result.t = parser->ptr;
    result.length = 1;

    if (parser->ptr[0] == '"')
    {
        result.type = CUE_TOKEN_TYPE_STRING;
        result.t = ++parser->ptr;
        while ((parser->ptr < parser->end) && (parser->ptr[0] != '"'))
        {
            ++parser->ptr;
        }
        if (parser->ptr >= parser->end)
        {
            printf("Error: closing quote not found!\n");
            result.t = NULL;
        }
        else
        {
            result.length = parser->ptr - result.t;
            ++parser->ptr;
        }
    }
    else if (is_letter(parser->ptr[0]))
    {
        result.type = CUE_TOKEN_TYPE_IDENTIFIER;
        while ((parser->ptr < parser->end) && (is_letter(parser->ptr[0]) || parser->ptr[0] == '.' || parser->ptr[0] == '_'))
        {
            ++parser->ptr;
        }
        result.length = parser->ptr - result.t;
    }
    else if (is_digit(parser->ptr[0]))
    {
        result.type = CUE_TOKEN_TYPE_NUMBER;
        while ((parser->ptr < parser->end) && is_digit(parser->ptr[0]))
        {
            ++parser->ptr;
        }
        result.length = parser->ptr - result.t;
    }
    else
    {
        result.type = CUE_TOKEN_TYPE_UNKNOWN; // TODO: ideally this should be handled, but for now were just gonna skip over unknown symbols
        ++parser->ptr;
    }

    return result;
}

void parse_cue_file(struct FileInfo* cue)
{
    struct Parser parser = {0};
    parser.ptr = cue->memory;
    parser.end = (u8*)cue->memory + cue->size;

    enum CueContextType context = CUE_CONTEXT_TYPE_NONE;

    while(parser.ptr < parser.end)
    {
        if (parser.ptr >= parser.end)
        {
            printf("End of file reached...\n");
            break;
        }
        struct CueToken token = get_token(&parser);
        if (!token.t)
        {
            printf("Error parsing cue file\n");
            break;
        }

        if (token_compare(token, "FILE"))
        {
            context = CUE_CONTEXT_TYPE_FILE;
            struct CueToken file_name = get_token(&parser);

            struct CueToken file_type = get_token(&parser); // TODO: handle filenames that begin with a number
            if (!token_compare(file_type, "BINARY")) // NOTE: the only file type we support
            {
                printf("Error: Unexpected identifier %.*s, valid file-types are: BINARY\n", file_type.length, file_type.t);
                break;
            }

        }
        else if (token_compare(token, "REM"))
        {
            while (parser.ptr < parser.end && (parser.ptr[0] != '\n' || parser.ptr[0] != '\r'))
                ++parser.ptr;
        }
        else if (token_compare(token, "TRACK"))
        {

        }
        else if (token_compare(token, "INDEX"))
        {

        }
        else
        {

        }
        printf("%.*s\n", token.length, token.t);
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