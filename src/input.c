#include "input.h"

u8 g_keystates[256];

void keyboard_get_digital_pad_input(struct input_device_base *device)
{
    u16 buttons = 0;

    if (g_keystates['I'])
        buttons |= PAD_TRIANGLE;
    if (g_keystates['J'])
        buttons |= PAD_SQUARE;
    if (g_keystates['K'])
        buttons |= PAD_CROSS;
    if (g_keystates['L'])
        buttons |= PAD_CIRCLE;
    if (g_keystates['W'])
        buttons |= PAD_UP;
    if (g_keystates['A'])
        buttons |= PAD_LEFT;
    if (g_keystates['S'])
        buttons |= PAD_DOWN;
    if (g_keystates['D'])
        buttons |= PAD_RIGHT;
    if (g_keystates['Q'])
        buttons |= PAD_L1;
    if (g_keystates['E'])
        buttons |= PAD_L2;
    if (g_keystates['U'])
        buttons |= PAD_R2;
    if (g_keystates['O'])
        buttons |= PAD_R1;
    if (g_keystates['F'])
        buttons |= PAD_SELECT;
    if (g_keystates['H'])
        buttons |= PAD_START;

    buttons = ~buttons;
    device->data[0] = (u8)buttons;
    device->data[1] = (u8)(buttons >> 8);
}
