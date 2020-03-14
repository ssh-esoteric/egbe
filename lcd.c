// SPDX-License-Identifier: GPL-3.0-or-later
#include "cpu.h"
#include "lcd.h"
#include "common.h"
#include <sys/param.h>

// Used to more easily debug VRAM (no changing palette or duplicated colors)
static const struct gameboy_palette monochrome = {
	.colors = {
		0x00FFFFFF,
		0x00BBBBBB,
		0x00555555,
		0x00000000,
	},
};

void lcd_update_palette_dmg(struct gameboy_palette *p, uint8_t val)
{
	p->colors[0] = monochrome.colors[(val & BITS(0, 1)) >> 0];
	p->colors[1] = monochrome.colors[(val & BITS(2, 3)) >> 2];
	p->colors[2] = monochrome.colors[(val & BITS(4, 5)) >> 4];
	p->colors[3] = monochrome.colors[(val & BITS(6, 7)) >> 6];
}

void lcd_update_palette_gbc(struct gameboy_palette *p, uint8_t index)
{
	int tmp = (p->raw[(index << 1) + 1] << 8) | p->raw[index << 1];

	tmp = ((tmp & BITS(0, 4))   << 19) // R
	    | ((tmp & BITS(5, 9))   << 6)  // G
	    | ((tmp & BITS(10, 15)) >> 7); // B

	// Roughly convert colors from 5-bit to 8-bit
	tmp |= ((tmp & 0x00E0E0E0) >> 5);

	p->colors[index] = tmp;
}

static void render_debug(struct gameboy *gb)
{
	struct gameboy_background_cell *cell;
	struct gameboy_tile *tile;

	if (gb->dbg_vram) {
		for (int ty = 0; ty < 24; ++ty) {
			for (int tx = 0; tx < 16; ++tx) {
				tile = &gb->tiles[0][(16 * ty) + tx];

				for (int dy = 0; dy < 8; ++dy) {
					for (int dx = 0; dx < 8; ++dx) {
						int y = (8 * ty) + dy;
						int x = (8 * tx) + dx;
						int color = tile->pixels[dy][dx];

						(*gb->dbg_vram)[y][x] = monochrome.colors[color];
					}
				}
			}
		}
	}

	if (gb->dbg_vram_gbc) {
		for (int ty = 0; ty < 24; ++ty) {
			for (int tx = 0; tx < 16; ++tx) {
				tile = &gb->tiles[1][(16 * ty) + tx];

				for (int dy = 0; dy < 8; ++dy) {
					for (int dx = 0; dx < 8; ++dx) {
						int y = (8 * ty) + dy;
						int x = (8 * tx) + dx;
						int color = tile->pixels[dy][dx];

						(*gb->dbg_vram_gbc)[y][x] = monochrome.colors[color];
					}
				}
			}
		}
	}

	if (gb->dbg_background) {
		for (int ty = 0; ty < 32; ++ty) {
			for (int tx = 0; tx < 32; ++tx) {
				cell = &gb->tilemaps[gb->background_tilemap].cells[ty][tx];

				for (int dy = 0; dy < 8; ++dy) {
					for (int dx = 0; dx < 8; ++dx) {
						int y = (8 * ty) + dy;
						int x = (8 * tx) + dx;
						int color = cell->tile->pixels[dy][dx];

						(*gb->dbg_background)[y][x] = cell->palette->colors[color];
					}
				}
			}
		}
	}

	if (gb->dbg_window) {
		for (int ty = 0; ty < 32; ++ty) {
			for (int tx = 0; tx < 32; ++tx) {
				cell = &gb->tilemaps[gb->window_tilemap].cells[ty][tx];

				for (int dy = 0; dy < 8; ++dy) {
					for (int dx = 0; dx < 8; ++dx) {
						int y = (8 * ty) + dy;
						int x = (8 * tx) + dx;
						int color = cell->tile->pixels[dy][dx];

						(*gb->dbg_window)[y][x] = cell->palette->colors[color];
					}
				}
			}
		}
	}

	if (gb->dbg_palettes) {
		for (int i = 0; i < 8; ++i) {
			for (int j = 0; j < 4; ++j) {
				int color;

				color = gb->bgp[i].colors[j];
				for (int dy = 0; dy < 8; ++dy) {
					for (int dx = 0; dx < 8; ++dx) {
						int y = (i * 10) + dy + 2;
						int x = (j * 10) + dx + 2;
						(*gb->dbg_palettes)[y][x] = color;
					}
				}

				color = gb->obp[i].colors[j];
				for (int dy = 0; dy < 8; ++dy) {
					for (int dx = 0; dx < 8; ++dx) {
						int y = (i * 10) + dy + 2;
						int x = (j * 10) + dx + 46;
						(*gb->dbg_palettes)[y][x] = color;
					}
				}
			}
		}
	}
}

static int sprite_qsort(const void *p1, const void *p2)
{
	const struct gameboy_sprite *lhs = p1;
	const struct gameboy_sprite *rhs = p2;

	if (lhs->x != rhs->x)
		return (lhs->x < rhs->x) ? -1 : 1;

	return (lhs < rhs) ? -1 : 1;
}

static void render_scanline(struct gameboy *gb)
{
	if (!gb->screen)
		return;

	uint8_t line[160];
	int y = gb->scanline;
	uint8_t dy;

	if (gb->sprites_unsorted)
		qsort(gb->sprites_sorted, 40, sizeof(void *), sprite_qsort);

	uint8_t window_start;
	if (gb->window_enabled && gb->scanline >= gb->wy)
		window_start = MAX(0, MIN(160, gb->wx));
	else
		window_start = 160;

	dy = y + gb->sy;
	for (int x = 0; x < window_start; ++x) {
		if (!gb->background_enabled)
			break;

		uint8_t dx = x + gb->sx;

		struct gameboy_background_cell *cell;
		cell = &gb->tilemaps[gb->background_tilemap].cells[dy / 8][dx / 8];

		uint8_t code = cell->tile->pixels[dy % 8][dx % 8];
		line[x] = code;
		(*gb->screen)[y][x] = cell->palette->colors[code];
	}

	dy = y - gb->wy;
	for (int x = window_start; x < 160; ++x) {
		uint8_t dx = x - gb->wx;

		struct gameboy_background_cell *cell;
		cell = &gb->tilemaps[gb->window_tilemap].cells[dy / 8][dx / 8];

		uint8_t code = cell->tile->pixels[dy % 8][dx % 8];
		line[x] = code;
		(*gb->screen)[y][x] = cell->palette->colors[code];
	}

	for (int i = 0; i < 40; ++i) {
		struct gameboy_sprite *spr = gb->sprites_sorted[i];

		dy = y - spr->y;
		if (dy >= gb->sprite_size)
			continue;

		struct gameboy_tile *tile = spr->tile;
		if (dy > 7)
			++tile; // 8x16 mode: use next tile

		uint8_t *row = tile->pixels[spr->flipy ? (7 - (dy % 8)) : (dy % 8)];

		for (int sx = 0; sx < 8; ++sx) {
			uint8_t dx = spr->x + sx;

			if (dx >= 160)
				continue;

			uint8_t code = row[spr->flipx ? (7 - sx) : sx];

			// Sprite color 0 is transparent
			if (!code)
				continue;

			// Low-priority sprites only prevail over bg color 0
			if (spr->priority && line[dx])
				continue;

			line[dx] = code;
			(*gb->screen)[y][dx] = spr->palette->colors[code];
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

	gb_callback(gb, &gb->on_vblank);
}

void lcd_init(struct gameboy *gb)
{
	for (int i = 0; i < 40; ++i)
		gb->sprites_sorted[i] = &gb->sprites[i];
	gb->sprites_unsorted = true;

	gb->sprite_size = 16;
	lcd_update_sprite_mode(gb, false);

	gb->tilemap_signed = true;
	lcd_update_tilemap_mode(gb, false);

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

		if (gb->hdma_enabled && gb->hdma_blocks_remaining && !gb->gdma)
			gb->hdma_blocks_queued = 1;

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
	gb->next_lcd_status_in = gb->cycles + 80;
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

uint8_t lcd_read_sprite(struct gameboy *gb, uint16_t offset)
{
	struct gameboy_sprite *spr = &gb->sprites[offset / 4];
	switch (offset % 4) {
	case 0:  return spr->y + 16;
	case 1:  return spr->x + 8;
	case 2:  return spr->tile_index;
	default: return spr->raw_flags;
	}
}

void lcd_refresh_sprite(struct gameboy *gb, struct gameboy_sprite *spr)
{
	spr->palette = &gb->obp[spr->palette_index];

	uint8_t val = spr->tile_index;
	if (gb->sprite_size == 16)
		spr->tile = &gb->tiles[spr->vram_bank][val & 0xFE];
	else
		spr->tile = &gb->tiles[spr->vram_bank][val];
}

void lcd_update_sprite(struct gameboy *gb, uint16_t offset, uint8_t val)
{
	struct gameboy_sprite *spr = &gb->sprites[offset / 4];
	switch (offset % 4) {
	case 0:
		spr->y = val - 16;
		gb->sprites_unsorted = true;
		break;

	case 1:
		spr->x = val - 8;
		gb->sprites_unsorted = true;
		break;

	case 2:
		spr->tile_index = val;
		lcd_refresh_sprite(gb, spr);
		break;

	case 3:
		spr->raw_flags = val;
		if (gb->gbc) {
			spr->palette_index = (val & BITS(0, 2));
			spr->vram_bank = !!(val & BIT(3));
		} else {
			spr->palette_index = !!(val & BIT(4));
		}
		spr->flipx = !!(val & BIT(5));
		spr->flipy = !!(val & BIT(6));
		spr->priority = !!(val & BIT(7));

		lcd_refresh_sprite(gb, spr);
		break;
	}
}

void lcd_update_sprite_mode(struct gameboy *gb, bool is_8x16)
{
	uint8_t new_sprite_size = is_8x16 ? 16 : 8;
	if (new_sprite_size == gb->sprite_size)
		return;
	gb->sprite_size = new_sprite_size;

	for (int i = 0; i < 40; ++i)
		lcd_refresh_sprite(gb, &gb->sprites[i]);
}

uint8_t lcd_read_tile(struct gameboy *gb, uint16_t offset)
{
	struct gameboy_tile *t = &gb->tiles[gb->vram_bank][offset / 16];

	return t->raw[offset % 16];
}

void lcd_update_tile(struct gameboy *gb, uint16_t offset, uint8_t val)
{
	struct gameboy_tile *t = &gb->tiles[gb->vram_bank][offset / 16];
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

uint8_t lcd_read_tilemap(struct gameboy *gb, uint16_t offset)
{
	struct gameboy_background_table *table = &gb->tilemaps[offset / 0x0400];

	struct gameboy_background_cell *cell = &table->cells_flat[offset % 0x0400];

	if (gb->vram_bank)
		return cell->raw_flags;
	else
		return cell->tile_index;
}

void lcd_refresh_tilemap(struct gameboy *gb, struct gameboy_background_cell *cell)
{
	cell->palette = &gb->bgp[cell->palette_index];

	int index = cell->tile_index;
	if (gb->tilemap_signed)
		index = 256 + (int8_t)index;

	cell->tile = &gb->tiles[cell->vram_bank][index];
}

void lcd_update_tilemap(struct gameboy *gb, uint16_t offset, uint8_t val)
{
	struct gameboy_background_cell *cell;

	cell = &gb->tilemaps[offset >= 0x0400].cells_flat[offset % 0x0400];

	if (gb->vram_bank) {
		cell->raw_flags = val;
		cell->palette_index = (val & BITS(0, 2));
		cell->vram_bank = !!(val & BIT(3));
		cell->flipx = !!(val & BIT(5));
		cell->flipy = !!(val & BIT(6));
		cell->priority = !!(val & BIT(7));
	} else {
		cell->tile_index = val;
	}

	lcd_refresh_tilemap(gb, cell);
}

void lcd_update_tilemap_mode(struct gameboy *gb, bool is_signed)
{
	if (gb->tilemap_signed == is_signed)
		return;
	gb->tilemap_signed = is_signed;

	for (int i = 0; i < 0x0400; ++i)
		lcd_refresh_tilemap(gb, &gb->tilemaps[0].cells_flat[i]);
	for (int i = 0; i < 0x0400; ++i)
		lcd_refresh_tilemap(gb, &gb->tilemaps[1].cells_flat[i]);
}
