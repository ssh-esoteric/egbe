#include "cpu.h"
#include "serial.h"

void serial_sync(struct gameboy *gb)
{
	if (!gb->is_serial_pending || gb->cycles < gb->next_serial_in)
		return;

	gb->is_serial_pending = false;
	gb->sb = gb->next_sb;

	irq_flag(gb, GAMEBOY_IRQ_SERIAL);
}

void gameboy_start_serial(struct gameboy *gb, uint8_t xfer)
{
	gb->is_serial_pending = true;
	gb->next_serial_in = gb->cycles + (512 * 8); // 8 shifts at 8192Hz
	gb->next_sb = xfer;
}
