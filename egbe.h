// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef EGBE_H
#define EGBE_H

#include "gameboy.h"

// ~28 event samples per second
// Also works well with games that sync every other VBlank (~140448)
#define EGBE_EVENT_CYCLES 150000

enum egbe_link_status {
	EGBE_LINK_DISCONNECTED = 0,
	EGBE_LINK_GUEST,
	EGBE_LINK_HOST,
};

struct egbe_gameboy {
	struct gameboy *gb;

	char *boot_path;
	char *cart_path;
	char *sram_path;

	char *state_path;
	char *state_path_end;
	char state_num;

	long start;
	long till;
	enum egbe_link_status status;
	bool xfer_pending;

	void (*cleanup)(struct egbe_gameboy *self);
	int (*connect)(struct egbe_gameboy *self);
	void (*tick)(struct egbe_gameboy *self);

	void *context;
};

void egbe_gameboy_init(struct egbe_gameboy *self, char *cart_path, char *boot_path);
void egbe_gameboy_cleanup(struct egbe_gameboy *self);

int egbe_gameboy_init_curl(struct egbe_gameboy *self, char *base_url);

void egbe_gameboy_set_savestate_num(struct egbe_gameboy *self, char n);

void egbe_gameboy_debug(struct egbe_gameboy *self);
int egbe_main(int argc, char **argv);

#endif