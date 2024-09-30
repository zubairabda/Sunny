#ifndef INPUT_H
#define INPUT_H

#include "common.h"
#include "sio.h"

struct input_state
{
    u8 keystates[128];
};

//input_plugin *init_input(void);
//void deinit_input(input_plugin *input);

inline b8 input_is_key_down(struct input_state *input, unsigned char key)
{
    return input->keystates[key];
}
//b8 input_is_button_down(u16 mask);

#endif /* INPUT_H */
