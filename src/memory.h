#ifndef MEMORY_H
#define MEMORY_H

#include "common.h"

extern u8 *g_bios;
extern u8 *g_ram;
extern u8 *g_scratch;
extern u8 *g_peripheral;

void *mem_read(u32 addr);
void *mem_write(u32 addr);

u32 load32(u32 vaddr);
u16 load16(u32 vaddr);
u8 load8(u32 vaddr);

void store32(u32 vaddr, u32 value);
void store16(u32 vaddr, u32 value);
void store8(u32 vaddr, u32 value);

#endif /* MEMORY_H */
