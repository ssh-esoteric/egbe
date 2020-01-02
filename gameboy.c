// SPDX-License-Identifier: GPL-3.0-or-later
#include "apu.h"
#include "cpu.h"
#include "lcd.h"
#include "mmu.h"
#include "common.h"

void gb_callback(struct gameboy *gb, struct gameboy_callback *cb)
{
	if (cb->callback)
		cb->callback(gb, cb->context);
}

struct gameboy *gameboy_alloc(enum gameboy_system system)
{
	struct gameboy *gb = calloc(1, sizeof(*gb));
	if (!gb) {
		GBLOG("Failed to allocate GB: %m");
		return NULL;
	}

	gb->system = system;
	gb->gbc = gb->system >= GAMEBOY_SYSTEM_GBC;
	gb->gdma = true;

	if (gb->gbc) {
		gb->wram_banks = 8;
	} else {
		gb->wram_banks = 2;
	}
	gb->wram_size = gb->wram_banks * sizeof(gb->wram[0]);

	gb->wram = malloc(gb->wram_size);
	if (!gb->wram) {
		GBLOG("Failed to allocate WRAM: %m");
		gameboy_free(gb);
		return NULL;
	}
	gb->wram_bank = 1;
	gb->wramx = gb->wram[gb->wram_bank];

	gb->cpu_status = GAMEBOY_CPU_CRASHED;
	gb->cycles = 0;

	apu_init(gb);
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
	gb->sram_enabled = false;
	gb->timer_enabled = false;

	gameboy_update_joypad(gb, NULL);

	lcd_init(gb);
}

// Note: Bits of P1 are _unset_ when the corresponding button is pressed
void gameboy_update_joypad(struct gameboy *gb, struct gameboy_joypad *jp)
{
	if (!jp) {
		struct gameboy_joypad clear = { 0 };
		gameboy_update_joypad(gb, &clear);
		return;
	}

	uint8_t old_arrows = gb->p1_arrows;
	uint8_t old_buttons = gb->p1_buttons;

	// The GB physically prevents left+right or up+down from being pressed
	// simultaneously.  For now, just unpress them both if so.
	//
	// TODO: Make this configurable; apparently allowing this can be good
	//       for certain tool-assisted speedruns.
	gb->p1_arrows = 0xDF;
	if (jp->right != jp->left)
		gb->p1_arrows &= ~(jp->right ? BIT(0) : BIT(1));
	if (jp->up != jp->down)
		gb->p1_arrows &= ~(jp->up ? BIT(2) : BIT(3));

	gb->p1_buttons = 0xEF
	               & ~(jp->a      ? BIT(0) : 0)
	               & ~(jp->b      ? BIT(1) : 0)
	               & ~(jp->select ? BIT(2) : 0)
	               & ~(jp->start  ? BIT(3) : 0);

	// Check for any unpressed -> pressed transitions
	bool check_arrows = (old_arrows ^ gb->p1_arrows) & gb->p1_arrows;
	bool check_buttons = (old_buttons ^ gb->p1_buttons) & gb->p1_buttons;

	if (check_arrows || check_buttons) {
		irq_flag(gb, GAMEBOY_IRQ_JOYPAD);

		if (gb->cpu_status == GAMEBOY_CPU_STOPPED)
			gb->cpu_status = GAMEBOY_CPU_RUNNING;
	}
}
