// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef EGBE_H
#define EGBE_H

#include "gameboy.h"

// ~28 event samples per second
// Also works well with games that sync every other VBlank (~140448)
#define EGBE_EVENT_CYCLES 150000

enum egbe_link_flags {
	EGBE_LINK_DISCONNECTED = 0,
	EGBE_LINK_WAITING      = (1 << 0),
	EGBE_LINK_GUEST        = (1 << 1),
	EGBE_LINK_HOST         = (1 << 2),

	EGBE_LINK_MASK = EGBE_LINK_WAITING | EGBE_LINK_HOST | EGBE_LINK_GUEST,
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
	bool xfer_pending;

	void (*tick)(struct egbe_gameboy *self);

	int link_status;
	void *link_context;
	void (*link_cleanup)(struct egbe_gameboy *self);
	int (*link_connect)(struct egbe_gameboy *self);

};

void egbe_gameboy_init(struct egbe_gameboy *self, char *cart_path, char *boot_path);
void egbe_gameboy_cleanup(struct egbe_gameboy *self);

int egbe_gameboy_init_curl(struct egbe_gameboy *self, char *api_url);

void egbe_gameboy_set_savestate_num(struct egbe_gameboy *self, char n);

void egbe_gameboy_debug(struct egbe_gameboy *self);
int egbe_main(int argc, char **argv);

#endif
