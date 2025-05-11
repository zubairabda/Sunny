#ifndef DISASM_H
#define DISASM_H

#include "cpu.h"

const char *instr_to_string(u32 op, char *buffer, u32 len);

#endif /* DISASM_H */
