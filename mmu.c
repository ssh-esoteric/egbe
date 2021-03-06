// SPDX-License-Identifier: GPL-3.0-or-later
#include "apu.h"
#include "lcd.h"
#include "mmu.h"
#include "timer.h"
#include "common.h"
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
		if (gb->features & GAMEBOY_FEATURE_RTC && val >= 0x08 && val <= 0x0C) {
			gb->rtc_status = (enum gameboy_rtc_status)(val - 7);
		} else {
			gb->rtc_status = GAMEBOY_RTC_DISABLED;

			gb->sram_bank = val % gb->sram_banks;
		}
		break;

	case 0x6000 ... 0x7FFF:
		if (gb->features & GAMEBOY_FEATURE_RTC) {
			gb->rtc_latch = (gb->rtc_latch << 8) | val;

			if (gb->rtc_latch != 0x0001)
				break;

			// (4MHz CPU >> 22) => seconds
			size_t mask = BIT(22) - 1;
			long diff = gb->cycles - gb->rtc_last_latched;
			gb->rtc_seconds += (diff >> 22);
			gb->rtc_last_latched = gb->cycles - (diff & mask);
		}
		break;
	}

	gb->romx = gb->rom[gb->rom_bank];
	gb->sramx = gb->sram[gb->sram_bank];
}

static uint8_t rtc_read(struct gameboy *gb)
{
	int tmp = gb->rtc_seconds;

	switch (gb->rtc_status) {
	case GAMEBOY_RTC_SECONDS:
		return tmp % 60;

	case GAMEBOY_RTC_MINUTES:
		return tmp / 60 % 60;

	case GAMEBOY_RTC_HOURS:
		return tmp / 60 / 60 % 24;

	case GAMEBOY_RTC_DAYS:
		return tmp / 60 / 60 / 24 % 256;

	case GAMEBOY_RTC_FLAGS:
		tmp /= (60 * 60 * 24 * 256);

		return (tmp == 1 ? BIT(0) : 0)
		     | BITS(1, 5)
		     | (gb->rtc_halted ? BIT(6) : 0)
		     | (tmp > 1 ? BIT(7) : 0);

	case GAMEBOY_RTC_DISABLED:
	default:
		GBLOG("Invalid RTC state: %d", gb->rtc_status);
		return 0xFF;
	}
}

static void rtc_write(struct gameboy *gb, uint8_t val)
{
	int orig = rtc_read(gb);
	int tmp = orig - val;

	switch (gb->rtc_status) {
	case GAMEBOY_RTC_SECONDS:
		gb->rtc_seconds += tmp;
		break;

	case GAMEBOY_RTC_MINUTES:
		gb->rtc_seconds += tmp * 60;
		break;

	case GAMEBOY_RTC_HOURS:
		gb->rtc_seconds += tmp * 60 * 60;
		break;

	case GAMEBOY_RTC_DAYS:
		gb->rtc_seconds += tmp * 60 * 60 * 24;
		break;

	case GAMEBOY_RTC_FLAGS:
		gb->rtc_halted = !!(val & BIT(6));

		tmp = (orig & BIT(0)) - (val & BIT(0));
		gb->rtc_seconds += tmp * 60 * 60 * 24 * 256;

		tmp = (orig & BIT(7)) - (val & BIT(7));
		tmp = !!tmp;
		gb->rtc_seconds += tmp * 60 * 60 * 24 * 256 * 2;
		break;

	case GAMEBOY_RTC_DISABLED:
	default:
		GBLOG("Invalid RTC state: %d", gb->rtc_status);
		break;
	}
}

uint8_t mmu_read(struct gameboy *gb, uint16_t addr)
{
	switch (addr) {
	case 0x0000 ... 0x00FF:
		if (gb->boot_enabled)
			return gb->boot[addr];
		if (gb->rom)
			return gb->rom[0][addr];
		break;
	case 0x0200 ... 0x08FF:
		if (gb->boot_enabled && gb->gbc)
			return gb->boot[addr];
		if (gb->rom)
			return gb->rom[0][addr];
		break;
	case 0x0100 ... 0x01FF:
	case 0x0900 ... 0x3FFF:
		if (gb->rom)
			return gb->rom[0][addr];
		break;

	case 0x4000 ... 0x7FFF:
		if (gb->rom)
			return gb->romx[addr % 0x4000];
		break;

	case 0x8000 ... 0x97FF:
		if (is_vram_accessible(gb))
			return lcd_read_tile(gb, addr % 0x2000);
		break;
	case 0x9800 ... 0x9FFF:
		if (is_vram_accessible(gb))
			return lcd_read_tilemap(gb, addr % 0x0800);
		break;

	case 0xA000 ... 0xBFFF:
		if (!gb->sram_enabled)
			break;

		if (gb->rtc_status)
			return rtc_read(gb);
		else
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
		if (is_oam_accessible(gb))
			return lcd_read_sprite(gb, addr % 0x0100);
		break;

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

	case GAMEBOY_ADDR_SB:
		if (gb->is_serial_pending)
			GBLOG("Mid-transfer read from SB!");
		return gb->sb;

	case GAMEBOY_ADDR_SC:
		return BITS(1, 6)
		     | (gb->is_serial_pending ? BIT(7) : 0)
		     | (gb->is_serial_internal ? BIT(0) : 0);

	case GAMEBOY_ADDR_DIV:
		return ((gb->cycles - gb->div_offset) >> 8) & 0xFF;

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
		     | (gb->background_tilemap ? BIT(3) : 0)
		     | (gb->tilemap_signed ? 0 : BIT(4))
		     | (gb->window_enabled ? BIT(5) : 0)
		     | (gb->window_tilemap ? BIT(6) : 0)
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
		return gb->bgp[0].raw[0];

	case GAMEBOY_ADDR_OBP0:
		return gb->obp[0].raw[0];

	case GAMEBOY_ADDR_OBP1:
		return gb->obp[1].raw[0];

	case GAMEBOY_ADDR_NR10:
		return gb->sq1.sweep.shift
		     | (gb->sq1.sweep.delta < 0 ? BIT(3) : 0)
		     | (gb->sq1.sweep.sweeps_max << 4)
		     | BIT(7);

	case GAMEBOY_ADDR_NR11:
		return BITS(0, 5)
		     | (gb->sq1.duty << 6);

	case GAMEBOY_ADDR_NR12:
		return gb->sq1.envelope.clocks_max
		     | (gb->sq1.envelope.delta > 0 ? BIT(3) : 0)
		     | (gb->sq1.envelope.volume_max << 4);

	case GAMEBOY_ADDR_NR13:
		return BITS(0, 7);

	case GAMEBOY_ADDR_NR14:
		return BITS(0, 2) // Write-only frequency
		     | BITS(3, 5) // Undefined
		     | (gb->sq1.length.is_terminal ? BIT(6) : 0)
		     | BIT(7);

	case GAMEBOY_ADDR_NR21:
		return BITS(0, 5)
		     | (gb->sq2.duty << 6);

	case GAMEBOY_ADDR_NR22:
		return gb->sq2.envelope.clocks_max
		     | (gb->sq2.envelope.delta > 0 ? BIT(3) : 0)
		     | (gb->sq2.envelope.volume_max << 4);

	case GAMEBOY_ADDR_NR23:
		return BITS(0, 7);

	case GAMEBOY_ADDR_NR24:
		return BITS(0, 2) // Write-only frequency
		     | BITS(3, 5) // Undefined
		     | (gb->sq2.length.is_terminal ? BIT(6) : 0)
		     | BIT(7);

	case GAMEBOY_ADDR_NR30:
		return BITS(0, 6)
		     | (gb->wave.super.dac ? BIT(7) : 0);

	case GAMEBOY_ADDR_NR31:
		return BITS(0, 7); // Pretty sure this is write-only?

	case GAMEBOY_ADDR_NR32:
		switch (gb->wave.volume_shift) {
		case 0: return BITS(0, 4) | (1 << 5) | BIT(7);
		case 1: return BITS(0, 4) | (2 << 5) | BIT(7);
		case 2: return BITS(0, 4) | (3 << 5) | BIT(7);
		case 4: return BITS(0, 4) | (0 << 5) | BIT(7);
		}
		GBLOG("Invalid wave volume shift: %d", gb->wave.volume_shift);
		break;

	case GAMEBOY_ADDR_NR33:
		return BITS(0, 7);

	case GAMEBOY_ADDR_NR34:
		return BITS(0, 2) // Write-only frequency
		     | BITS(3, 5) // Undefined
		     | (gb->wave.length.is_terminal ? BIT(6) : 0)
		     | BIT(7);

	case 0xFF30 ... 0xFF3F:
	{
		uint8_t offset = (addr % 0x10) * 2;
		return (gb->wave.samples[offset] << 4) | gb->wave.samples[offset + 1];
	}

	case GAMEBOY_ADDR_NR41:
		return BITS(0, 5) // TODO: W or R/W?
		     | BITS(6, 7);

	case GAMEBOY_ADDR_NR42:
		return gb->noise.envelope.clocks_max
		     | (gb->noise.envelope.delta > 0 ? BIT(3) : 0)
		     | (gb->noise.envelope.volume_max << 4);

	case GAMEBOY_ADDR_NR43:
		return gb->noise.divisor
		     | (gb->noise.lfsr_mask == 0x4040 ? BIT(3) : 0)
		     | (gb->noise.shift << 4);

	case GAMEBOY_ADDR_NR44:
		return BITS(0, 2) // Write-only frequency
		     | BITS(3, 5) // Undefined
		     | (gb->noise.length.is_terminal ? BIT(6) : 0)
		     | BIT(7);

	case GAMEBOY_ADDR_NR50:
		return gb->so1_volume
		     | (gb->so1_vin ? BIT(3) : 0)
		     | (gb->so2_volume << 4)
		     | (gb->so2_vin ? BIT(7) : 0);

	case GAMEBOY_ADDR_NR51:
		return (gb->sq1.super.output_left ? BIT(0) : 0)
		     | (gb->sq2.super.output_left ? BIT(1) : 0)
		     | (gb->wave.super.output_left ? BIT(2) : 0)
		     | (gb->noise.super.output_left ? BIT(3) : 0)
		     | (gb->sq1.super.output_right ? BIT(4) : 0)
		     | (gb->sq2.super.output_right ? BIT(5) : 0)
		     | (gb->wave.super.output_right ? BIT(6) : 0)
		     | (gb->noise.super.output_right ? BIT(7) : 0);

	case GAMEBOY_ADDR_NR52:
		return (gb->sq1.super.enabled ? BIT(0) : 0)
		     | (gb->sq2.super.enabled ? BIT(1) : 0)
		     | (gb->wave.super.enabled ? BIT(2) : 0)
		     | (gb->noise.super.enabled ? BIT(3) : 0)
		     | BITS(4, 6)
		     | (gb->apu_enabled ? BIT(7) : 0);

	case GAMEBOY_ADDR_KEY1:
		if (!gb->gbc)
			break;
		return (gb->double_speed_switch ? BIT(0) : 0)
		     | BITS(1, 6)
		     | (gb->double_speed ? BIT(7) : 0);

	case GAMEBOY_ADDR_VBK:
		if (gb->gbc)
			return 0xFE | gb->vram_bank;
		break;

	case GAMEBOY_ADDR_HDMA1:
	case GAMEBOY_ADDR_HDMA2:
	case GAMEBOY_ADDR_HDMA3:
	case GAMEBOY_ADDR_HDMA4:
		// TCAGBD: "Always returns FFh when read"
		break;

	case GAMEBOY_ADDR_HDMA5:
		if (!gb->gbc)
			break;

		if (!gb->hdma_enabled)
			return 0xFF;

		return ((gb->hdma_blocks_remaining - 1) & BITS(0, 6))
		     | (gb->gdma ? 0 : BIT(7));

	case GAMEBOY_ADDR_BGPI:
		if (!gb->gbc)
			break;
		return (gb->bgp_index & BITS(0, 5))
		     | BIT(6)
		     | (gb->bgp_increment ? BIT(7) : 0);

	case GAMEBOY_ADDR_BGPD:
		if (!gb->gbc)
			break;
		return gb->bgp[gb->bgp_index / 8].raw[gb->bgp_index % 8];

	case GAMEBOY_ADDR_OBPI:
		if (!gb->gbc)
			break;
		return (gb->obp_index & BITS(0, 5))
		     | BIT(6)
		     | (gb->obp_increment ? BIT(7) : 0);

	case GAMEBOY_ADDR_OBPD:
		if (!gb->gbc)
			break;
		return gb->obp[gb->obp_index / 8].raw[gb->obp_index % 8];

	case GAMEBOY_ADDR_SVBK:
		if (gb->gbc)
			return gb->wram_bank | BITS(3, 7);
		break;
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
		if (!gb->sram_enabled)
			break;

		if (gb->rtc_status)
			rtc_write(gb, val);
		else
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

	case GAMEBOY_ADDR_SB:
		if (gb->is_serial_pending)
			GBLOG("Mid-transfer write to SB!");
		gb->sb = val;
		break;

	case GAMEBOY_ADDR_SC:
		if (gb->is_serial_pending)
			GBLOG("Mid-transfer write to SC!");

		gb->is_serial_internal = !!(val & BIT(0));

		if (!gb->is_serial_pending && gb->is_serial_internal && (val & BIT(7))) {
			if (gb->on_serial_start.callback)
				gb_callback(gb, &gb->on_serial_start);
			else
				// Disconnected serial cables still "send" this
				gameboy_start_serial(gb, 0xFF);
		}
		break;

	case GAMEBOY_ADDR_DIV:
		gb->div_offset = gb->cycles;

		gb->next_apu_frame_in = gb->cycles + 8192;
		gb->next_timer_in = gb->cycles + gb->timer_frequency_cycles;
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

	case GAMEBOY_ADDR_NR10:
		gb->sq1.sweep.shift = val & BITS(0, 2);
		gb->sq1.sweep.delta = (val & BIT(3)) ? -1 : 1;
		gb->sq1.sweep.sweeps_max = (val & BITS(4, 6)) >> 4;
		break;

	case GAMEBOY_ADDR_NR11:
		gb->sq1.duty = (val & BITS(6, 7)) >> 6;
		gb->sq1.length.clocks_remaining = gb->sq1.length.clocks_max - (val & BITS(0, 5));
		break;

	case GAMEBOY_ADDR_NR12:
		gb->sq1.envelope.clocks_max = val & BITS(0, 2);
		gb->sq1.envelope.delta = (val & BIT(3)) ? 1 : -1;
		gb->sq1.envelope.volume_max = (val & BITS(4, 7)) >> 4;

		gb->sq1.super.dac = !!(val & BITS(3, 7));
		if (!gb->sq1.super.dac)
			gb->sq1.super.enabled = false;
		break;

	case GAMEBOY_ADDR_NR13:
		gb->sq1.super.frequency &= BITS(8, 10);
		gb->sq1.super.frequency |= val;

		gb->sq1.super.period = 4 * (2048 - gb->sq1.super.frequency);
		break;

	case GAMEBOY_ADDR_NR14:
		gb->sq1.super.frequency &= 0xFF;
		gb->sq1.super.frequency |= ((val & BITS(0, 2)) << 8);

		gb->sq1.super.period = 4 * (2048 - gb->sq1.super.frequency);

		gb->sq1.length.is_terminal = !!(val & BIT(6));
		if (val & BIT(7))
			apu_trigger_square(gb, &gb->sq1);
		break;

	case GAMEBOY_ADDR_NR21:
		gb->sq2.duty = (val & BITS(6, 7)) >> 6;
		gb->sq2.length.clocks_remaining = gb->sq2.length.clocks_max - (val & BITS(0, 5));
		break;

	case GAMEBOY_ADDR_NR22:
		gb->sq2.envelope.clocks_max = val & BITS(0, 2);
		gb->sq2.envelope.delta = (val & BIT(3)) ? 1 : -1;
		gb->sq2.envelope.volume_max = (val & BITS(4, 7)) >> 4;

		gb->sq2.super.dac = !!(val & BITS(3, 7));
		if (!gb->sq2.super.dac)
			gb->sq2.super.enabled = false;
		break;

	case GAMEBOY_ADDR_NR23:
		gb->sq2.super.frequency &= BITS(8, 10);
		gb->sq2.super.frequency |= val;

		gb->sq2.super.period = 4 * (2048 - gb->sq2.super.frequency);
		break;

	case GAMEBOY_ADDR_NR24:
		gb->sq2.super.frequency &= 0xFF;
		gb->sq2.super.frequency |= ((val & BITS(0, 2)) << 8);

		gb->sq2.super.period = 4 * (2048 - gb->sq2.super.frequency);

		gb->sq2.length.is_terminal = !!(val & BIT(6));
		if (val & BIT(7))
			apu_trigger_square(gb, &gb->sq2);
		break;

	case GAMEBOY_ADDR_NR30:
		gb->wave.super.dac = !!(val & BIT(7));
		if (!gb->wave.super.dac)
			gb->wave.super.enabled = false;
		break;

	case GAMEBOY_ADDR_NR31:
		gb->wave.length.clocks_remaining = gb->wave.length.clocks_max - val;
		break;

	case GAMEBOY_ADDR_NR32:
		switch ((val & BITS(5, 6)) >> 5) {
		case 0: gb->wave.volume_shift = 4; break; // Effectively mute
		case 1: gb->wave.volume_shift = 0; break;
		case 2: gb->wave.volume_shift = 1; break;
		case 3: gb->wave.volume_shift = 2; break;
		}
		break;

	case GAMEBOY_ADDR_NR33:
		gb->wave.super.frequency &= BITS(8, 10);
		gb->wave.super.frequency |= val;

		gb->wave.super.period = 2 * (2048 - gb->wave.super.frequency);
		break;

	case GAMEBOY_ADDR_NR34:
		gb->wave.super.frequency &= 0xFF;
		gb->wave.super.frequency |= ((val & BITS(0, 2)) << 8);

		gb->wave.super.period = 2 * (2048 - gb->wave.super.frequency);

		gb->wave.length.is_terminal = !!(val & BIT(6));
		if (val & BIT(7))
			apu_trigger_wave(gb, &gb->wave);
		break;

	case GAMEBOY_ADDR_NR41:
		gb->noise.length.clocks_remaining = gb->noise.length.clocks_max - (val & BITS(0, 5));
		break;

	case GAMEBOY_ADDR_NR42:
		gb->noise.envelope.clocks_max = val & BITS(0, 2);
		gb->noise.envelope.delta = (val & BIT(3)) ? 1 : -1;
		gb->noise.envelope.volume_max = (val & BITS(4, 7)) >> 4;

		gb->noise.super.dac = !!(val & BITS(3, 7));
		if (!gb->noise.super.dac)
			gb->noise.super.enabled = false;
		break;

	case GAMEBOY_ADDR_NR43:
		gb->noise.divisor = (val & BITS(0, 2));
		gb->noise.lfsr_mask = (val & BIT(3)) ? 0x4040 : 0x4000;
		gb->noise.shift = (val & BITS(4, 7)) >> 4;

		if (gb->noise.shift >= 14)
			GBLOG("Invalid LFSR shift: %d", gb->noise.shift);

		gb->noise.super.period = ((gb->noise.divisor * 16) ?: 8) << (gb->noise.shift);
		break;

	case GAMEBOY_ADDR_NR44:
		gb->noise.length.is_terminal = !!(val & BIT(6));
		if (val & BIT(7))
			apu_trigger_noise(gb, &gb->noise);
		break;

	case GAMEBOY_ADDR_NR50:
		gb->so1_volume = (val & BITS(0, 2));
		gb->so2_volume = (val & BITS(4, 6)) >> 4;
		gb->so1_vin = !!(val & BIT(3));
		gb->so2_vin = !!(val & BIT(7));
		break;

	case GAMEBOY_ADDR_NR51:
		gb->sq1.super.output_left = !!(val & BIT(0));
		gb->sq2.super.output_left = !!(val & BIT(1));
		gb->wave.super.output_left = !!(val & BIT(2));
		gb->noise.super.output_left = !!(val & BIT(3));

		gb->sq1.super.output_right = !!(val & BIT(4));
		gb->sq2.super.output_right = !!(val & BIT(5));
		gb->wave.super.output_right = !!(val & BIT(6));
		gb->noise.super.output_right = !!(val & BIT(7));
		break;

	case GAMEBOY_ADDR_NR52:
		if (val & BIT(7))
			apu_enable(gb);
		else
			apu_disable(gb);
		break;

	case 0xFF30 ... 0xFF3F:
	{
		uint8_t offset = (addr % 0x10) * 2;
		gb->wave.samples[offset + 0] = val >> 4;
		gb->wave.samples[offset + 1] = val & 0x0F;
		break;
	}

	case GAMEBOY_ADDR_LCDC:
		gb->background_enabled = (val & BIT(0));
		gb->sprites_enabled = (val & BIT(1));
		lcd_update_sprite_mode(gb, val & BIT(2));
		gb->background_tilemap = !!(val & BIT(3));
		lcd_update_tilemap_mode(gb, !(val & BIT(4)));
		gb->window_enabled = (val & BIT(5));
		gb->window_tilemap = !!(val & BIT(6));
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
		gb->bgp[0].raw[0] = val;
		lcd_update_palette_dmg(&gb->bgp[0], val);
		break;

	case GAMEBOY_ADDR_OBP0:
		gb->obp[0].raw[0] = val;
		lcd_update_palette_dmg(&gb->obp[0], val);
		break;

	case GAMEBOY_ADDR_OBP1:
		gb->obp[1].raw[0] = val;
		lcd_update_palette_dmg(&gb->obp[1], val);
		break;

	case GAMEBOY_ADDR_BOOT_SWITCH:
		if (val != 0x01 && val != 0x11) {
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

	case GAMEBOY_ADDR_KEY1:
		gb->double_speed_switch = !!(val & BIT(0));
		break;

	case GAMEBOY_ADDR_VBK:
		if (!gb->gbc)
			break;
		if (gb->hdma_enabled) {
			GBLOG("Can't update VRAM Bank while in HDMA");
			break;
		}
		gb->vram_bank = val & BIT(0);
		break;

	case GAMEBOY_ADDR_HDMA1:
		gb->hdma_src &= 0x00FF;
		gb->hdma_src |= (val << 8);
		break;
	case GAMEBOY_ADDR_HDMA2:
		gb->hdma_src &= 0xFF00;
		gb->hdma_src |= (val & 0xF0);
		break;
	case GAMEBOY_ADDR_HDMA3:
		gb->hdma_dst &= 0x00FF;
		gb->hdma_dst |= ((val & BITS(0, 4)) << 8) | BIT(15);
		break;
	case GAMEBOY_ADDR_HDMA4:
		gb->hdma_dst &= 0xFF00;
		gb->hdma_dst |= (val & 0xF0);
		break;
	case GAMEBOY_ADDR_HDMA5:
		if (!gb->gbc)
			break;

		gb->hdma_blocks_queued = 0;
		gb->hdma_blocks_remaining = (val & BITS(0, 6)) + 1;
		if (val & BIT(7)) {
			if (gb->hdma_enabled)
				GBLOG("Attempted to interrupt HDMA");

			gb->gdma = false;
			gb->hdma_enabled = true;
			if (!gb->lcd_enabled || gb->lcd_status == GAMEBOY_LCD_HBLANK)
				gb->hdma_blocks_queued = 1;

			//GBLOG("Start %d block HDMA: %04X => %04X",
			//      gb->hdma_blocks_remaining,
			//      gb->hdma_src, gb->hdma_dst);
		} else {
			if (!gb->gdma && gb->hdma_enabled) {
				gb->hdma_enabled = false;
				break;
			}

			gb->gdma = true;
			gb->hdma_enabled = true;
			gb->hdma_blocks_queued = gb->hdma_blocks_remaining;
			//GBLOG("Start %d block GDMA: %04X => %04X",
			//      gb->hdma_blocks_remaining,
			//      gb->hdma_src, gb->hdma_dst);
		}
		break;

	case GAMEBOY_ADDR_BGPI:
		if (!gb->gbc)
			break;
		gb->bgp_index = val & BITS(0, 5);
		gb->bgp_increment = !!(val & BIT(7));
		break;

	case GAMEBOY_ADDR_BGPD:
		if (!gb->gbc)
			break;
		gb->bgp[gb->bgp_index / 8].raw[gb->bgp_index % 8] = val;
		lcd_update_palette_gbc(&gb->bgp[gb->bgp_index / 8], gb->bgp_index % 8 / 2);
		gb->bgp_index = (gb->bgp_index + gb->bgp_increment) & BITS(0, 5);
		break;

	case GAMEBOY_ADDR_OBPI:
		if (!gb->gbc)
			break;
		gb->obp_index = val & BITS(0, 5);
		gb->obp_increment = !!(val & BIT(7));
		break;

	case GAMEBOY_ADDR_OBPD:
		if (!gb->gbc)
			break;
		gb->obp[gb->obp_index / 8].raw[gb->obp_index % 8] = val;
		lcd_update_palette_gbc(&gb->obp[gb->obp_index / 8], gb->obp_index % 8 / 2);
		gb->obp_index = (gb->obp_index + gb->obp_increment) & BITS(0, 5);
		break;

	case GAMEBOY_ADDR_SVBK:
		if (!gb->gbc)
			break;
		gb->wram_bank = (val & BITS(0, 2)) ?: 1;
		gb->wramx = gb->wram[gb->wram_bank];
		break;
	}
}
