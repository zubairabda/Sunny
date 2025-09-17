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

b8 psx_load_image(const char *path)
{
    psx_image_type type;
    if (string_ends_with_ignore_case(path, ".exe"))
    {
        platform_file exe;
        if (!platform_open_file(path, &exe))
            return false;
        psx_reset();
        b8 result = psx_load_exe(&exe);
        platform_close_file(&exe);
        return result;
    }
    else if (string_ends_with_ignore_case(path, ".bin"))
    {
        type = BIN;
    }
    else if (string_ends_with_ignore_case(path, ".cue"))
    {
        type = CUE;
    }
    else
    {
        printf("Unrecognized file extension for file: %s\n", path);
        return false;
    }

    disk_image *disk = open_disk(path, type);
    if (disk)
    {
        if (g_psx.disk != NULL)
            close_disk(g_psx.disk);
        strncpy(g_psx.disk_path, path, 4096);
        g_psx.disk = disk;
        psx_reset();
        return true;
    }
    else
    {
        return false;
    }
}

b8 psx_load_bios(const char *path)
{
    if (g_bios != NULL)
    {
        free(g_bios);
        g_bios = NULL;
    }

    struct file_dat file = {0};
    if (allocate_and_read_file(path, 0, &file))
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
