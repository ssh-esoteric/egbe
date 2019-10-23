#ifndef EGBE_GAMEBOY_H
#define EGBE_GAMEBOY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum gameboy_system {
	GAMEBOY_SYSTEM_DMG,
	GAMEBOY_SYSTEM_GBP,
	GAMEBOY_SYSTEM_SGB,
	GAMEBOY_SYSTEM_GBC,
	GAMEBOY_SYSTEM_SGB2,
};

struct gameboy {
	enum gameboy_system system;
};

struct gameboy *gameboy_alloc(enum gameboy_system system);
void gameboy_free(struct gameboy *gb);

#endif
