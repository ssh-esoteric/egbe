#include "common.h"
#include "lcd.h"
#include "mmu.h"
#include "timer.h"
#include <assert.h>

static inline bool is_oam_accessible(struct gameboy *gb)
{
	return gb->lcd_status <= GAMEBOY_LCD_VBLANK;
}

static inline bool is_vram_accessible(struct gameboy *gb)
{
	return gb->lcd_status <= GAMEBOY_LCD_OAM_SEARCH;
}

static void mbc1_write(struct gameboy *gb, uint16_t addr, uint8_t val)
{
	union {
		struct {
			uint8_t lo:5;
			uint8_t hi:2;
			uint8_t _sram_mode:1;
		};
		struct {
			uint8_t all:7;
			uint8_t _rom_mode:1;
		};
	} bank;
	static_assert(sizeof(bank) == 1, "Unexpected MBC1 bank struct");

	if (gb->mbc1_sram_mode) {
		bank.lo = gb->rom_bank;
		bank.hi = gb->sram_bank;
	} else {
		bank.all = gb->rom_bank;
	}

	switch (addr) {
	case 0x0000 ... 0x1FFF:
		gb->sram_enabled = gb->sram && (val & 0x0F) == 0x0A;
		break;

	case 0x2000 ... 0x3FFF:
		bank.lo = (val & 0x1F) ?: 1;
		break;

	case 0x4000 ... 0x5FFF:
		bank.hi = (val & 0x03);
		break;

	case 0x6000 ... 0x7FFF:
		gb->mbc1_sram_mode = (val & 0x01);
		break;
	}

	if (gb->mbc1_sram_mode) {
		gb->rom_bank = bank.lo;
		gb->sram_bank = bank.hi;
	} else {
		gb->rom_bank = bank.all;
		gb->sram_bank = 0;
	}

	gb->romx = gb->rom[gb->rom_bank];
	gb->sramx = gb->sram[gb->sram_bank];
}

static void mbc3_write(struct gameboy *gb, uint16_t addr, uint8_t val)
{
	switch (addr) {
	case 0x0000 ... 0x1FFF:
		gb->sram_enabled = gb->sram && (val & 0x0F) == 0x0A;
		break;

	case 0x2000 ... 0x3FFF:
		gb->rom_bank = (val & 0x7F) ?: 1;
		break;

	case 0x4000 ... 0x5FFF:
		// TODO: RTC registers if mapped
		gb->sram_bank = val % gb->sram_banks;
		break;

	case 0x6000 ... 0x7FFF:
		// TODO: Latch RTC
		break;
	}

	gb->romx = gb->rom[gb->rom_bank];
	gb->sramx = gb->sram[gb->sram_bank];
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
			return gb->romx[addr % 0x4000];
		break;

	case 0x8000 ... 0x97FF:
		if (is_vram_accessible(gb))
			return gb->tiles[(addr % 0x2000) / 16].raw[addr % 16];
		break;
	case 0x9800 ... 0x9FFF:
		if (is_vram_accessible(gb))
			return gb->tilemap_raw[addr % 0x0800];
		break;

	case 0xA000 ... 0xBFFF:
		if (gb->sram_enabled)
			return gb->sramx[addr % 0x2000 % gb->sram_size];
		break;

	case 0xC000 ... 0xCFFF:
		return gb->wram[0][addr % 0x1000];
	case 0xD000 ... 0xDFFF:
		return gb->wramx[addr % 0x1000];

	case 0xE000 ... 0xFDFF:
		GBLOG("Bad read from ECHO RAM: %04X", addr);
		gb->cpu_status = GAMEBOY_CPU_CRASHED;
		break;

	case 0xFE00 ... 0xFE9F:
		break; // TODO: OAM

	case 0xFF80 ... 0xFFFE:
		return gb->hram[addr % 0x0080];

	case GAMEBOY_ADDR_IE:
		return gb->irq_enabled;

	case GAMEBOY_ADDR_IF:
		return gb->irq_flagged | 0xE0;

	case GAMEBOY_ADDR_P1:
		if (gb->joypad_status == GAMEBOY_JOYPAD_ARROWS)
			return gb->p1_arrows;
		else
			return gb->p1_buttons;

	case GAMEBOY_ADDR_DIV:
		return (gb->cycles >> 8) & 0xFF;

	case GAMEBOY_ADDR_TIMA:
		return gb->timer_counter;

	case GAMEBOY_ADDR_TMA:
		return gb->timer_modulo;

	case GAMEBOY_ADDR_TAC:
		return (gb->timer_frequency_code & 0x03)
		     | 0xF8 // TODO: Do the unused bits return 0 or 1?
		     | (gb->timer_enabled ? BIT(2) : 0);

	case GAMEBOY_ADDR_LCDC:
		return (gb->background_enabled ? BIT(0) : 0)
		     | (gb->sprites_enabled ? BIT(1) : 0)
		     | (gb->sprite_size == 16 ? BIT(2) : 0)
		     | (gb->background_tilemap == gb->tilemap[1] ? BIT(3) : 0)
		     | (gb->tilemap_signed ? 0 : BIT(4))
		     | (gb->window_enabled ? BIT(5) : 0)
		     | (gb->window_tilemap == gb->tilemap[1] ? BIT(6) : 0)
		     | (gb->lcd_enabled ? BIT(7) : 0);

	case GAMEBOY_ADDR_STAT:
		return gb->lcd_enabled ? gb->lcd_status : 0
		     | (gb->scanline == gb->scanline_compare) ? BIT(2) : 0
		     | gb->stat_on_hblank ? BIT(3) : 0
		     | gb->stat_on_vblank ? BIT(4) : 0
		     | gb->stat_on_oam_search ? BIT(5) : 0
		     | gb->stat_on_scanline ? BIT(6) : 0
		     | BIT(7);

	case GAMEBOY_ADDR_LY:
		return gb->scanline;

	case GAMEBOY_ADDR_LYC:
		return gb->scanline_compare;

	case GAMEBOY_ADDR_SCY:
		return gb->sy;

	case GAMEBOY_ADDR_SCX:
		return gb->sx;

	case GAMEBOY_ADDR_WY:
		return gb->wy;

	case GAMEBOY_ADDR_WX:
		return gb->wx + 7;

	case GAMEBOY_ADDR_BGP:
		return gb->bgp;

	case GAMEBOY_ADDR_OBP0:
		return gb->obp[0];

	case GAMEBOY_ADDR_OBP1:
		return gb->obp[1];
	}

	return 0xFF; // "Undefined" read
}

void mmu_write(struct gameboy *gb, uint16_t addr, uint8_t val)
{
	switch (addr) {
	case 0x0000 ... 0x7FFF:
		switch (gb->mbc) {
		case GAMEBOY_MBC_NONE:
			break;
		case GAMEBOY_MBC_MBC1:
			mbc1_write(gb, addr, val);
			break;
		case GAMEBOY_MBC_MBC3:
			mbc3_write(gb, addr, val);
			break;
		case GAMEBOY_MBC_MBC2:
		case GAMEBOY_MBC_MMM01:
		case GAMEBOY_MBC_MBC5:
		case GAMEBOY_MBC_MBC6:
		case GAMEBOY_MBC_MBC7:
		case GAMEBOY_MBC_HUC1:
		case GAMEBOY_MBC_HUC3:
		case GAMEBOY_MBC_TAMA5:
		case GAMEBOY_MBC_CAMERA:
			GBLOG("MBC $%d not yet implemented", gb->mbc);
			gb->cpu_status = GAMEBOY_CPU_CRASHED;
			break;
		}
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
		if (gb->sram_enabled)
			gb->sramx[addr % 0x2000 % gb->sram_size] = val;
		break;

	case 0xC000 ... 0xCFFF:
		gb->wram[0][addr % 0x1000] = val;
		break;
	case 0xD000 ... 0xDFFF:
		gb->wramx[addr % 0x1000] = val;
		break;

	case 0xE000 ... 0xFDFF:
		GBLOG("Bad write to ECHO RAM: %02X => %04X", val, addr);
		gb->cpu_status = GAMEBOY_CPU_CRASHED;
		break;

	case 0xFE00 ... 0xFE9F:
		if (is_oam_accessible(gb))
			lcd_update_sprite(gb, addr % 0x0100, val);
		break;

	case 0xFF80 ... 0xFFFE:
		gb->hram[addr % 0x0080] = val;
		break;

	case GAMEBOY_ADDR_IE:
		gb->irq_enabled = val;
		break;

	case GAMEBOY_ADDR_IF:
		gb->irq_flagged = val & 0x1F;
		break;

	case GAMEBOY_ADDR_P1:
		// The arrow and button lines correspond to bits 4 and 5
		// respectively.  An _unset_ bit selects the line.
		// TODO: How should this behave if both bits are set or
		//       neither bit is set?
		if (val & BIT(5))
			gb->joypad_status = GAMEBOY_JOYPAD_ARROWS;
		else
			gb->joypad_status = GAMEBOY_JOYPAD_BUTTONS;
		break;

	case GAMEBOY_ADDR_DIV:
		gb->next_lcd_status_in -= gb->cycles;
		gb->next_timer_in -= gb->cycles;
		gb->cycles = 0;
		break;

	case GAMEBOY_ADDR_TIMA:
		gb->timer_counter = val;
		break;

	case GAMEBOY_ADDR_TMA:
		gb->timer_modulo = val;
		break;

	case GAMEBOY_ADDR_TAC:
		gb->timer_enabled = !!(val & BIT(2));
		timer_set_frequency(gb, val & 0x03);
		break;

	case GAMEBOY_ADDR_LCDC:
		gb->background_enabled = (val & BIT(0));
		gb->sprites_enabled = (val & BIT(1));
		gb->sprite_size = (val & BIT(2)) ? 16 : 8;
		gb->background_tilemap = gb->tilemap[!!(val & BIT(3))];
		lcd_update_tilemap_cache(gb, !(val & BIT(4)));
		gb->window_enabled = (val & BIT(5));
		gb->window_tilemap = gb->tilemap[!!(val & BIT(6))];
		if (val & BIT(7))
			lcd_enable(gb);
		else
			lcd_disable(gb);
		break;

	case GAMEBOY_ADDR_STAT:
		// TODO: Does this STAT if we're already in these modes?
		gb->stat_on_hblank = (val & BIT(3));
		gb->stat_on_vblank = (val & BIT(4));
		gb->stat_on_oam_search = (val & BIT(5));
		gb->stat_on_scanline = (val & BIT(6));
		break;

	case GAMEBOY_ADDR_DMA:
		// TODO: DMAs are much more complicated than this
		gb->dma = val;
		for (int from = (val << 8), to = 0xFE00, i = 0; i < 0xA0; ++i)
			mmu_write(gb, (to | i), mmu_read(gb, (from | i)));
		break;

	case GAMEBOY_ADDR_LY:
		// TODO: "Writing will reset the counter"
		//       Just the counter, or does it restart the rendering?
		break;

	case GAMEBOY_ADDR_LYC:
		gb->scanline_compare = val;
		lcd_update_scanline(gb, gb->scanline);
		break;

	case GAMEBOY_ADDR_SCY:
		gb->sy = val;
		break;

	case GAMEBOY_ADDR_SCX:
		gb->sx = val;
		break;

	case GAMEBOY_ADDR_WY:
		gb->wy = val;
		break;

	case GAMEBOY_ADDR_WX:
		gb->wx = val - 7;
		break;

	case GAMEBOY_ADDR_BGP:
		gb->bgp = val;
		break;

	case GAMEBOY_ADDR_OBP0:
		gb->obp[0] = val;
		break;

	case GAMEBOY_ADDR_OBP1:
		gb->obp[1] = val;
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
