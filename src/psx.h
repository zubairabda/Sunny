#ifndef PSX_H
#define PSX_H

#include "allocator.h"
#include "config.h"
#include "platform/platform.h"

typedef enum psx_image_type
{
    EXE,
    BIN,
    CUE,
    ISO
} psx_image_type;

void psx_mount_image(platform_file file, psx_image_type type);
b8 psx_mount_from_file(const char *path);
void psx_load_image(void);
void psx_reset(void);
//void bios_init(void *bios);
//b8 psx_mount_exe(platform_file *file);

void psx_init(struct memory_arena *arena, void *bios);
void psx_run(void);
void psx_step(void);

#endif /* PSX_H */
