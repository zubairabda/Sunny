#include "input.h"

static inline b8 is_key_down(u8 *keys, u8 key)
{
    return keys[key];
}

void keyboard_get_digital_pad_input(struct input_device_base *keyboard)
{
    struct keyboard_pad *kbd = (struct keyboard_pad *)keyboard;
    u16 buttons = 0;

    if (kbd->keystates['I'])
        buttons |= PAD_TRIANGLE;
    if (kbd->keystates['J'])
        buttons |= PAD_CIRCLE;
    if (kbd->keystates['K'])
        buttons |= PAD_CROSS;
    if (kbd->keystates['L'])
        buttons |= PAD_SQUARE;
    if (kbd->keystates['W'])
        buttons |= PAD_UP;
    if (kbd->keystates['A'])
        buttons |= PAD_RIGHT;
    if (kbd->keystates['S'])
        buttons |= PAD_DOWN;
    if (kbd->keystates['D'])
        buttons |= PAD_RIGHT;

    buttons = ~buttons;
    kbd->base.data[0] = (u8)buttons;
    kbd->base.data[1] = (u8)(buttons >> 8);
}
