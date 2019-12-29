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

static inline void set_cell_tile_index(struct gameboy *gb,
                                       struct gameboy_background_cell *cell,
                                       uint8_t val)
{
	int index = gb->tilemap_signed ? (256 + (int8_t)val) : val;

	cell->tile_index = val;
	cell->tile = &gb->tiles[cell->vram_bank][index];
}

static inline void set_sprite_tile_index(struct gameboy *gb,
                                         struct gameboy_sprite *sprite,
                                         uint8_t val)
{
	sprite->tile_index = val;
	if (gb->sprite_size == 16)
		sprite->tile = &gb->tiles[sprite->vram_bank][val & 0xFE];
	else
		sprite->tile = &gb->tiles[sprite->vram_bank][val];
}

static void render_debug(struct gameboy *gb)
{
	struct gameboy_background_cell *cell;
	struct gameboy_tile *tile;

	for (int ty = 0; ty < 24; ++ty) {
		for (int tx = 0; tx < 16; ++tx) {
			tile = &gb->tiles[0][(16 * ty) + tx];

			for (int dy = 0; dy < 8; ++dy) {
				for (int dx = 0; dx < 8; ++dx) {
					int y = (8 * ty) + dy;
					int x = (8 * tx) + dx;
					int color = tile->pixels[dy][dx];

					gb->dbg_vram[y][x] = monochrome.colors[color];
				}
			}
		}
	}

	for (int ty = 0; ty < 24; ++ty) {
		for (int tx = 0; tx < 16; ++tx) {
			tile = &gb->tiles[1][(16 * ty) + tx];

			for (int dy = 0; dy < 8; ++dy) {
				for (int dx = 0; dx < 8; ++dx) {
					int y = (8 * ty) + dy;
					int x = (8 * tx) + dx;
					int color = tile->pixels[dy][dx];

					gb->dbg_vram_gbc[y][x] = monochrome.colors[color];
				}
			}
		}
	}

	for (int ty = 0; ty < 32; ++ty) {
		for (int tx = 0; tx < 32; ++tx) {
			cell = &gb->background_tilemap->cells[ty][tx];

			for (int dy = 0; dy < 8; ++dy) {
				for (int dx = 0; dx < 8; ++dx) {
					int y = (8 * ty) + dy;
					int x = (8 * tx) + dx;
					int color = cell->tile->pixels[dy][dx];

					gb->dbg_background[y][x] = cell->palette->colors[color];
				}
			}
		}
	}

	for (int ty = 0; ty < 32; ++ty) {
		for (int tx = 0; tx < 32; ++tx) {
			cell = &gb->window_tilemap->cells[ty][tx];

			for (int dy = 0; dy < 8; ++dy) {
				for (int dx = 0; dx < 8; ++dx) {
					int y = (8 * ty) + dy;
					int x = (8 * tx) + dx;
					int color = cell->tile->pixels[dy][dx];

					gb->dbg_window[y][x] = cell->palette->colors[color];
				}
			}
		}
	}

	for (int i = 0; i < 8; ++i) {
		for (int j = 0; j < 4; ++j) {
			int color;

			color = gb->bgp[i].colors[j];
			for (int dy = 0; dy < 8; ++dy) {
				for (int dx = 0; dx < 8; ++dx) {
					int y = (i * 10) + dy + 2;
					int x = (j * 10) + dx + 2;
					gb->dbg_palettes[y][x] = color;
				}
			}

			color = gb->obp[i].colors[j];
			for (int dy = 0; dy < 8; ++dy) {
				for (int dx = 0; dx < 8; ++dx) {
					int y = (i * 10) + dy + 2;
					int x = (j * 10) + dx + 46;
					gb->dbg_palettes[y][x] = color;
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
		cell = &gb->background_tilemap->cells[dy / 8][dx / 8];

		uint8_t code = cell->tile->pixels[dy % 8][dx % 8];
		line[x] = code;
		gb->screen[y][x] = cell->palette->colors[code];
	}

	dy = y - gb->wy;
	for (int x = window_start; x < 160; ++x) {
		uint8_t dx = x - gb->wx;

		struct gameboy_background_cell *cell;
		cell = &gb->window_tilemap->cells[dy / 8][dx / 8];

		uint8_t code = cell->tile->pixels[dy % 8][dx % 8];
		line[x] = code;
		gb->screen[y][x] = cell->palette->colors[code];
	}

	for (int i = 0; i < 40; ++i) {
		struct gameboy_sprite *s = gb->sprites_sorted[i];

		dy = y - s->y;
		if (dy >= gb->sprite_size)
			continue;

		struct gameboy_tile *tile = s->tile;
		if (dy > 7)
			++tile; // 8x16 mode: use next tile

		uint8_t *row = tile->pixels[s->flipy ? (7 - (dy % 8)) : (dy % 8)];

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
			gb->screen[y][dx] = s->palette->colors[code];
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
	gb->background_tilemap = &gb->tilemaps[0];
	gb->window_tilemap = &gb->tilemaps[1];

	gb->tilemap_signed = true;
	lcd_update_tilemap_mode(gb, false);

	gb->sprite_size = 16;
	lcd_update_sprite_mode(gb, false);

	gb->sprites_unsorted = true;
	for (int i = 0; i < 40; ++i) {
		gb->sprites[i].palette = &gb->obp[0];

		gb->sprites_sorted[i] = &gb->sprites[i];
	}

	for (int i = 0; i < 1024; ++i) {
		gb->tilemaps[0].cells_flat[i].palette = &gb->bgp[0];
		gb->tilemaps[1].cells_flat[i].palette = &gb->bgp[0];
	}

	gb->lcd_enabled = true;
	lcd_disable(gb);

	for (int y = 0; y < 82; ++y) {
		for (int x = 0; x < 86; ++x) {
			if ((x + y) % 2)
				gb->dbg_palettes[y][x] = 0x00CCCCCC;
			else
				gb->dbg_palettes[y][x] = 0x00DDDDDD;
		}
	}
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
	struct gameboy_sprite *s = &gb->sprites[offset / 4];
	switch (offset % 4) {
	case 0:  return s->y + 16;
	case 1:  return s->x + 8;
	case 2:  return s->tile_index;
	default: return s->raw_flags;
	}
}

void lcd_update_sprite(struct gameboy *gb, uint16_t offset, uint8_t val)
{
	struct gameboy_sprite *s = &gb->sprites[offset / 4];
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
		set_sprite_tile_index(gb, s, val);
		break;

	case 3:
		s->raw_flags = val;
		if (gb->gbc) {
			s->palette_index = (val & BITS(0, 2));
			s->vram_bank = !!(val & BIT(3));
			set_sprite_tile_index(gb, s, s->tile_index);
		} else {
			s->palette_index = !!(val & BIT(4));
		}
		s->palette = &gb->obp[s->palette_index];
		s->flipx = !!(val & BIT(5));
		s->flipy = !!(val & BIT(6));
		s->priority = !!(val & BIT(7));
		break;
	}
}

void lcd_update_sprite_mode(struct gameboy *gb, bool is_8x16)
{
	uint8_t new_sprite_size = is_8x16 ? 16 : 8;
	if (new_sprite_size == gb->sprite_size)
		return;
	gb->sprite_size = new_sprite_size;

	for (int i = 0; i < 40; ++i) {
		struct gameboy_sprite *s = &gb->sprites[i];

		set_sprite_tile_index(gb, s, s->tile_index);
	}
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

		cell->palette = &gb->bgp[cell->palette_index];
		set_cell_tile_index(gb, cell, cell->tile_index);
	} else {
		set_cell_tile_index(gb, cell, val);
	}
}

void lcd_update_tilemap_mode(struct gameboy *gb, bool is_signed)
{
	if (gb->tilemap_signed == is_signed)
		return;
	gb->tilemap_signed = is_signed;

	struct gameboy_background_cell *cell;
	for (int i = 0; i < 0x0400; ++i) {
		cell = &gb->tilemaps[0].cells_flat[i];
		set_cell_tile_index(gb, cell, cell->tile_index);

		cell = &gb->tilemaps[1].cells_flat[i];
		set_cell_tile_index(gb, cell, cell->tile_index);
	}
}
