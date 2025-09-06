#ifndef PSX_H
#define PSX_H

#include "stream.h"
#include "sio.h"

typedef enum
{
    SYSTEM_STATE_STOPPED = 0,
    SYSTEM_STATE_PAUSED,
    SYSTEM_STATE_RUNNING
} system_state;

typedef struct
{
    system_state state;
    disk_image *disk;
    struct input_device_base *controllers[2];
} psx_state;

extern psx_state g_psx;

b8 psx_load_image(const char *path);
b8 psx_load_bios(const char *path);
b8 psx_can_boot(void);
void psx_add_controller(u32 port, input_device_type type, void (*fp_read_data)(struct input_device_base *));
void psx_remove_controller(u32 port);

void psx_init(void);
void psx_reset(void);
void psx_shutdown(void);
b32 psx_run(void);
void psx_step(void);

#endif /* PSX_H */
