// SPDX-License-Identifier: GPL-3.0-or-later
#include "apu.h"
#include "common.h"

#define MAX_APU_SAMPLES(gb) (sizeof(gb->apu_sample) / sizeof(gb->apu_sample[0]))

uint8_t duty_waves[4][8] = {
	{ 0, 0, 0, 0, 0, 0, 0, 1, },
	{ 1, 0, 0, 0, 0, 0, 0, 1, },
	{ 1, 0, 0, 0, 0, 1, 1, 1, },
	{ 0, 1, 1, 1, 1, 1, 1, 0, },
};

void apu_init(struct gameboy *gb)
{
	gb->apu_frame = 7; // TODO: Verify starting frame
	gb->next_apu_frame_in = 8192;

	gb->sq1.length.clocks_max = 64;
	gb->sq2.length.clocks_max = 64;
	gb->wave.length.clocks_max = 256;
	gb->noise.length.clocks_max = 64;

	apu_disable(gb);
	apu_enable(gb);
}

void apu_enable(struct gameboy *gb)
{
	if (gb->apu_enabled)
		return;

	gb->apu_enabled = true;
	gb->apu_frame = 0;
}

void apu_disable(struct gameboy *gb)
{
	if (!gb->apu_enabled)
		return;
	gb->apu_enabled = false;

	gb->sq1.super.enabled   = gb->sq1.super.dac   = false;
	gb->sq2.super.enabled   = gb->sq2.super.dac   = false;
	gb->wave.super.enabled  = gb->wave.super.dac  = false;
	gb->noise.super.enabled = gb->noise.super.dac = false;

}

static void clock_envelope(struct apu_envelope_module *env, struct apu_channel *c)
{
	if (!env->clocks_remaining)
		return;
	--env->clocks_remaining;

	env->volume += env->delta;
	if (env->volume > 15)
		env->volume = 15;
	else if (env->volume < 0)
		env->volume = 0;
}

static void clock_length(struct apu_length_module *len, struct apu_channel *c)
{
	if (!c->enabled || !len->is_terminal || !len->clocks_remaining)
		return;

	if (!--len->clocks_remaining)
		c->enabled = false;
}

static void clock_sweep(struct apu_sweep_module *sweep, struct apu_channel *c)
{
	if (!sweep->sweeps_remaining)
		return;
	--sweep->sweeps_remaining;

	int tmp = sweep->shadow;
	tmp += (sweep->shadow >> sweep->shift) * sweep->delta;
	tmp &= BITS(0, 10); // TODO: What happens if tmp underflowed?

	if (tmp > 2047)
		c->enabled = false;

	sweep->shadow = tmp;
	c->frequency = tmp;
}

void apu_sync(struct gameboy *gb)
{
	if (gb->cycles >= gb->next_apu_frame_in) {
		gb->next_apu_frame_in += 8192; // 512 Hz

		gb->apu_frame = (gb->apu_frame + 1) & BITS(0, 2);
		switch (gb->apu_frame) {
		case 2:
		case 6:
			clock_sweep(&gb->sq1.sweep, &gb->sq1.super);
			// SQ2 does not have a sweep module
			; // fallthrough
		case 0:
		case 4:
			clock_length(&gb->sq1.length, &gb->sq1.super);
			clock_length(&gb->sq2.length, &gb->sq2.super);
			clock_length(&gb->wave.length, &gb->wave.super);
			clock_length(&gb->noise.length, &gb->noise.super);
			break;
		case 7:
			clock_envelope(&gb->sq1.envelope, &gb->sq1.super);
			clock_envelope(&gb->sq2.envelope, &gb->sq2.super);
			clock_envelope(&gb->noise.envelope, &gb->noise.super);
			break;
		}
	}

	if (gb->cycles >= gb->sq1.super.next_tick_in) {
		gb->sq1.super.next_tick_in += gb->sq1.super.period;

		gb->sq1.duty_index = (gb->sq1.duty_index + 1) & BITS(0, 2);
	}

	if (gb->cycles >= gb->sq2.super.next_tick_in) {
		gb->sq2.super.next_tick_in += gb->sq2.super.period;

		gb->sq2.duty_index = (gb->sq2.duty_index + 1) & BITS(0, 2);
	}

	if (gb->cycles >= gb->wave.super.next_tick_in) {
		gb->wave.super.next_tick_in += gb->wave.super.period;

		gb->wave.index = ((gb->wave.index + 1) & 0x1F);
	}

	if (gb->cycles >= gb->noise.super.next_tick_in) {
		gb->noise.super.next_tick_in += gb->noise.super.period;

		uint8_t lo = gb->noise.lfsr & BITS(0, 1);

		gb->noise.lfsr >>= 1;
		if (lo == BIT(0) || lo == BIT(1))
			gb->noise.lfsr |= gb->noise.lfsr_mask;
	}

	if (gb->cycles >= gb->next_apu_sample) {
		gb->next_apu_sample += 95; // TODO: 44100 Hz =~ 95.1

		float left = 0;
		float right = 0;

		if (!gb->sq1.super.muted &&
		    gb->sq1.super.dac &&
		    duty_waves[gb->sq1.duty][gb->sq1.duty_index]) {
			float sample = gb->sq1.envelope.volume;
			left += (gb->sq1.super.output_left ? sample : 0);
			right += (gb->sq1.super.output_right ? sample : 0);
		}

		if (!gb->sq2.super.muted &&
		    gb->sq2.super.dac &&
		    duty_waves[gb->sq2.duty][gb->sq2.duty_index]) {
			float sample = gb->sq2.envelope.volume;
			left += (gb->sq2.super.output_left ? sample : 0);
			right += (gb->sq2.super.output_right ? sample : 0);
		}

		if (!gb->wave.super.muted &&
		    gb->wave.super.dac) {
			float sample = (gb->wave.samples[gb->wave.index] >> gb->wave.volume_shift);
			left += (gb->wave.super.output_left ? sample : 0);
			right += (gb->wave.super.output_right ? sample : 0);
		}

		if (!gb->noise.super.muted &&
		    gb->noise.super.dac &&
		    !(gb->noise.lfsr & BIT(0))) {
			float sample = gb->noise.envelope.volume;
			left += (gb->noise.super.output_left ? sample : 0);
			right += (gb->noise.super.output_right ? sample : 0);
		}

		gb->apu_sample[gb->apu_index++] = left  * gb->so1_volume / 256;
		gb->apu_sample[gb->apu_index++] = right * gb->so2_volume / 256;

		if (gb->apu_index >= MAX_APU_SAMPLES(gb)) {
			gb_callback(gb, &gb->on_apu_buffer_filled);

			gb->apu_index = 0;
		}
	}
}

static void trigger_envelope(struct apu_envelope_module *env)
{
	env->volume = env->volume_max;
	env->clocks_remaining = env->clocks_max;
}

static void trigger_length(struct apu_length_module *len)
{
	if (!len->clocks_remaining)
		len->clocks_remaining = len->clocks_max;
}

static void trigger_sweep(struct apu_sweep_module *sweep, struct apu_channel *super)
{
	sweep->shadow = super->frequency;
	sweep->sweeps_remaining = sweep->sweeps_max;
}

void apu_trigger_square(struct gameboy *gb, struct apu_square_channel *square)
{
	square->super.enabled = square->super.dac;

	trigger_envelope(&square->envelope);
	trigger_length(&square->length);
	trigger_sweep(&square->sweep, &square->super);
}

void apu_trigger_wave(struct gameboy *gb, struct apu_wave_channel *wave)
{
	wave->index = 0;

	wave->super.enabled = wave->super.dac;

	trigger_length(&wave->length);
}

void apu_trigger_noise(struct gameboy *gb, struct apu_noise_channel *noise)
{
	gb->noise.lfsr = ~0;

	noise->super.enabled = noise->super.dac;

	trigger_envelope(&noise->envelope);
	trigger_length(&noise->length);
}
