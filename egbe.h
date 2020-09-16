// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef EGBE_H
#define EGBE_H

#include "gameboy.h"

// ~28 event samples per second
// Also works well with games that sync every other VBlank (~140448)
#define EGBE_EVENT_CYCLES 150000

// TODO: Make this dynamic
#define EGBE_MAX_PLUGINS 32

#define EGBE_PLUGIN_API 0UL

struct egbe_application;
struct egbe_gameboy;
struct egbe_plugin;

typedef int (*EGBE_PLUGIN_INIT)(
	struct egbe_application *app,
	struct egbe_plugin *plugin);

typedef void (*EGBE_PLUGIN_EXIT)(
	struct egbe_application *app,
	struct egbe_plugin *plugin);

typedef void (*EGBE_PLUGIN_CALL)(
	struct egbe_application *app,
	struct egbe_plugin *plugin,
	void (*call_next_plugin)(void *context),
	void *context);

typedef void (*EGBE_PLUGIN_START_DEBUGGER)(
	struct egbe_gameboy *self);

typedef int (*EGBE_PLUGIN_START_LINK_CLIENT)(
	struct egbe_gameboy *self,
	char *api_url);

enum egbe_link_flags {
	EGBE_LINK_DISCONNECTED = 0,
	EGBE_LINK_WAITING      = (1 << 0),
	EGBE_LINK_GUEST        = (1 << 1),
	EGBE_LINK_HOST         = (1 << 2),

	EGBE_LINK_MASK = EGBE_LINK_WAITING | EGBE_LINK_HOST | EGBE_LINK_GUEST,
};

struct egbe_application {
	struct egbe_plugin *plugins[EGBE_MAX_PLUGINS];
	size_t plugins_registered;

	int argc;
	char **argv;

	EGBE_PLUGIN_START_DEBUGGER start_debugger;

	EGBE_PLUGIN_START_LINK_CLIENT start_link_client;
};

struct egbe_plugin {
	void *dll_handle;
	long api;
	bool active;

	char *name;
	char *description;
	char *website;

	char *author;
	char *version;

	EGBE_PLUGIN_INIT init;
	EGBE_PLUGIN_EXIT exit;
	EGBE_PLUGIN_CALL call;

	EGBE_PLUGIN_START_DEBUGGER start_debugger;

	EGBE_PLUGIN_START_LINK_CLIENT start_link_client;
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

void egbe_gameboy_set_savestate_num(struct egbe_gameboy *self, char n);

#endif
