#include "common.h"

int main(int argc, char **argv)
{
	if (argc < 2) {
		puts("Usage: egbe <ROM.gb> [<BOOT.bin>]");
		return 0;
	}

	struct gameboy *gb = gameboy_alloc(GAMEBOY_SYSTEM_DMG);
	if (!gb)
		return 1;

	gameboy_free(gb);

	return 0;
}
