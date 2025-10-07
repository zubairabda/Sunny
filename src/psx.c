#include "psx.h"
#include "event.h"
#include "cpu.h"
#include "gpu.h"
#include "counters.h"
#include "cdrom.h"
#include "dma.h"
#include "spu.h"
#include "memory.h"
#include "debug.h"

psx_state g_psx;

b8 psx_load_exe(platform_file *file)
{
    u64 fsize = platform_get_file_size(file);
    SY_ASSERT(fsize <= 0xffffffff);
    void *buffer = malloc(fsize);
    platform_read_file(file, 0, buffer, fsize);

    u8 *fp = buffer;
    if (memcmp(fp, "PS-X EXE", 8) != 0)
    {
        return false;
    }

    psx_reset();

    while (g_cpu.pc != 0x80030000)
        psx_step();

    u32 dst = U32FromPtr(fp + 0x18);
    u32 size = U32FromPtr(fp + 0x1c);
    memcpy((g_ram + (dst & 0x1fffffff)), (fp + 0x800), size);
    
    g_cpu.pc = U32FromPtr(fp + 0x10);
    g_cpu.registers[28] = U32FromPtr(fp + 0x14);
    if (U32FromPtr(fp + 0x30) != 0)
    {
        g_cpu.registers[29] = U32FromPtr(fp + 0x30) + U32FromPtr(fp + 0x34);
        g_cpu.registers[30] = U32FromPtr(fp + 0x30) + U32FromPtr(fp + 0x34);
    }

    free(buffer);

    g_cpu.next_pc = g_cpu.pc + 4;

    return true;
}

psx_image_type psx_get_image_type_from_path(const char *path)
{
    if (string_ends_with_ignore_case(path, ".exe"))
    {
        return IMAGE_TYPE_EXE;
    }
    else if (string_ends_with_ignore_case(path, ".bin"))
    {
        return IMAGE_TYPE_BIN;
    }
    else if (string_ends_with_ignore_case(path, ".cue"))
    {
        return IMAGE_TYPE_CUE;
    }
    else
    {
        return IMAGE_TYPE_NONE;
    }
}

void psx_format_memcard(u8 *data)
{
    memset(data, 0, MEMCARD_SIZE);
    data[0] = 'M';
    data[1] = 'C';
    data[0x7f] = 0xe;

    // initialize directory frames
    for (int i = 1; i < 16; ++i)
    {
        u8 *frame = &data[128 * i];
        frame[0] = 0xa0;
        frame[8] = 0xff;
        frame[9] = 0xff;
        frame[0x7f] = 0xa0;
    }

    // initialize broken sector list
    for (int i = 16; i < 36; ++i)
    {
        u32 *frame = (u32 *)&data[128 * i];
        frame[0] = 0xffffffff;
    }

    // sector replacement data / unused frames
    for (int i = 36; i < 63; ++i)
    {
        u8 *frame = &data[128 * i];
        memset(frame, 0xff, 128);
    }

    // write test frame - same as frame 0
    memcpy(&data[128 * 63], &data[0], 128);
#if 0
    // initialize remaining 15 file blocks
    for (int i = 0; i < 15; ++i)
    {

    }
#endif
}

b8 psx_load_image(const char *path, psx_image_type type)
{
    b8 result = false;
    switch (type)
    {
    case IMAGE_TYPE_EXE:
    {
        platform_file exe;
        if (platform_open_file(path, FILE_OPEN_READ, &exe))
        {
            result = psx_load_exe(&exe);
            platform_close_file(&exe);
        }
        break;
    }
    case IMAGE_TYPE_BIN:
    case IMAGE_TYPE_CUE:
    {
        disk_image *disk = open_disk(path, type);
        if (disk)
        {
            if (g_psx.disk != NULL)
                close_disk(g_psx.disk);
            g_psx.disk = disk;
            psx_reset();
            result = true;
        }   
        break;
    }
    default:
        return false;
    }

    if (result)
    {
        strcpy(g_psx.image_path, path);

        platform_close_file(&g_psx.memcard);

        char memcard_path[OS_PATH_MAX];
        int len = sprintf(memcard_path, "memcards/");
        if (platform_create_dir(memcard_path))
        {
            const char *str = NULL;
            u32 name_len = platform_get_file_base_name(path, &str);
            // should always succeed
            SY_ASSERT(name_len);
            
            strncat(memcard_path, str, name_len);
            strcat(memcard_path, ".mcd");

            if (platform_open_file(memcard_path, FILE_OPEN_READ | FILE_OPEN_WRITE | FILE_OPEN_CREATE, &g_psx.memcard))
            {
                u64 temp = g_psx.arena.used;
                u8 *data = push_arena(&g_psx.arena, MEMCARD_SIZE);

                b8 format = false;
                if (platform_read_file(&g_psx.memcard, 0, data, MEMCARD_SIZE) < MEMCARD_SIZE)
                {
                    format = true;
                }
                else
                {
                    if (data[0] != 'M' || data[1] != 'C' || data[0x7f] != 0xe)
                    {
                        format = true;
                    }
                }

                if (format)
                {
                    // format memcard and write to disk
                    platform_set_file_size(&g_psx.memcard, MEMCARD_SIZE);

                    psx_format_memcard(data);

                    platform_write_file(&g_psx.memcard, 0, data, MEMCARD_SIZE);
                }

                g_psx.arena.used = temp;
            }
        }
    }

    return result;
}

b8 psx_load_bios(const char *path)
{
    if (g_bios != NULL)
    {
        free(g_bios);
        g_bios = NULL;
    }

    struct file_dat file = {0};
    if (allocate_and_read_file(path, false, &file))
    {
        g_bios = file.memory;
        return true;
    }

    return false;
}

void psx_init(void)
{
    g_psx.arena = allocate_arena(MEGABYTES(8));
}

void psx_reset(void)
{
    struct memory_arena *arena = &g_psx.arena;
    clear_arena(arena);

    g_ram = push_arena(arena, MEGABYTES(2));
    g_scratch = push_arena(arena, KILOBYTES(1));

    // initialize ram with known garbage debug value
    memset32(g_ram, 0xdeadbeef, RAM_SIZE / 4);

    scheduler_reset(arena);
    cpu_reset();
    gpu_reset(arena);
    dma_reset();
    spu_reset(arena);
    cdrom_reset();
    sio_reset();
    counters_reset();
}

void psx_shutdown(void)
{
    platform_close_file(&g_psx.memcard);
    if (g_psx.disk)
        close_disk(g_psx.disk);
    free_arena(&g_psx.arena);
}

b8 psx_can_boot(void)
{
    return (g_bios != NULL); // TODO: validate bios
}

b32 psx_run(void)
{
    b32 result = execute_instructions();
    tick_events();
    return result;
}

void psx_step(void)
{
    b8 value = g_debug.breakpoints_enabled; // currently disabling breakpoints to prevent pausing on them while stepping
    g_debug.breakpoints_enabled = false;
    g_target_cycles = g_cycles_elapsed + 1;
    execute_instructions();
    tick_events();
    g_debug.breakpoints_enabled = value;
}
