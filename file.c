// SPDX-License-Identifier: GPL-3.0-or-later
#include "lcd.h"
#include "common.h"
#include <string.h>

#define ROM_BANK_SIZE sizeof(((struct gameboy *)NULL)->rom[0])
#define SRAM_BANK_SIZE sizeof(((struct gameboy *)NULL)->sram[0])

#define ENDIAN_CHECK (('E' << 24) | ('G' << 16) | ('B' << 8) | 'E')

struct gb_state {
	int32_t endian_check;
	uint32_t version; // TODO
	uint64_t sizeof_gameboy;
	char title_check[16];

	struct gameboy gb;
};

static long fsize_rewind(FILE *in)
{
	fseek(in, 0, SEEK_END);
	long result = ftell(in);
	fseek(in, 0, SEEK_SET);
	return result;
}

static int prepare_boot_rom(struct gameboy *gb, FILE *in)
{
	long size = fsize_rewind(in);
	long need = (gb->system >= GAMEBOY_SYSTEM_GBC) ? 0x0900 : 0x0100;
	if (size != need) {
		GBLOG("Bad boot ROM size (got $%lX; need $%lX)", size, need);
		return EINVAL;
	}

	gb->boot = malloc(size);
	if (!gb->boot) {
		GBLOG("Failed to allocate boot ROM: %m");
		return ENOMEM;
	}
	gb->boot_size = size;

	if (!fread(gb->boot, size, 1, in)) {
		GBLOG("Failed to read boot ROM file: %m");
		return EIO;
	}

	return 0;
}

static int prepare_cartridge(struct gameboy *gb, FILE *in)
{
	long size = fsize_rewind(in);
	long need = ROM_BANK_SIZE * 2;
	if (size < need) {
		GBLOG("ROM must be at least 2 banks large (got: $%lX)", size);
		return EINVAL;
	}

	fseek(in, GAMEBOY_ADDR_ROM_SIZE_CODE, SEEK_SET);
	uint8_t code = fgetc(in);
	fseek(in, 0, SEEK_SET);

	int banks = 0;
	switch (code) {
	case 0x00 ... 0x08:
		banks = 2 << code;
		need = ROM_BANK_SIZE * banks;
		break;
	default:
		GBLOG("Bad ROM size code: $%02X", code);
		return EINVAL;
	}

	if (size != need) {
		GBLOG("Bad ROM size (got $%lX; need $%lX)", size, need);
		return EINVAL;
	}

	gb->rom = malloc(size);
	if (!gb->rom) {
		GBLOG("Failed to allocate ROM: %m");
		return ENOMEM;
	}
	gb->rom_bank = 1; // This works out well even with no MBC
	gb->rom_banks = banks;
	gb->rom_size = size;
	gb->romx = gb->rom[gb->rom_bank];

	if (!fread(gb->rom, size, 1, in)) {
		GBLOG("Failed to read ROM file: %m");
		return EIO;
	}

	// Kind of a hack to make this table look nicer
	#define GAMEBOY_FEATURE_ 0
	#define GBTYPE(gb, m, f1, f2, f3)                    \
		do {                                         \
			gb->mbc = GAMEBOY_MBC_##m;           \
			gb->features = GAMEBOY_FEATURE_##f1  \
			             | GAMEBOY_FEATURE_##f2  \
			             | GAMEBOY_FEATURE_##f3; \
		} while (0)
	code = gb->rom[0][GAMEBOY_ADDR_CARTRIDGE_TYPE];
	switch (code) {
	case 0x00: GBTYPE(gb,   NONE ,      ,         ,              ); break;
	case 0x01: GBTYPE(gb,   MBC1 ,      ,         ,              ); break;
	case 0x02: GBTYPE(gb,   MBC1 , SRAM ,         ,              ); break;
	case 0x03: GBTYPE(gb,   MBC1 , SRAM , BATTERY ,              ); break;
	case 0x05: GBTYPE(gb,   MBC2 ,      ,         ,              ); break;
	case 0x06: GBTYPE(gb,   MBC2 , SRAM , BATTERY ,              ); break;
	case 0x08: GBTYPE(gb,   NONE , SRAM ,         ,              ); break;
	case 0x09: GBTYPE(gb,   NONE , SRAM , BATTERY ,              ); break;
	case 0x0B: GBTYPE(gb,  MMM01 ,      ,         ,              ); break;
	case 0x0C: GBTYPE(gb,  MMM01 , SRAM ,         ,              ); break;
	case 0x0D: GBTYPE(gb,  MMM01 , SRAM , BATTERY ,              ); break;
	case 0x0F: GBTYPE(gb,   MBC3 ,      , BATTERY , RTC          ); break;
	case 0x10: GBTYPE(gb,   MBC3 , SRAM , BATTERY , RTC          ); break;
	case 0x11: GBTYPE(gb,   MBC3 ,      ,         ,              ); break;
	case 0x12: GBTYPE(gb,   MBC3 , SRAM ,         ,              ); break;
	case 0x13: GBTYPE(gb,   MBC3 , SRAM , BATTERY ,              ); break;
	case 0x19: GBTYPE(gb,   MBC5 ,      ,         ,              ); break;
	case 0x1A: GBTYPE(gb,   MBC5 , SRAM ,         ,              ); break;
	case 0x1B: GBTYPE(gb,   MBC5 , SRAM , BATTERY ,              ); break;
	case 0x1C: GBTYPE(gb,   MBC5 ,      ,         , RUMBLE       ); break;
	case 0x1D: GBTYPE(gb,   MBC5 , SRAM ,         , RUMBLE       ); break;
	case 0x1E: GBTYPE(gb,   MBC5 , SRAM , BATTERY , RUMBLE       ); break;
	case 0x20: GBTYPE(gb,   MBC6 , SRAM , BATTERY ,              ); break;
	case 0x22: GBTYPE(gb,   MBC7 , SRAM , BATTERY , ACCELEROMETER); break;
	case 0xFC: GBTYPE(gb, CAMERA ,      ,         ,              ); break;
	case 0xFD: GBTYPE(gb,  TAMA5 ,      ,         ,              ); break;
	case 0xFE: GBTYPE(gb,   HUC3 ,      ,         ,              ); break;
	case 0xFF: GBTYPE(gb,   HUC1 , SRAM , BATTERY ,              ); break;
	default:
		GBLOG("Bad ROM type code: $%02X", code);
		return EINVAL;
	}

	banks = 1;
	size = 0;
	code = gb->rom[0][GAMEBOY_ADDR_SRAM_SIZE_CODE];
	switch (code) {
	case 0x00:
		if (gb->mbc == GAMEBOY_MBC_MBC2)
			size = SRAM_BANK_SIZE / 16;
		break;
	case 0x01: size = SRAM_BANK_SIZE / 4; break;
	case 0x02: size = SRAM_BANK_SIZE; break;
	case 0x03: size = SRAM_BANK_SIZE * (banks = 4);  break;
	case 0x04: size = SRAM_BANK_SIZE * (banks = 16); break;
	case 0x05: size = SRAM_BANK_SIZE * (banks = 8);  break;
	default:
		   GBLOG("Bad SRAM size code: $%02X", code);
		   return EINVAL;
	}

	if (size) {
		gb->sram = malloc(size);
		if (!gb->sram) {
			GBLOG("Failed to allocate SRAM: %m");
			return ENOMEM;
		}
		gb->sram_bank = 0;
		gb->sram_banks = banks;
		gb->sram_size = size;
		gb->sramx = gb->sram[gb->sram_bank];
	}

	return 0;
}

static void inspect_cartridge(struct gameboy *gb)
{
	#define line(key, fmt, ...) printf("%-19s" fmt "\n", key ": ", ##__VA_ARGS__)
	uint8_t *rom = gb->rom[0];
	char *text = (char *)rom;

	bool gbc_required = (rom[GAMEBOY_ADDR_GBC_FLAG] == 0xC0);
	bool is_sgb = (rom[GAMEBOY_ADDR_SGB_FLAG] == 0x03);
	bool is_intl = !!rom[GAMEBOY_ADDR_DESTINATION_CODE];

	if (gbc_required) {
		line("Game Title", "%.10s", &text[GAMEBOY_ADDR_GAME_TITLE]);
		line("Manufacturer Code", "%.4s", &text[GAMEBOY_ADDR_MANUFACTURER_CODE]);
	} else {
		line("Game Title", "%.15s", &text[GAMEBOY_ADDR_GAME_TITLE]);
	}
	line("ROM Version", "%d", rom[GAMEBOY_ADDR_ROM_VERSION]);
	line("Destination", "%s", is_intl ? "International" : "Japan");

	char old_licensee = rom[GAMEBOY_ADDR_OLD_LICENSEE_CODE];
	if (old_licensee == 0x33)
		line("New Licensee", "%.2s", &text[GAMEBOY_ADDR_NEW_LICENSEE_CODE]);
	else
		line("Old Licensee", "$%02X", old_licensee);

	char *gbc;
	switch (rom[GAMEBOY_ADDR_GBC_FLAG]) {
	case 0x00: gbc = "No";             break;
	case 0x80: gbc = "Yes (Optional)"; break;
	case 0xC0: gbc = "Yes (Required)"; break;
	default:   gbc = "Unknown";        break;
	}
	line("GBC Flag", "%s", gbc);

	line("SGB Flag", "%s", is_sgb ? "Yes" : "No");

	char *mbc;
	switch (gb->mbc) {
	case GAMEBOY_MBC_NONE:   mbc = "None";    break;
	case GAMEBOY_MBC_MBC1:   mbc = "MBC1";    break;
	case GAMEBOY_MBC_MBC2:   mbc = "MBC2";    break;
	case GAMEBOY_MBC_MBC3:   mbc = "MBC3";    break;
	case GAMEBOY_MBC_MMM01:  mbc = "MMM01";   break;
	case GAMEBOY_MBC_MBC5:   mbc = "MBC5";    break;
	case GAMEBOY_MBC_MBC6:   mbc = "MBC6";    break;
	case GAMEBOY_MBC_MBC7:   mbc = "MBC7";    break;
	case GAMEBOY_MBC_HUC1:   mbc = "HUC1";    break;
	case GAMEBOY_MBC_HUC3:   mbc = "HUC3";    break;
	case GAMEBOY_MBC_TAMA5:  mbc = "TAMA5";   break;
	case GAMEBOY_MBC_CAMERA: mbc = "CAMERA";  break;
	default:                 mbc = "Unknown"; break;
	}
	line("MBC", "%s", mbc);

	if (gb->features & GAMEBOY_FEATURE_SRAM)
		line("- Feature", "%s", "SRAM");
	if (gb->features & GAMEBOY_FEATURE_BATTERY)
		line("- Feature", "%s", "Battery");
	if (gb->features & GAMEBOY_FEATURE_RTC)
		line("- Feature", "%s", "Real-Time Clock");
	if (gb->features & GAMEBOY_FEATURE_RUMBLE)
		line("- Feature", "%s", "Rumble");
	if (gb->features & GAMEBOY_FEATURE_ACCELEROMETER)
		line("- Feature", "%s", "Accelerometer");

	line("ROM Size", "%lu banks", gb->rom_size / ROM_BANK_SIZE);

	if (gb->sram_size && gb->sram_size < SRAM_BANK_SIZE)
		line("SRAM Size", "1 bank ($%04lX)", gb->sram_size);
	else if (gb->sram_size == SRAM_BANK_SIZE)
		line("SRAM Size", "1 bank");
	else if (gb->sram_size > SRAM_BANK_SIZE)
		line("SRAM Size", "%lu banks", gb->sram_size / SRAM_BANK_SIZE);

	uint8_t logo[] = {
		0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B,
		0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
		0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E,
		0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99,
		0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC,
		0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E,
	};
	if (memcmp(logo, &rom[GAMEBOY_ADDR_NINTENDO_LOGO], sizeof(logo)) == 0)
		line("Logo Checksum", "Good");
	else
		line("Logo Checksum", "Bad");

	uint8_t hsum = 0;
	for (uint16_t addr = GAMEBOY_ADDR_GAME_TITLE; addr <= GAMEBOY_ADDR_ROM_VERSION; ++addr)
		hsum = hsum - rom[addr] - 1;

	uint8_t htmp = rom[GAMEBOY_ADDR_HEADER_CHECKSUM];
	if (hsum == htmp)
		line("Header Checksum", "Good");
	else
		line("Header Checksum", "Bad (got %02X; need %02X)", hsum, htmp);

	uint16_t gsum = 0;
	for (size_t addr = 0; addr < GAMEBOY_ADDR_GLOBAL_CHECKSUM; ++addr)
		gsum = gsum + rom[addr];
	for (size_t addr = GAMEBOY_ADDR_GLOBAL_CHECKSUM + 2; addr < gb->rom_size; ++addr)
		gsum = gsum + rom[addr];

	uint16_t gtmp = (rom[GAMEBOY_ADDR_GLOBAL_CHECKSUM] << 8)
	              |  rom[GAMEBOY_ADDR_GLOBAL_CHECKSUM + 1];
	if (gsum == gtmp)
		line("Global Checksum", "Good");
	else
		line("Global Checksum", "Bad (got %04X; need %04X)", gsum, gtmp);
}

int gameboy_insert_boot_rom(struct gameboy *gb, char *path)
{
	FILE *in = fopen(path, "rb");
	if (!in) {
		GBLOG("Failed to open boot ROM file: %m");
		return errno;
	}

	int rc = prepare_boot_rom(gb, in);
	if (rc)
		gameboy_remove_boot_rom(gb);

	fclose(in);

	return rc;
}

int gameboy_insert_cartridge(struct gameboy *gb, char *path)
{
	FILE *in = fopen(path, "rb");
	if (!in) {
		GBLOG("Failed to open cartridge file: %m");
		return errno;
	}

	int rc = prepare_cartridge(gb, in);
	if (rc)
		gameboy_remove_cartridge(gb);
	else
		inspect_cartridge(gb);

	fclose(in);

	return rc;
}

void gameboy_remove_boot_rom(struct gameboy *gb)
{
	free(gb->boot);
	gb->boot = NULL;
	gb->boot_size = 0;
}

void gameboy_remove_cartridge(struct gameboy *gb)
{
	free(gb->rom);
	gb->rom = NULL;
	gb->romx = NULL;
	gb->rom_bank = 0;
	gb->rom_banks = 0;
	gb->rom_size = 0;

	free(gb->sram);
	gb->sram = NULL;
	gb->sramx = NULL;
	gb->sram_bank = 0;
	gb->sram_banks = 0;
	gb->sram_size = 0;
}

static int fread_sram(struct gameboy *gb, FILE *in)
{
	long size = fsize_rewind(in);
	long need = gb->sram_size;

	if (size != need) {
		GBLOG("Bad SRAM size (got $%lX; need $%lX)", size, need);
		return EINVAL;
	}

	if (!fread(gb->sram, size, 1, in)) {
		GBLOG("Failed to read SRAM file: %m");
		return EIO;
	}

	return 0;
}

int gameboy_load_sram(struct gameboy *gb, char *path)
{
	if (!gb->sram_size)
		return 0;

	FILE *in = fopen(path, "rb");
	if (!in) {
		GBLOG("Failed to open SRAM file for reading: %m");
		return errno;
	}

	int rc = fread_sram(gb, in);
	fclose(in);

	return rc;
}

static int fwrite_sram(struct gameboy *gb, FILE *out)
{
	if (!fwrite(gb->sram, gb->sram_size, 1, out)) {
		GBLOG("Failed to write SRAM file: %m");
		return EIO;
	}

	return 0;
}

int gameboy_save_sram(struct gameboy *gb, char *path)
{
	if (!gb->sram_size)
		return 0;

	FILE *out = fopen(path, "wb");
	if (!out) {
		GBLOG("Failed to open SRAM file for writing: %m");
		return errno;
	}

	int rc = fwrite_sram(gb, out);
	fclose(out);

	return rc;
}

static int fread_state(struct gameboy *gb, FILE *in)
{
	struct gb_state state;
	long size = fsize_rewind(in);
	long need = sizeof(state);

	if (size < need) {
		GBLOG("Saved state is too small (got %lX; need >= %lX)",
		      size, need);
		return EINVAL;
	}

	if (!fread(&state, need, 1, in)) {
		GBLOG("Failed to read header of state file: %m");
		return EIO;
	}

	if (state.sizeof_gameboy != sizeof(*gb)) {
		GBLOG("Saved state differs in sizeof(struct gameboy)");
		return EINVAL;
	}

	if (state.endian_check != ENDIAN_CHECK) {
		GBLOG("Saved state differs in endianness");
		return EINVAL;
	}

	if (memcmp(&state.title_check, &gb->rom[0][GAMEBOY_ADDR_GAME_TITLE], 16)) {
		GBLOG("Saved state has the wrong title (got '%.16s'; need '%.16s')",
		      (char *)&state.title_check,
		      (char *)&gb->rom[0][GAMEBOY_ADDR_GAME_TITLE]);
		return EINVAL;
	}

	state.gb.on_apu_buffer_filled = gb->on_apu_buffer_filled;
	state.gb.on_serial_start = gb->on_serial_start;
	state.gb.on_vblank = gb->on_vblank;

	memset(&state.gb.apu_samples, 0, sizeof(state.gb.apu_samples));
	state.gb.apu_index = 0;

	state.gb.boot = gb->boot;
	state.gb.rom = gb->rom;
	state.gb.sram = gb->sram;
	state.gb.wram = gb->wram;

	memcpy(gb, &state.gb, sizeof(state.gb));

	gb->romx = gb->rom[gb->rom_bank];
	gb->sramx = gb->sram[gb->sram_bank];
	gb->wramx = gb->wram[gb->wram_bank];

	for (int i = 0; i < 40; ++i)
		gb->sprites_sorted[i] = &gb->sprites[i];
	gb->sprites_unsorted = true;

	for (int i = 0; i < 40; ++i)
		lcd_refresh_sprite(gb, &gb->sprites[i]);

	for (int i = 0; i < 0x0400; ++i)
		lcd_refresh_tilemap(gb, &gb->tilemaps[0].cells_flat[i]);
	for (int i = 0; i < 0x0400; ++i)
		lcd_refresh_tilemap(gb, &gb->tilemaps[1].cells_flat[i]);

	if (!fread(gb->wram, gb->wram_size, 1, in)) {
		GBLOG("Failed to read WRAM");
		return EIO;
	}

	if (gb->sram_size && !fread(gb->sram, gb->sram_size, 1, in)) {
		GBLOG("Failed to read SRAM");
		return EIO;
	}

	return 0;
}

int gameboy_load_state(struct gameboy *gb, char *path)
{
	FILE *in = fopen(path, "rb");
	if (!in) {
		GBLOG("Failed to open state file for reading: %m");
		return errno;
	}

	int rc = fread_state(gb, in);
	fclose(in);

	return rc;
}

static int fwrite_state(struct gameboy *gb, FILE *out)
{
	struct gb_state state = {
		.sizeof_gameboy = sizeof(*gb),
		.endian_check = ENDIAN_CHECK,
	};
	memcpy(&state.title_check, &gb->rom[0][GAMEBOY_ADDR_GAME_TITLE], 16);

	memcpy(&state.gb, gb, sizeof(*gb));

	fwrite(&state, sizeof(state), 1, out);
	fwrite(gb->wram, gb->wram_size, 1, out);
	fwrite(gb->sram, gb->sram_size, 1, out);

	return 0;
}

int gameboy_save_state(struct gameboy *gb, char *path)
{
	FILE *out = fopen(path, "wb");
	if (!out) {
		GBLOG("Failed to open state file for writing: %m");
		return errno;
	}

	int rc = fwrite_state(gb, out);
	fclose(out);

	return rc;
}
