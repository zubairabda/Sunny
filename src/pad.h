#ifndef PAD_H
#define PAD_H

#include "psx.h"

union digital_pad_switches
{
    struct
    {
        u16 select : 1;
        u16 l3 : 1;
        u16 r3 : 1;
        u16 start : 1;
        u16 joypad_up : 1;
        u16 joypad_right : 1;
        u16 joypad_down : 1;
        u16 joypad_left : 1;
        u16 l2 : 1;
        u16 r2 : 1;
        u16 l1 : 1;
        u16 r1 : 1;
        u16 triangle : 1;
        u16 circle : 1;
        u16 cross : 1;
        u16 square : 1;
    };
    u16 value;
};

enum joypad_context
{
    JOYPAD_CONTEXT_NONE = 0,
    JOYPAD_CONTEXT_READ
};

typedef union
{
    struct
    {
        u32 tx_started : 1;
        u32 rx_fifo_not_empty : 1;
        u32 tx_finished : 1;
        u32 rx_parity_error : 1;
        u32 : 3;
        u32 ack_input_level : 1;
        u32 : 1;
        u32 irq : 1;
        u32 : 1;
        u32 timer : 21; 
    };
    u32 value;
} JOY_STAT;

struct digital_pad
{
    union digital_pad_switches buttons;
    u32 command_index;
    enum joypad_context context;
};

struct joypad_state
{
    union digital_pad_switches buttons;
    u32 command_index;
    enum joypad_context context;
    JOY_STAT stat;
    u16 mode;
    u16 control;
    u16 baud_reload; // not sure if this needs a default value
    u8 bytes_left_until_irq;
    u8 tx_buffer;
    u8 tx_buffer_full;
    u8 rx_buffer;
    u8 rx_buffer_full;
    u64 cycles_at_baud_store;
};

u16 joypad_read(struct joypad_state *pad, u32 offset);
void joypad_store(struct psx_state *psx, u32 offset, u16 value);


#endif
