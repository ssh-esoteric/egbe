#include "common.h"
#include "lcd.h"
#include "mmu.h"

static inline bool is_oam_accessible(struct gameboy *gb)
{
	return gb->lcd_status <= GAMEBOY_LCD_VBLANK;
}

static inline bool is_vram_accessible(struct gameboy *gb)
{
	return gb->lcd_status <= GAMEBOY_LCD_OAM_SEARCH;
}

uint8_t mmu_read(struct gameboy *gb, uint16_t addr)
{
	switch (addr) {
	case 0x0000 ... 0x00FF:
		if (gb->boot_enabled)
			return gb->boot[addr];
		; // fallthrough
	case 0x0100 ... 0x3FFF:
		if (gb->rom)
			return gb->rom[0][addr];
		break;

	case 0x4000 ... 0x7FFF:
		if (gb->rom)
			return gb->rom_bank[addr % 0x4000];
		break;

	case 0x8000 ... 0x9FFF:
		break; // TODO: VRAM

	case 0xA000 ... 0xBFFF:
		if (gb->sram)
			return gb->sram_bank[addr % 0x2000 % gb->sram_size];
		break;

	case 0xC000 ... 0xCFFF:
		return gb->wram[0][addr % 0x1000];
	case 0xD000 ... 0xDFFF:
		return gb->wram_bank[addr % 0x1000];

	case 0xE000 ... 0xFDFF:
		GBLOG("Bad read from ECHO RAM: %04X", addr);
		gb->cpu_status = GAMEBOY_CPU_CRASHED;
		break;

	case 0xFE00 ... 0xFE9F:
		break; // TODO: OAM

	case 0xFF80 ... 0xFFFE:
		return gb->hram[addr % 0x0080];

	case GAMEBOY_ADDR_LY:
		return gb->scanline;
	}

	return 0xFF; // "Undefined" read
}

void mmu_write(struct gameboy *gb, uint16_t addr, uint8_t val)
{
	switch (addr) {
	case 0x0000 ... 0x7FFF:
		// TODO: Cartridges with an MBC can intercept these reads
		break;

	case 0x8000 ... 0x97FF:
		if (is_vram_accessible(gb))
			lcd_update_tile(gb, addr % 0x2000, val);
		break;
	case 0x9800 ... 0x9FFF:
		if (is_vram_accessible(gb))
			lcd_update_tilemap(gb, addr % 0x0800, val);
		break;

	case 0xA000 ... 0xBFFF:
		if (gb->sram)
			gb->sram_bank[addr % 0x2000 % gb->sram_size] = val;
		break;

	case 0xC000 ... 0xCFFF:
		gb->wram[0][addr % 0x1000] = val;
		break;
	case 0xD000 ... 0xDFFF:
		gb->wram_bank[addr % 0x1000] = val;
		break;

	case 0xE000 ... 0xFDFF:
		GBLOG("Bad write to ECHO RAM: %02X => %04X", val, addr);
		gb->cpu_status = GAMEBOY_CPU_CRASHED;
		break;

	case 0xFE00 ... 0xFE9F:
		// TODO: OAM
		break;

	case 0xFF80 ... 0xFFFE:
		gb->hram[addr % 0x0080] = val;
		break;

	case GAMEBOY_ADDR_LCDC:
		if (val & 0x80)
			lcd_enable(gb);
		else
			lcd_disable(gb);
		break;

	case GAMEBOY_ADDR_BGP:
		gb->bgp = val;
		break;

	case GAMEBOY_ADDR_BOOT_SWITCH:
		if (val != 0x01) {
			GBLOG("Bad write to boot ROM switch: %02X", val);
			gb->cpu_status = GAMEBOY_CPU_CRASHED;
		} else if (!gb->boot_enabled) {
			GBLOG("Boot ROM already disabled");
			gb->cpu_status = GAMEBOY_CPU_CRASHED;
		} else {
			GBLOG("Out of boot ROM!\n"
			      "\tPC: %04X\n"
			      "\tSP: %04X\n"
			      "\tAF: %04X (%c%c%c%c)\n"
			      "\tBC: %04X\n"
			      "\tDE: %04X\n"
			      "\tHL: %04X",
			      gb->pc,
			      gb->sp,
			      gb->af,
			      (gb->carry     ? 'C' : '.'),
			      (gb->halfcarry ? 'H' : '.'),
			      (gb->subtract  ? 'N' : '.'),
			      (gb->zero      ? 'Z' : '.'),
			      gb->bc,
			      gb->de,
			      gb->hl);
			gb->boot_enabled = false;
		}
		break;
	}
}
