#ifndef DISASM_H
#define DISASM_H

#include "cpu.h"

static const char *register_names[32] = 
{
    "r0",
    "at",
    "v0",
    "v1",
    "a0",
    "a1",
    "a2",
    "a3",
    "t0",
    "t1",
    "t2",
    "t3",
    "t4",
    "t5",
    "t6",
    "t7",
    "s0",
    "s1",
    "s2",
    "s3",
    "s4",
    "s5",
    "s6",
    "s7",
    "t8",
    "t9",
    "k0",
    "k1",
    "gp",
    "sp",
    "fp",
    "ra"
};

const char *instr_to_string(u32 op, char *buffer, u32 len);

#endif /* DISASM_H */
