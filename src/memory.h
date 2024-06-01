#ifndef MEMORY_H
#define MEMORY_H

#include "psx.h"

void *mem_read(struct psx_state *psx, u32 addr);
void *mem_write(struct psx_state *psx, u32 addr);

u32 load32(struct psx_state* psx, u32 vaddr);
u16 load16(struct psx_state* psx, u32 vaddr);
u8 load8(struct psx_state* psx, u32 vaddr);

void store32(struct psx_state* psx, u32 vaddr, u32 value);
void store16(struct psx_state* psx, u32 vaddr, u32 value);
void store8(struct psx_state* psx, u32 vaddr, u32 value);

#endif
