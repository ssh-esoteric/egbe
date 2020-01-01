// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef EGBE_GAMEBOY_H
#define EGBE_GAMEBOY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct gameboy;
struct gameboy_callback;
struct gameboy_palette;
struct gameboy_tile;

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

	GAMEBOY_ADDR_P1  = 0xFF00,

	GAMEBOY_ADDR_SB  = 0xFF01,
	GAMEBOY_ADDR_SC  = 0xFF02,

	GAMEBOY_ADDR_DIV  = 0xFF04,
	GAMEBOY_ADDR_TIMA = 0xFF05,
	GAMEBOY_ADDR_TMA  = 0xFF06,
	GAMEBOY_ADDR_TAC  = 0xFF07,

	GAMEBOY_ADDR_IF  = 0xFF0F,
	GAMEBOY_ADDR_IE  = 0xFFFF,

	GAMEBOY_ADDR_NR10 = 0xFF10,
	GAMEBOY_ADDR_NR11 = 0xFF11,
	GAMEBOY_ADDR_NR12 = 0xFF12,
	GAMEBOY_ADDR_NR13 = 0xFF13,
	GAMEBOY_ADDR_NR14 = 0xFF14,
	GAMEBOY_ADDR_NR21 = 0xFF16,
	GAMEBOY_ADDR_NR22 = 0xFF17,
	GAMEBOY_ADDR_NR23 = 0xFF18,
	GAMEBOY_ADDR_NR24 = 0xFF19,
	GAMEBOY_ADDR_NR30 = 0xFF1A,
	GAMEBOY_ADDR_NR31 = 0xFF1B,
	GAMEBOY_ADDR_NR32 = 0xFF1C,
	GAMEBOY_ADDR_NR33 = 0xFF1D,
	GAMEBOY_ADDR_NR34 = 0xFF1E,
	GAMEBOY_ADDR_NR41 = 0xFF20,
	GAMEBOY_ADDR_NR42 = 0xFF21,
	GAMEBOY_ADDR_NR43 = 0xFF22,
	GAMEBOY_ADDR_NR44 = 0xFF23,
	GAMEBOY_ADDR_NR50 = 0xFF24,
	GAMEBOY_ADDR_NR51 = 0xFF25,
	GAMEBOY_ADDR_NR52 = 0xFF26,

	GAMEBOY_ADDR_LCDC = 0xFF40,
	GAMEBOY_ADDR_STAT = 0xFF41,
	GAMEBOY_ADDR_SCY  = 0xFF42,
	GAMEBOY_ADDR_SCX  = 0xFF43,
	GAMEBOY_ADDR_LY   = 0xFF44,
	GAMEBOY_ADDR_LYC  = 0xFF45,
	GAMEBOY_ADDR_DMA  = 0xFF46,
	GAMEBOY_ADDR_BGP  = 0xFF47,
	GAMEBOY_ADDR_OBP0 = 0xFF48,
	GAMEBOY_ADDR_OBP1 = 0xFF49,
	GAMEBOY_ADDR_WY   = 0xFF4A,
	GAMEBOY_ADDR_WX   = 0xFF4B,

	GAMEBOY_ADDR_BOOT_SWITCH = 0xFF50,

	GAMEBOY_ADDR_KEY1  = 0xFF4D,
	GAMEBOY_ADDR_VBK   = 0xFF4F,
	GAMEBOY_ADDR_HDMA1 = 0xFF51,
	GAMEBOY_ADDR_HDMA2 = 0xFF52,
	GAMEBOY_ADDR_HDMA3 = 0xFF53,
	GAMEBOY_ADDR_HDMA4 = 0xFF54,
	GAMEBOY_ADDR_HDMA5 = 0xFF55,
	GAMEBOY_ADDR_RP    = 0xFF56,
	GAMEBOY_ADDR_BGPI  = 0xFF68,
	GAMEBOY_ADDR_BGPD  = 0xFF69,
	GAMEBOY_ADDR_OBPI  = 0xFF6A,
	GAMEBOY_ADDR_OBPD  = 0xFF6B,
	GAMEBOY_ADDR_SVBK  = 0xFF70,
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

enum gameboy_ime_status {
	GAMEBOY_IME_DISABLED,
	GAMEBOY_IME_PENDING,
	GAMEBOY_IME_ENABLED,
};

// IE/IF flag: (1 << n)
// RST vector: 0x0040 + (n * 0x08)
enum gameboy_irq {
	GAMEBOY_IRQ_VBLANK = 0,
	GAMEBOY_IRQ_STAT   = 1,
	GAMEBOY_IRQ_TIMER  = 2,
	GAMEBOY_IRQ_SERIAL = 3,
	GAMEBOY_IRQ_JOYPAD = 4,
};

enum gameboy_joypad_status {
	GAMEBOY_JOYPAD_ARROWS,
	GAMEBOY_JOYPAD_BUTTONS,
};

enum gameboy_lcd_status {
	GAMEBOY_LCD_HBLANK         = 0,
	GAMEBOY_LCD_VBLANK         = 1,
	GAMEBOY_LCD_OAM_SEARCH     = 2,
	GAMEBOY_LCD_PIXEL_TRANSFER = 3,
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

struct apu_envelope_module {
	int volume_max;
	int volume;
	int delta; // 1 or -1

	int clocks_max;
	int clocks_remaining;
};

struct apu_length_module {
	bool is_terminal; // Disable the channel when the length is done?

	int clocks_max;
	int clocks_remaining;
};

struct apu_sweep_module {
	int shadow; // Shadow of frequency
	int shift; // Shadow >> shift
	int delta; // 1 or -1

	int sweeps_max;
	int sweeps_remaining;
};

struct apu_channel {
	bool muted; // Emulator-only flag to supress channel output
	bool enabled;
	bool dac;

	bool output_left;
	bool output_right;

	int frequency; // Frequency of CLOCKs; NOT (1/period) of waveform
	int period;
	long next_tick_in;
};

struct apu_square_channel {
	struct apu_channel super;

	uint8_t duty; // Duty wave from apu.c: duty_waves[4][8]
	uint8_t duty_index;

	struct apu_envelope_module envelope;
	struct apu_length_module length;
	struct apu_sweep_module sweep;
};

struct apu_wave_channel {
	struct apu_channel super;

	uint8_t volume_shift;

	uint8_t samples[32];
	uint8_t index;

	struct apu_length_module length;
};

struct apu_noise_channel {
	struct apu_channel super;

	uint16_t lfsr;
	uint16_t lfsr_mask; // Mask for 7-bit or 15-bit mode
	uint8_t shift;
	uint8_t divisor;

	struct apu_envelope_module envelope;
	struct apu_length_module length;
};

struct gameboy_callback {
	void (*callback)(struct gameboy *gb, void *context);
	void *context;
};

struct gameboy_joypad {
	bool right;
	bool left;
	bool up;
	bool down;

	bool a;
	bool b;
	bool select;
	bool start;
};

struct gameboy_palette {
	int colors[4];

	uint8_t raw[8];
};

struct gameboy_sprite {
	struct gameboy_palette *palette;
	struct gameboy_tile *tile; // First tile of two in 8x16 mode
	uint8_t palette_index; // DMG: 0-1; GBC: 0-7
	uint8_t tile_index;
	uint8_t vram_bank;

	uint8_t raw_flags;
	uint8_t x;
	uint8_t y;
	bool flipx;
	bool flipy;
	bool priority;
};

struct gameboy_tile {
	uint8_t pixels[8][8]; // 8x8 2-bit color codes
	uint8_t raw[16];
};

struct gameboy_background_cell {
	struct gameboy_palette *palette;
	struct gameboy_tile *tile;
	uint8_t palette_index;
	uint8_t tile_index;
	uint8_t vram_bank;

	uint8_t raw_flags;
	bool flipx;
	bool flipy;
	bool priority;
};

struct gameboy_background_table {
	union {
		struct gameboy_background_cell cells[32][32];
		struct gameboy_background_cell cells_flat[1024];
	};
};

struct gameboy {
	unsigned int features;
	enum gameboy_mbc mbc;
	enum gameboy_system system;
	bool gbc;

	enum gameboy_cpu_status cpu_status;
	long cycles;

	bool double_speed;
	bool double_speed_switch;

	enum gameboy_ime_status ime_status;
	uint8_t irq_enabled;
	uint8_t irq_flagged;

	enum gameboy_joypad_status joypad_status;
	uint8_t p1_arrows;
	uint8_t p1_buttons;

	long next_timer_in;
	bool timer_enabled;
	uint8_t timer_counter;
	uint8_t timer_modulo;
	uint8_t timer_frequency_code;
	int timer_frequency_cycles;

	long next_serial_in;
	bool is_serial_pending;
	bool is_serial_internal;
	uint8_t sb;
	uint8_t next_sb;
	struct gameboy_callback on_serial_start;

	bool apu_enabled;
	long next_apu_frame_in;
	uint8_t apu_frame;
	uint8_t so1_volume;
	uint8_t so2_volume;
	bool so1_vin;
	bool so2_vin;

	// TODO: The APU currently assumes 2 float channels at 44100 Hz
	long next_apu_sample;
	size_t apu_index;
	float apu_sample[2048];
	struct gameboy_callback on_apu_buffer_filled;

	struct apu_square_channel sq1;
	struct apu_square_channel sq2;
	struct apu_wave_channel wave;
	struct apu_noise_channel noise;

	bool lcd_enabled;
	enum gameboy_lcd_status lcd_status;
	enum gameboy_lcd_status next_lcd_status;
	long next_lcd_status_in;

	uint8_t scanline;
	uint8_t scanline_compare;
	uint8_t sy;
	uint8_t sx;
	uint8_t wy;
	uint8_t wx;
	uint8_t dma;
	uint8_t sprite_size;
	bool sprites_enabled;
	bool background_enabled;
	bool window_enabled;
	bool stat_on_hblank;
	bool stat_on_vblank;
	bool stat_on_oam_search;
	bool stat_on_scanline;

	bool hdma_enabled;
	bool gdma;
	uint8_t hdma_blocks_remaining;
	uint8_t hdma_blocks_queued;
	uint16_t hdma_src;
	uint16_t hdma_dst;

	struct gameboy_palette bgp[8];
	struct gameboy_palette obp[8];

	uint8_t bgp_index;
	bool bgp_increment;
	uint8_t obp_index;
	bool obp_increment;

	struct gameboy_callback on_vblank;
	int screen[144][160];
	int dbg_background[256][256];
	int dbg_window[256][256];
	int dbg_palettes[82][86];
	int dbg_vram[192][128];
	int dbg_vram_gbc[192][128];

	struct gameboy_sprite sprites[40];
	struct gameboy_sprite *sprites_sorted[40];
	bool sprites_unsorted;

	struct gameboy_tile tiles[2][384];
	struct gameboy_background_table tilemaps[2];
	struct gameboy_background_table *background_tilemap;
	struct gameboy_background_table *window_tilemap;
	uint8_t vram_bank;
	bool tilemap_signed;

	bool boot_enabled;
	uint8_t *boot;
	size_t boot_size;

	uint8_t (*rom)[0x4000];
	uint8_t *romx;
	size_t rom_bank;
	size_t rom_banks;
	size_t rom_size;

	bool sram_enabled;
	uint8_t (*sram)[0x2000];
	uint8_t *sramx;
	size_t sram_bank;
	size_t sram_banks;
	size_t sram_size;

	uint8_t (*wram)[0x1000];
	uint8_t *wramx;
	size_t wram_bank;
	size_t wram_banks;
	size_t wram_size;

	uint8_t hram[0x007F];

	bool mbc1_sram_mode;

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

int gameboy_load_sram(struct gameboy *gb, char *path);
int gameboy_save_sram(struct gameboy *gb, char *path);

void gameboy_update_joypad(struct gameboy *gb, struct gameboy_joypad *jp);

void gameboy_start_serial(struct gameboy *gb, uint8_t xfer);

#endif
