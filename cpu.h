// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef EGBE_CPU_H
#define EGBE_CPU_H

#include "gameboy.h"

void irq_flag(struct gameboy *gb, enum gameboy_irq irq);

#endif
