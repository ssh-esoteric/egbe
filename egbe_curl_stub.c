// SPDX-License-Identifier: GPL-3.0-or-later
#include "egbe.h"
#include "common.h"

int egbe_gameboy_init_curl(struct egbe_gameboy *self, char *api_url)
{
	GBLOG("Curl client not configured at compile time");

	return 0;
}
