#include "common.h"
#include "lcd.h"

static int to_color(int color, uint8_t palette)
{
	color = palette >> (2 * (color & 0x03));

	switch (color & 0x03) {
	case 0:  return 0x00FFFFFF;
	case 1:  return 0x00BBBBBB;
	case 2:  return 0x00555555;
	default: return 0x00000000;
	}
}

static void render_debug(struct gameboy *gb)
{
	for (int ty = 0; ty < 24; ++ty) {
		for (int tx = 0; tx < 16; ++tx) {
			struct tile *t = &gb->tiles[(16 * ty) + tx];

			for (int dy = 0; dy < 8; ++dy) {
				for (int dx = 0; dx < 8; ++dx) {
					int y = (8 * ty) + dy;
					int x = (8 * tx) + dx;
					int color = t->pixels[dy][dx];

					gb->dbg_vram[y][x] = to_color(color, gb->bgp);
				}
			}
		}
	}

	for (int ty = 0; ty < 32; ++ty) {
		for (int tx = 0; tx < 32; ++tx) {
			struct tile *t = gb->background_tilemap[(32 * ty) + tx];

			for (int dy = 0; dy < 8; ++dy) {
				for (int dx = 0; dx < 8; ++dx) {
					int y = (8 * ty) + dy;
					int x = (8 * tx) + dx;
					int color = t->pixels[dy][dx];

					gb->dbg_background[y][x] = to_color(color, gb->bgp);
				}
			}
		}
	}

	for (int ty = 0; ty < 32; ++ty) {
		for (int tx = 0; tx < 32; ++tx) {
			struct tile *t = gb->window_tilemap[(32 * ty) + tx];

			for (int dy = 0; dy < 8; ++dy) {
				for (int dx = 0; dx < 8; ++dx) {
					int y = (8 * ty) + dy;
					int x = (8 * tx) + dx;
					int color = t->pixels[dy][dx];

					gb->dbg_window[y][x] = to_color(color, gb->bgp);
				}
			}
		}
	}
}

void lcd_init(struct gameboy *gb)
{
	for (int i = 0; i < 1024; ++i)
		gb->tilemap[0][i] = &gb->tiles[0];
	for (int i = 0; i < 1024; ++i)
		gb->tilemap[1][i] = &gb->tiles[0];

	gb->background_tilemap = gb->tilemap[0];
	gb->window_tilemap = gb->tilemap[1];

	gb->lcd_enabled = true;
	lcd_disable(gb);
}

void lcd_sync(struct gameboy *gb)
{
	if (!gb->lcd_enabled || gb->cycles < gb->next_lcd_status_in)
		return;

	gb->lcd_status = gb->next_lcd_status;
	switch (gb->lcd_status) {
	case GAMEBOY_LCD_OAM_SEARCH:
		if (++gb->scanline > 143)
			gb->scanline = 0;

		gb->next_lcd_status = GAMEBOY_LCD_PIXEL_TRANSFER;
		gb->next_lcd_status_in += 80;
		break;

	case GAMEBOY_LCD_PIXEL_TRANSFER:
		gb->next_lcd_status = GAMEBOY_LCD_HBLANK;
		gb->next_lcd_status_in += 172;
		break;

	case GAMEBOY_LCD_HBLANK:
		if (gb->scanline == 143)
			gb->next_lcd_status = GAMEBOY_LCD_VBLANK;
		else
			gb->next_lcd_status = GAMEBOY_LCD_OAM_SEARCH;

		gb->next_lcd_status_in += 204;
		break;

	case GAMEBOY_LCD_VBLANK:
		if (++gb->scanline == 153)
			gb->next_lcd_status = GAMEBOY_LCD_OAM_SEARCH;
		else
			gb->next_lcd_status = GAMEBOY_LCD_VBLANK;

		gb->next_lcd_status_in += 456;

		if (gb->scanline == 144 && gb->on_vblank.callback) {
			render_debug(gb);
			gb->on_vblank.callback(gb, gb->on_vblank.context);
		}

		break;
	}
}

void lcd_enable(struct gameboy *gb)
{
	if (gb->lcd_enabled)
		return;

	gb->lcd_enabled = true;
	gb->scanline = 0;
	gb->lcd_status = GAMEBOY_LCD_OAM_SEARCH;
	gb->next_lcd_status = GAMEBOY_LCD_PIXEL_TRANSFER;
	gb->next_lcd_status_in = gb->cycles += 80;
}

void lcd_disable(struct gameboy *gb)
{
	if (!gb->lcd_enabled)
		return;

	gb->lcd_enabled = false;
	gb->lcd_status = GAMEBOY_LCD_HBLANK;
	gb->scanline = 0;
}

void lcd_update_tile(struct gameboy *gb, uint16_t offset, uint8_t val)
{
	struct tile *t = &gb->tiles[offset / 16];

	uint8_t *row = t->pixels[(offset / 2) % 8];
	int bit = (offset % 2) ? 0x02 : 0x01;

	for (int n = 0; n < 8; ++n) {
		if (val & (1 << n))
			row[7-n] |= bit;
		else
			row[7-n] &= ~bit;
	}
}

void lcd_update_tilemap(struct gameboy *gb, uint16_t offset, uint8_t val)
{
	struct tile *t = &gb->tiles[val];

	gb->tilemap[offset >= 0x0400][offset % 0x0400] = t;
}
