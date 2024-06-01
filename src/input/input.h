#ifndef INPUT_H
#define INPUT_H

#include "common.h"
#include "pad.h"

struct input_state
{
    union digital_pad_switches pad;
    u8 keystates[128];
};

//input_plugin *init_input(void);
//void deinit_input(input_plugin *input);

b8 input_is_key_down(char key);
//b8 input_is_button_down(u16 mask);

#endif
