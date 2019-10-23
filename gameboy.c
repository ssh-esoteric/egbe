#include "common.h"

struct gameboy *gameboy_alloc(enum gameboy_system system)
{
	struct gameboy *gb = calloc(1, sizeof(*gb));
	if (!gb) {
		GBLOG("Failed to allocate GB: %m");
		return NULL;
	}

	gb->system = system;

	return gb;
}

void gameboy_free(struct gameboy *gb)
{
	free(gb);
}
