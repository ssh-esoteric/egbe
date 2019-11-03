#ifndef EGBE_LCD_H
#define EGBE_LCD_H

#include "gameboy.h"

void lcd_init(struct gameboy *gb);
void lcd_sync(struct gameboy *gb);

void lcd_enable(struct gameboy *gb);
void lcd_disable(struct gameboy *gb);

void lcd_update_tile(struct gameboy *gb, uint16_t offset, uint8_t val);
void lcd_update_tilemap(struct gameboy *gb, uint16_t offset, uint8_t val);

#endif
