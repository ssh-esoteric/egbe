#ifndef EGBE_GAMEBOY_H
#define EGBE_GAMEBOY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum gameboy_addr {
	GAMEBOY_ADDR_NINTENDO_LOGO     = 0x0104,
	GAMEBOY_ADDR_GAME_TITLE        = 0x0134,
	GAMEBOY_ADDR_MANUFACTURER_CODE = 0x013F,
	GAMEBOY_ADDR_GBC_FLAG          = 0x0143,
	GAMEBOY_ADDR_NEW_LICENSEE_CODE = 0x0144,
	GAMEBOY_ADDR_SGB_FLAG          = 0x0146,
	GAMEBOY_ADDR_CARTRIDGE_TYPE    = 0x0147,
	GAMEBOY_ADDR_ROM_SIZE_CODE     = 0x0148,
	GAMEBOY_ADDR_SRAM_SIZE_CODE    = 0x0149,
	GAMEBOY_ADDR_DESTINATION_CODE  = 0x014A,
	GAMEBOY_ADDR_OLD_LICENSEE_CODE = 0x014B,
	GAMEBOY_ADDR_ROM_VERSION       = 0x014C,
	GAMEBOY_ADDR_HEADER_CHECKSUM   = 0x014D,
	GAMEBOY_ADDR_GLOBAL_CHECKSUM   = 0x014E,
};

enum gameboy_cpu_status {
	GAMEBOY_CPU_CRASHED,
	GAMEBOY_CPU_RUNNING,
	GAMEBOY_CPU_HALTED,
	GAMEBOY_CPU_STOPPED,
};

enum gameboy_features {
	GAMEBOY_FEATURE_SRAM          = (1 << 0),
	GAMEBOY_FEATURE_BATTERY       = (1 << 1),
	GAMEBOY_FEATURE_RTC           = (1 << 2), // AKA "Timer"
	GAMEBOY_FEATURE_RUMBLE        = (1 << 3),
	GAMEBOY_FEATURE_ACCELEROMETER = (1 << 3),
};

enum gameboy_mbc {
	GAMEBOY_MBC_NONE,
	GAMEBOY_MBC_MBC1,
	GAMEBOY_MBC_MBC2,
	GAMEBOY_MBC_MBC3,
	GAMEBOY_MBC_MMM01,
	GAMEBOY_MBC_MBC5,
	GAMEBOY_MBC_MBC6,
	GAMEBOY_MBC_MBC7,
	GAMEBOY_MBC_HUC1,
	GAMEBOY_MBC_HUC3,
	GAMEBOY_MBC_TAMA5,
	GAMEBOY_MBC_CAMERA,
};

enum gameboy_system {
	GAMEBOY_SYSTEM_DMG,
	GAMEBOY_SYSTEM_GBP,
	GAMEBOY_SYSTEM_SGB,
	GAMEBOY_SYSTEM_GBC,
	GAMEBOY_SYSTEM_SGB2,
};

struct gameboy {
	unsigned int features;
	enum gameboy_mbc mbc;
	enum gameboy_system system;

	enum gameboy_cpu_status cpu_status;

	bool boot_enabled;
	uint8_t *boot;
	size_t boot_size;

	uint8_t (*rom)[0x4000];
	uint8_t *rom_bank;
	size_t rom_size;

	uint8_t (*sram)[0x2000];
	uint8_t *sram_bank;
	size_t sram_size;

	uint8_t (*wram)[0x1000];
	uint8_t *wram_bank;
	size_t wram_size;

	uint8_t hram[0x007F];

	uint16_t pc;
	uint16_t sp;

	union {
		struct {
			union {
				struct {
					uint8_t _unused_cpu_flags:4;
					uint8_t carry:1;
					uint8_t halfcarry:1;
					uint8_t subtract:1;
					uint8_t zero:1;
				};
				uint8_t f;
			};
			uint8_t a;
		};
		uint16_t af;
	};

	union {
		struct {
			uint8_t c;
			uint8_t b;
		};
		uint16_t bc;
	};

	union {
		struct {
			uint8_t e;
			uint8_t d;
		};
		uint16_t de;
	};

	union {
		struct {
			uint8_t l;
			uint8_t h;
		};
		uint16_t hl;
	};
};

struct gameboy *gameboy_alloc(enum gameboy_system system);
void gameboy_free(struct gameboy *gb);

void gameboy_restart(struct gameboy *gb);
void gameboy_tick(struct gameboy *gb);

int gameboy_insert_boot_rom(struct gameboy *gb, char *path);
void gameboy_remove_boot_rom(struct gameboy *gb);
int gameboy_insert_cartridge(struct gameboy *gb, char *path);
void gameboy_remove_cartridge(struct gameboy *gb);

#endif
