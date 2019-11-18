#include "common.h"
#include "cpu.h"
#include "timer.h"

void timer_set_frequency(struct gameboy *gb, uint8_t val)
{
	gb->timer_frequency_code = val & 0x03;

	switch (gb->timer_frequency_code) {
	case 0: gb->timer_frequency_cycles = 1024; break;
	case 1: gb->timer_frequency_cycles = 16;   break;
	case 2: gb->timer_frequency_cycles = 64;   break;
	case 3: gb->timer_frequency_cycles = 256;  break;
	}

	long base = gb->next_div_in - gb->cycles;

	gb->next_timer_in = base + gb->timer_frequency_cycles;
}

void timer_sync(struct gameboy *gb)
{
	if (gb->cycles >= gb->next_div_in) {
		++gb->div;
		gb->next_div_in += 256;
	}

	if (!gb->timer_enabled || gb->cycles < gb->next_timer_in)
		return;

	gb->next_timer_in += gb->timer_frequency_cycles;

	if (++gb->timer_counter == 0) {
		gb->timer_counter = gb->timer_modulo;
		irq_flag(gb, GAMEBOY_IRQ_TIMER);
	}
}
