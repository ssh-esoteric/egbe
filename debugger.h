// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef EGBE_DEBUGGER_H
#define EGBE_DEBUGGER_H

#include "gameboy.h"

int debugger_callback(int (*callback)(void *context), void *context);
void debugger_open(struct gameboy *gb);

#endif
