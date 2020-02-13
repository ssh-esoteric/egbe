// SPDX-License-Identifier: GPL-3.0-or-later
#include "cpu.h"
#include "timer.h"
#include "common.h"

void timer_set_frequency(struct gameboy *gb, uint8_t val)
{
	gb->timer_frequency_code = val & 0x03;

	switch (gb->timer_frequency_code) {
	case 0: gb->timer_frequency_cycles = 1024; break;
	case 1: gb->timer_frequency_cycles = 16;   break;
	case 2: gb->timer_frequency_cycles = 64;   break;
	case 3: gb->timer_frequency_cycles = 256;  break;
	}

	int mask = gb->timer_frequency_cycles - 1;

	long div = gb->cycles - gb->div_offset;
	long next = (div | mask) + 1;

	gb->next_timer_in += (next - div);
}

void timer_sync(struct gameboy *gb)
{
	if (!gb->timer_enabled || gb->cycles < gb->next_timer_in)
		return;

	gb->next_timer_in += gb->timer_frequency_cycles;

	if (++gb->timer_counter == 0) {
		gb->timer_counter = gb->timer_modulo;
		irq_flag(gb, GAMEBOY_IRQ_TIMER);
	}
}
