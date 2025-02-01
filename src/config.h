#ifndef CONFIG_H
#define CONFIG_H

#include "common.h"

#define MAX_CONFIG_PATH 512

struct controller_config
{
    int type;

};

struct psx_config
{
    char bios_path[MAX_CONFIG_PATH];
    char boot_file[MAX_CONFIG_PATH];
    struct controller_config controls[2];
    b8 software_rendering;
};

extern struct psx_config g_config;

b8 load_config(void);
b8 save_config(void);

#endif /* CONFIG_H */
