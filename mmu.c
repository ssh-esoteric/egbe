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

	case GAMEBOY_ADDR_IE:
		return gb->irq_enabled;

	case GAMEBOY_ADDR_IF:
		return gb->irq_flagged & 0xE0;


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
