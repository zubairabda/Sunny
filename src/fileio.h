#ifndef FILEIO_H
#define FILEIO_H

#include "common.h"

enum CueContextType
{
    CUE_CONTEXT_TYPE_NONE,
    CUE_CONTEXT_TYPE_FILE,
    CUE_CONTEXT_TYPE_TRACK
};

enum CueFileType
{
    CUE_FILE_TYPE_BINARY,
    CUE_FILE_TYPE_COUNT
};

struct CueTable
{
    int foo;
};

enum CueTokenType
{
    CUE_TOKEN_TYPE_IDENTIFIER,
    CUE_TOKEN_TYPE_STRING,
    CUE_TOKEN_TYPE_NUMBER,
    CUE_TOKEN_TYPE_UNKNOWN,
    CUE_TOKEN_TYPE_NULL,
    CUE_TOKEN_TYPE_COUNT
};

struct Parser
{
    u8* ptr;
    u8* end;
};

struct CueToken
{
    u8* t;
    enum CueTokenType type;
    u32 length;
};

struct FileInfo
{
    void* memory;
    u64 size;
};

b8 read_file(char* path, struct FileInfo* info);
b8 write_file(struct FileInfo* file, char* out);
void parse_cue_file(struct FileInfo* cue);
void write_bmp(u32 width, u32 height, u8* data, char* filename);

#endif
