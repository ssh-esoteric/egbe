// SPDX-License-Identifier: GPL-3.0-or-later
#include "egbe.h"
#include "common.h"

int egbe_gameboy_init_lws(struct egbe_gameboy *self, char *api_url)
{
	GBLOG("libwebsockets client not configured at compile time");

	return 0;
}
