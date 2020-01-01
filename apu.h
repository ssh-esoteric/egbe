// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef EGBE_APU_H
#define EGBE_APU_H

#include "gameboy.h"

void apu_init(struct gameboy *gb);
void apu_sync(struct gameboy *gb);

void apu_enable(struct gameboy *gb);
void apu_disable(struct gameboy *gb);

void apu_trigger_square(struct gameboy *gb, struct apu_square_channel *square);
void apu_trigger_wave(struct gameboy *gb, struct apu_wave_channel *wave);
void apu_trigger_noise(struct gameboy *gb, struct apu_noise_channel *noise);

#endif
