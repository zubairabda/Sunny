#ifndef INPUT_H
#define INPUT_H

#include "common.h"
#include "sio.h"

struct keyboard_pad
{
    struct input_device_base base;
    u8 keystates[128];
};

void keyboard_get_digital_pad_input(struct input_device_base *keyboard);

#endif /* INPUT_H */
