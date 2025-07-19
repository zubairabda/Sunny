#ifndef RENDERER_H
#define RENDERER_H

#include "common.h"
#include "sy_math.h"
#include "gpu_common.h"

#define VRAM_WIDTH 1024
#define VRAM_HEIGHT 512
#define VRAM_SIZE (VRAM_WIDTH * VRAM_HEIGHT * 2)

typedef struct renderer_context
{
    b8 is_initialized;
    void (*handle_resize)(u32 new_width, u32 new_height);
    void (*update_display)(void);
    void (*present)(void);
    void (*shutdown)(void);
} renderer_context;

typedef struct software_renderer
{
    renderer_context base;
    void *vram;
} software_renderer;

extern renderer_context *g_renderer;
extern u64 g_vblank_counter;

#endif /* RENDERER_H */
