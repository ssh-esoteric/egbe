#include "common.h"
#include "lcd.h"
#include "mmu.h"

struct gameboy *gameboy_alloc(enum gameboy_system system)
{
	struct gameboy *gb = calloc(1, sizeof(*gb));
	if (!gb) {
		GBLOG("Failed to allocate GB: %m");
		return NULL;
	}

	gb->system = system;
	if (gb->system >= GAMEBOY_SYSTEM_GBC) {
		gb->wram_size = 8 * sizeof(gb->wram[0]);
	} else {
		gb->wram_size = 2 * sizeof(gb->wram[0]);
	}

	gb->wram = malloc(gb->wram_size);
	if (!gb->wram) {
		GBLOG("Failed to allocate WRAM: %m");
		return NULL;
	}
	gb->wram_bank = gb->wram[1];

	gb->cpu_status = GAMEBOY_CPU_CRASHED;
	gb->cycles = 0;

	lcd_init(gb);

	return gb;
}

void gameboy_free(struct gameboy *gb)
{
	gameboy_remove_boot_rom(gb);
	gameboy_remove_cartridge(gb);

	free(gb->wram);
	free(gb);
}

void gameboy_restart(struct gameboy *gb)
{
	gb->boot_enabled = !!gb->boot;
	if (gb->boot_enabled) {
		gb->pc = 0x0000;
		gb->sp = 0x0000;
		gb->af = 0x0000;
		gb->bc = 0x0000;
		gb->de = 0x0000;
		gb->hl = 0x0000;
	} else {
		// TODO: These values are DMG-specific
		gb->pc = 0x0100;
		gb->sp = 0xFFFE;
		gb->af = 0x01B0;
		gb->bc = 0x0013;
		gb->de = 0x00D8;
		gb->hl = 0x014D;
	}

	gb->cpu_status = GAMEBOY_CPU_RUNNING;
	gb->cycles = 0;
	gb->div = 0;
	gb->next_div_in = 256;
	gb->timer_enabled = false;

	lcd_init(gb);
}
