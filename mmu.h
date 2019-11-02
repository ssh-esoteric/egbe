#ifndef EGBE_MMU_H
#define EGBE_MMU_H

#include "gameboy.h"

uint8_t mmu_read(struct gameboy *gb, uint16_t addr);
void mmu_write(struct gameboy *gb, uint16_t addr, uint8_t val);

#endif
