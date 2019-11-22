#include "common.h"
#include "cpu.h"
#include "lcd.h"
#include <sys/param.h>

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

static int sprite_qsort(const void *p1, const void *p2)
{
	const struct sprite *lhs = p1;
	const struct sprite *rhs = p2;

	if (lhs->x != rhs->x)
		return (lhs->x < rhs->x) ? -1 : 1;

	return (lhs < rhs) ? -1 : 1;
}

static void render_scanline(struct gameboy *gb)
{
	uint8_t line[160];
	int y = gb->scanline;
	uint8_t dy = y + gb->sy;

	if (gb->sprites_unsorted)
		qsort(gb->sprites_sorted, 40, sizeof(void *), sprite_qsort);

	uint8_t wx;
	if (gb->window_enabled && gb->scanline >= gb->wy)
		wx = MAX(0, MIN(160, gb->wx));
	else
		wx = 160;

	for (int x = 0; x < wx; ++x) {
		if (!gb->background_enabled)
			break;

		uint8_t dx = x + gb->sx;

		struct tile *t = gb->background_tilemap[(dy / 8 * 32) + (dx / 8)];

		uint8_t code = t->pixels[dy % 8][dx % 8];
		line[x] = code;
		gb->screen[y][x] = to_color(code, gb->bgp);
	}
	for (int x = wx; x < 160; ++x) {
		// The window isn't affected by SX/SY offsets
		struct tile *t = gb->window_tilemap[(y / 8 * 32) + (x / 8)];

		uint8_t code = t->pixels[y % 8][x % 8];
		line[x] = code;
		gb->screen[y][x] = to_color(code, gb->bgp);
	}

	for (int i = 0; i < 40; ++i) {
		struct sprite *s = gb->sprites_sorted[i];

		uint8_t dy = (uint8_t)(y - s->y);

		if (dy >= gb->sprite_size)
			continue;

		// TODO: Tile/row fetching in 8x16 mode
		struct tile *t = &gb->tiles[s->index];
		uint8_t *row = t->pixels[s->flipy ? (7 - (dy % 8)) : (dy % 8)];

		for (int sx = 0; sx < 8; ++sx) {
			uint8_t dx = s->x + sx;

			if (dx >= 160)
				continue;

			uint8_t code = row[s->flipx ? (7 - sx) : sx];

			// Sprite color 0 is transparent
			if (!code)
				continue;

			// Low-priority sprites only prevail over bg color 0
			if (s->priority && line[dx])
				continue;

			line[dx] = code;
			gb->screen[y][dx] = to_color(code, gb->obp[s->palette]);
		}

		// TODO: Stop after 10th sprite per scanline
	}
}

static void enter_vblank(struct gameboy *gb)
{
	render_debug(gb);

	irq_flag(gb, GAMEBOY_IRQ_VBLANK);

	if (gb->stat_on_vblank)
		irq_flag(gb, GAMEBOY_IRQ_STAT);

	if (gb->on_vblank.callback)
		gb->on_vblank.callback(gb, gb->on_vblank.context);
}

void lcd_init(struct gameboy *gb)
{
	gb->tilemap_signed = true;
	lcd_update_tilemap_cache(gb, false);

	gb->background_tilemap = gb->tilemap[0];
	gb->window_tilemap = gb->tilemap[1];

	gb->sprite_size = 8;
	gb->sprites_unsorted = true;
	for (int i = 0; i < 40; ++i)
		gb->sprites_sorted[i] = &gb->sprites[i];

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
		lcd_update_scanline(gb, gb->scanline);

		if (gb->stat_on_oam_search)
			irq_flag(gb, GAMEBOY_IRQ_STAT);

		gb->next_lcd_status = GAMEBOY_LCD_PIXEL_TRANSFER;
		gb->next_lcd_status_in += 80;
		break;

	case GAMEBOY_LCD_PIXEL_TRANSFER:
		gb->next_lcd_status = GAMEBOY_LCD_HBLANK;
		gb->next_lcd_status_in += 172;
		break;

	case GAMEBOY_LCD_HBLANK:
		render_scanline(gb);

		if (gb->stat_on_hblank)
			irq_flag(gb, GAMEBOY_IRQ_STAT);

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

		lcd_update_scanline(gb, gb->scanline);

		gb->next_lcd_status_in += 456;

		if (gb->scanline == 144)
			enter_vblank(gb);

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

void lcd_update_scanline(struct gameboy *gb, uint8_t scanline)
{
	gb->scanline = scanline;

	if (gb->stat_on_scanline && gb->scanline == gb->scanline_compare)
		irq_flag(gb, GAMEBOY_IRQ_STAT);
}

void lcd_update_sprite(struct gameboy *gb, uint16_t offset, uint8_t val)
{
	struct sprite *s = &gb->sprites[offset / 4];
	switch (offset % 4) {
	case 0:
		s->y = val - 16;
		gb->sprites_unsorted = true;
		break;

	case 1:
		s->x = val - 8;
		gb->sprites_unsorted = true;
		break;

	case 2:
		s->index = val;
		break;

	case 3:
		s->palette = !!(val & BIT(4));
		s->flipx = !!(val & BIT(5));
		s->flipy = !!(val & BIT(6));
		s->priority = !!(val & BIT(7));
		break;
	}
}

void lcd_update_tile(struct gameboy *gb, uint16_t offset, uint8_t val)
{
	struct tile *t = &gb->tiles[offset / 16];
	t->raw[offset % 16] = val;

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
	struct tile *t = gb->tilemap_signed
		? &gb->tiles[256 + (int8_t)val]
		: &gb->tiles[val];

	gb->tilemap_raw[offset] = val;
	gb->tilemap[offset >= 0x0400][offset % 0x0400] = t;
}

void lcd_update_tilemap_cache(struct gameboy *gb, bool is_signed)
{
	if (gb->tilemap_signed == is_signed)
		return;

	gb->tilemap_signed = is_signed;
	for (int i = 0; i < 0x0800; ++i)
		lcd_update_tilemap(gb, i, gb->tilemap_raw[i]);
}
