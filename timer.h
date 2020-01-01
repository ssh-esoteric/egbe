// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef EGBE_TIMER_H
#define EGBE_TIMER_H

#include "gameboy.h"

void timer_set_frequency(struct gameboy *gb, uint8_t val);
void timer_sync(struct gameboy *gb);

#endif
