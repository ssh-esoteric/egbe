// SPDX-License-Identifier: GPL-3.0-or-later
#include "egbe.h"
#include "common.h"

void egbe_gameboy_debug(struct egbe_gameboy *self)
{
	GBLOG("Debugger not configured at compile time");
}

int main(int argc, char **argv)
{
	return egbe_main(argc, argv);
}
