// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef EGBE_LCD_H
#define EGBE_LCD_H

#include "gameboy.h"

void lcd_init(struct gameboy *gb);
void lcd_sync(struct gameboy *gb);

void lcd_enable(struct gameboy *gb);
void lcd_disable(struct gameboy *gb);

void lcd_update_scanline(struct gameboy *gb, uint8_t scanline);

void lcd_update_palette_dmg(struct gameboy_palette *p, uint8_t val);
void lcd_update_palette_gbc(struct gameboy_palette *p, uint8_t index);

uint8_t lcd_read_sprite(struct gameboy *gb, uint16_t offset);
void lcd_update_sprite(struct gameboy *gb, uint16_t offset, uint8_t val);
void lcd_update_sprite_mode(struct gameboy *gb, bool is_8x16);

uint8_t lcd_read_tile(struct gameboy *gb, uint16_t offset);
void lcd_update_tile(struct gameboy *gb, uint16_t offset, uint8_t val);

uint8_t lcd_read_tilemap(struct gameboy *gb, uint16_t offset);
void lcd_update_tilemap(struct gameboy *gb, uint16_t offset, uint8_t val);
void lcd_update_tilemap_mode(struct gameboy *gb, bool is_signed);

#endif
