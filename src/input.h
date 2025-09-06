#ifndef INPUT_H
#define INPUT_H

#include "common.h"
#include "sio.h"

extern u8 g_keystates[256];

void keyboard_get_digital_pad_input(struct input_device_base *device);

#endif /* INPUT_H */
