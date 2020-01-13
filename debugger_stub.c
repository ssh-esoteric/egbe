// SPDX-License-Identifier: GPL-3.0-or-later
#include "debugger.h"
#include "common.h"

int debugger_callback(int (*callback)(void *context), void *context)
{
	return callback(context);
}

void debugger_open(struct gameboy *gb)
{
	GBLOG("Debugger not configured at compile time");
}
