#include "savestate.h"
#include "psx.h"
#include "cpu.h"
#include "gpu.h"
#include "spu.h"
#include "cdrom.h"
#include "mdec.h"
#include "dma.h"
#include "counters.h"

struct savestate_header
{
    u8 magic[8];
    u32 version;
};

#define SAVESTATE_VERSION 1

static void savestate_add(FILE *save, void *data, size_t size)
{
    fwrite(data, size, 1, save);
}

static void savestate_load(FILE *save, void *buffer, size_t size)
{
    fread(buffer, size, 1, save);
}

void save_system_state(void)
{
    FILE *save = fopen("savestate", "wb");

    struct savestate_header header;
    header.magic[0] = 's';
    header.magic[1] = 'u';
    header.magic[2] = 'n';
    header.magic[3] = 'n';
    header.magic[4] = 'y';
    header.magic[5] = 'p';
    header.magic[6] = 's';
    header.magic[7] = 'x';
    header.version = SAVESTATE_VERSION;

    savestate_add(save, &header, sizeof(struct savestate_header));

    software_renderer *renderer = (software_renderer *)g_renderer;
    u16 *vram = renderer->vram;

    u32 path_len = safe_truncate32(strlen(g_psx.image_path));
    savestate_add(save, &path_len, sizeof(path_len));
    savestate_add(save, g_psx.image_path, path_len);

    savestate_add(save, g_psx.arena.base, g_psx.arena.size);
    savestate_add(save, &g_cpu, sizeof(struct cpu_state));
    savestate_add(save, &g_gpu, sizeof(struct gpu_state));
    savestate_add(save, vram, VRAM_SIZE);
    savestate_add(save, &g_spu, sizeof(struct spu_state));
    savestate_add(save, &g_cdrom, sizeof(struct cdrom_context));
    savestate_add(save, &g_dma, sizeof(struct dma_state));
    savestate_add(save, &g_mdec, sizeof(struct mdec_state));
    savestate_add(save, &g_counters[0], sizeof(struct root_counter) * 3);
    savestate_add(save, &g_sio, sizeof(struct sio_context));

    savestate_add(save, &g_cycles_elapsed, sizeof(g_cycles_elapsed));
    savestate_add(save, &g_target_cycles, sizeof(g_target_cycles));
    savestate_add(save, &g_scheduler.id_count, sizeof(g_scheduler.id_count));
    savestate_add(save, &g_scheduler.event_count, sizeof(g_scheduler.event_count));
    
    struct event_list_node *current = g_scheduler.sentinel->next;
    while (current != g_scheduler.sentinel)
    {
        savestate_add(save, &current->data, sizeof(tick_event));
        current = current->next;
    }

    fclose(save);
}

b8 load_system_state(void)
{
    FILE *save = fopen("savestate", "rb");

    struct savestate_header header;
    fread(&header, sizeof(struct savestate_header), 1, save);

    if (memcmp(header.magic, "sunnypsx", 8))
        goto error;

    if (header.version != SAVESTATE_VERSION)
        goto error;

    //psx_reset();

    software_renderer *renderer = (software_renderer *)g_renderer;
    u16 *vram = renderer->vram;

    u32 path_len;
    savestate_load(save, &path_len, sizeof(path_len));
    savestate_load(save, g_psx.image_path, path_len);

    psx_image_type type = psx_get_image_type_from_path(g_psx.image_path);
    psx_load_image(g_psx.image_path, type);

    savestate_load(save, g_psx.arena.base, g_psx.arena.size);
    savestate_load(save, &g_cpu, sizeof(struct cpu_state));
    savestate_load(save, &g_gpu, sizeof(struct gpu_state));
    savestate_load(save, vram, VRAM_SIZE);
    savestate_load(save, &g_spu, sizeof(struct spu_state));
    savestate_load(save, &g_cdrom, sizeof(struct cdrom_context));
    savestate_load(save, &g_dma, sizeof(struct dma_state));
    savestate_load(save, &g_mdec, sizeof(struct mdec_state));
    savestate_load(save, &g_counters[0], sizeof(struct root_counter) * 3);
    savestate_load(save, &g_sio, sizeof(struct sio_context));

    clear_events();

    savestate_load(save, &g_cycles_elapsed, sizeof(g_cycles_elapsed));
    savestate_load(save, &g_target_cycles, sizeof(g_target_cycles));
    savestate_load(save, &g_scheduler.id_count, sizeof(g_scheduler.id_count));
    savestate_load(save, &g_scheduler.event_count, sizeof(g_scheduler.event_count));
    
    for (u32 i = 0; i < g_scheduler.event_count; ++i)
    {
        struct event_list_node *event = pool_alloc(&g_scheduler.event_pool);
        savestate_load(save, &event->data, sizeof(tick_event));
        insert_event(event);
    }

    fclose(save);

    return true;

error:
    fclose(save);
    return false;
}