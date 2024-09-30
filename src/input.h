#ifndef INPUT_H
#define INPUT_H

#include "common.h"
#include "sio.h"

struct keyboard_pad
{
    struct input_device_base base;
    u8 keystates[128];
};

//input_plugin *init_input(void);
//void deinit_input(input_plugin *input);

void keyboard_get_digital_pad_input(struct input_device_base *keyboard);
void keyboard_get_analog_pad_input(struct input_device_base *keyboard);

//b8 input_is_button_down(u16 mask);

#endif /* INPUT_H */
