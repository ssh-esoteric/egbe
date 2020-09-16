// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include "egbe.h"
#include "common.h"
#include <dlfcn.h>
#include <glob.h>
#include <libgen.h>
#include <limits.h>
#include <SDL2/SDL.h>
#include <string.h>

char PLUGIN_UNSPECIFIED[] = "<Unspecified>";

struct texture {
	int *pixels;
	struct SDL_Texture *texture;
	struct SDL_Rect rect;
};

struct view {
	struct SDL_Window *window;
	struct SDL_Renderer *renderer;

	struct texture screen;
	struct texture alt_screen;
	struct texture dbg_background;
	struct texture dbg_window;
	struct texture dbg_palettes;
	struct texture dbg_vram;
	struct texture dbg_vram_gbc;
};

struct audio {
	SDL_AudioDeviceID device_id;
};

static int texture_init(struct texture *t, struct SDL_Renderer *r, size_t size)
{
	t->pixels = calloc(1, size);
	if (!t->pixels) {
		GBLOG("Unable to allocate texture: %m");
		return errno;
	}

	t->texture = SDL_CreateTexture(
		r,
		SDL_PIXELFORMAT_RGB888,
		SDL_TEXTUREACCESS_STREAMING,
		t->rect.w,
		t->rect.h
	);

	if (!t->texture) {
		GBLOG("Failure in SDL_CreateTexture: %s", SDL_GetError());
		return 1;
	}

	return 0;
}

static void texture_free(struct texture *t)
{
	free(t->pixels);
	t->pixels = NULL;

	if (t->texture)
		SDL_DestroyTexture(t->texture);
}

static int view_init(struct view *v)
{
	v->window = SDL_CreateWindow(
		"EGBE",
		SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED,
		660,
		520,
		SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS
	);

	if (!v->window) {
		GBLOG("Failure in SDL_CreateWindow: %s", SDL_GetError());
		return 1;
	}

	v->renderer = SDL_CreateRenderer(
		v->window,
		-1,
		SDL_RENDERER_TARGETTEXTURE | SDL_RENDERER_PRESENTVSYNC
	);

	if (!v->renderer) {
		GBLOG("Failure in SDL_CreateRenderer: %s", SDL_GetError());
		return 1;
	}

	struct gameboy *gb;

	return texture_init(&v->screen, v->renderer, sizeof(*gb->screen))
	    || texture_init(&v->alt_screen, v->renderer, sizeof(*gb->screen))
	    || texture_init(&v->dbg_background, v->renderer, sizeof(*gb->dbg_background))
	    || texture_init(&v->dbg_window, v->renderer, sizeof(*gb->dbg_window))
	    || texture_init(&v->dbg_palettes, v->renderer, sizeof(*gb->dbg_palettes))
	    || texture_init(&v->dbg_vram, v->renderer, sizeof(*gb->dbg_vram))
	    || texture_init(&v->dbg_vram_gbc, v->renderer, sizeof(*gb->dbg_vram_gbc));
}

static void view_free(struct view *v)
{
	texture_free(&v->screen);
	texture_free(&v->alt_screen);
	texture_free(&v->dbg_background);
	texture_free(&v->dbg_window);
	texture_free(&v->dbg_palettes);
	texture_free(&v->dbg_vram);
	texture_free(&v->dbg_vram_gbc);

	if (v->renderer)
		SDL_DestroyRenderer(v->renderer);

	if (v->window)
		SDL_DestroyWindow(v->window);
}

static void view_render_texture(struct view *v, struct texture *t)
{
	if (!t->pixels)
		return;

	SDL_UpdateTexture(t->texture, NULL, t->pixels, sizeof(int) * t->rect.w);

	SDL_RenderCopy(v->renderer, t->texture, NULL, &t->rect);
}

static void on_vblank(struct gameboy *gb, void *context)
{
	struct view *v = context;

	SDL_RenderClear(v->renderer);

	view_render_texture(v, &v->screen);
	view_render_texture(v, &v->alt_screen);
	view_render_texture(v, &v->dbg_background);
	view_render_texture(v, &v->dbg_window);
	view_render_texture(v, &v->dbg_palettes);
	view_render_texture(v, &v->dbg_vram);
	view_render_texture(v, &v->dbg_vram_gbc);

	SDL_RenderPresent(v->renderer);
}

static int audio_init(struct audio *audio)
{
	int samples = 4096;
	int channels = 2;
	struct SDL_AudioSpec want = {
		.freq = 48000,
		.format = AUDIO_S32,
		.channels = channels,
		.samples = samples * channels,
		// .callback = NULL,
		// .userdata = NULL,
	};
	struct SDL_AudioSpec have;
	audio->device_id = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);

	if (!audio->device_id) {
		GBLOG("Failed to initialize SDL audio: %s", SDL_GetError());
		return 1;
	} else if (want.format != have.format) {
		GBLOG("Audio format mismatch (got %d; need %d)",
		      want.format, have.format);
		return 1;
	}

	return 0;
}

static void audio_free(struct audio *audio)
{
	if (audio->device_id) {
		SDL_ClearQueuedAudio(audio->device_id);
		SDL_CloseAudioDevice(audio->device_id);
	}
}

static void queue_audio(struct gameboy *gb, void *context)
{
	struct audio *audio = context;

	int buf[MAX_APU_SAMPLES][2];

	for (size_t i = 0; i < gb->apu_index; ++i) {
		struct gameboy_audio_sample *left = &gb->apu_samples[i][0];
		struct gameboy_audio_sample *right = &gb->apu_samples[i][1];

		buf[i][0] = (left->sq1   * !gb->sq1.super.muted)
			  + (left->sq2   * !gb->sq2.super.muted)
			  + (left->wave  * !gb->wave.super.muted)
			  + (left->noise * !gb->noise.super.muted);
		buf[i][0] *= left->volume;

		buf[i][1] = (right->sq1   * !gb->sq1.super.muted)
			  + (right->sq2   * !gb->sq2.super.muted)
			  + (right->wave  * !gb->wave.super.muted)
			  + (right->noise * !gb->noise.super.muted);
		buf[i][1] *= right->volume;

		buf[i][0] <<= 20;
		buf[i][1] <<= 20;
	}

	SDL_QueueAudio(audio->device_id, buf, gb->apu_index * sizeof(int) * 2);
}

static void toggle_channel(struct apu_channel *super, char *name)
{
	super->muted = !super->muted;
	GBLOG("APU: %s %s", super->muted ? "Muted" : "Unmuted", name);
}

static void local_solo_tick(struct egbe_gameboy *self)
{
	self->till = self->gb->cycles + EGBE_EVENT_CYCLES;
	while (self->gb->cycles < self->till)
		gameboy_tick(self->gb);
}

static void local_serial_interrupt(struct gameboy *gb, void *context)
{
	struct egbe_gameboy *self = context;

	self->till = gb->cycles;
	self->xfer_pending = true;
}

static void local_serial_tick(struct egbe_gameboy *host)
{
	struct egbe_gameboy *guest = host->link_context;

	// Note that host->till could be set early from local_serial_interrupt
	host->till = host->gb->cycles + EGBE_EVENT_CYCLES;
	while (host->gb->cycles < host->till)
		gameboy_tick(host->gb);

	guest->till = host->till;
	while (guest->gb->cycles < guest->till)
		gameboy_tick(guest->gb);

	if (host->xfer_pending) {
		gameboy_start_serial(host->gb, guest->gb->sb);
		gameboy_start_serial(guest->gb, host->gb->sb);
		host->xfer_pending = false;
	}
}

void egbe_gameboy_init(struct egbe_gameboy *self, char *cart_path, char *boot_path)
{
	self->link_status = EGBE_LINK_DISCONNECTED;
	self->start = 0L;
	self->till = 0L;

	self->cart_path = cart_path ? strdup(cart_path) : NULL;
	self->boot_path = boot_path ? strdup(boot_path) : NULL;
	self->sram_path = NULL;
	self->state_path = NULL;

	if (self->cart_path) {
		size_t len = strlen(cart_path);

		self->sram_path = calloc(1, len + 6);
		strncpy(self->sram_path, cart_path, len);
		strcpy(self->sram_path + len, ".sram");

		self->state_path = calloc(1, len + 5);
		self->state_path_end = self->state_path + len + 3;
		self->state_num = 1;
		strncpy(self->state_path, cart_path, len);
		strcpy(self->state_path + len, ".ss1");
	}

	if (self->boot_path)
		gameboy_insert_boot_rom(self->gb, self->boot_path);
	if (self->cart_path)
		gameboy_insert_cartridge(self->gb, self->cart_path);
	if (self->sram_path)
		gameboy_load_sram(self->gb, self->sram_path);

	gameboy_restart(self->gb);
}

void egbe_gameboy_cleanup(struct egbe_gameboy *self)
{
	if (self->gb)
		gameboy_free(self->gb);

	if (self->link_cleanup)
		self->link_cleanup(self);

	free(self->boot_path);
	free(self->cart_path);
	free(self->sram_path);
	free(self->state_path);
}

void egbe_gameboy_set_savestate_num(struct egbe_gameboy *self, char n)
{
	self->state_num = n;
	*self->state_path_end = n + '0';
}

static void link_connect(struct egbe_gameboy *self)
{
	if (self->link_status & (EGBE_LINK_HOST | EGBE_LINK_GUEST)) {
		GBLOG("GB already registered as a %s",
		      self->link_status & EGBE_LINK_HOST ? "host" : "guest");
		return;
	}

	if (!self->link_connect) {
		GBLOG("No link cable handler configured");
		return;
	}

	GBLOG("Attempting to start link cable");
	int rc = self->link_connect(self);
	if (rc)
		GBLOG("Failed to connect link cable: %d", rc);
}

static void egbe_main(struct egbe_application *app)
{
	int argc = app->argc;
	char **argv = app->argv;

	if (SDL_Init(SDL_INIT_EVERYTHING)) {
		GBLOG("Failed to initialize SDL: %s", SDL_GetError());
		return;
	}
	atexit(SDL_Quit);

	enum gameboy_system system = GAMEBOY_SYSTEM_DMG;
	if (getenv("GBC"))
		system = GAMEBOY_SYSTEM_GBC;

	struct egbe_gameboy host = {
		.gb = gameboy_alloc(system),
		.tick = local_solo_tick,
	};
	struct egbe_gameboy guest = { 0 };
	struct egbe_gameboy *focus = &host;

	if (!host.gb)
		return;

	char *serial = getenv("SERIAL");

	if (app->start_link_client) {
		app->start_link_client(&host, getenv("SERIAL_URL"));
	} else if (serial && strcmp(serial, "local") == 0) {
		// TODO: Move this section to its own "start_link_client" hook?

		guest.gb = gameboy_alloc(system);
		if (!guest.gb)
			return;

		// TODO: host.status and guest.status should be used somewhere
		host.tick = local_serial_tick;
		host.link_context = &guest;

		host.gb->on_serial_start.callback = local_serial_interrupt;
		host.gb->on_serial_start.context = &host;
	}

	char *host_cart = getenv("CART1") ?: getenv("CART");
	char *host_boot = getenv("BOOT1") ?: getenv("BOOT");

	if (!host_cart && argc >= 2)
		host_cart = argv[1];
	if (!host_cart)
		GBLOG("Warning: no ROM file provided");

	if (!host_boot && argc >= 3)
		host_boot = argv[2];
	if (!host_boot)
		GBLOG("Warning: no boot ROM file provided");

	egbe_gameboy_init(&host, host_cart, host_boot);

	if (guest.gb) {
		char *guest_cart = getenv("CART2") ?: host_cart;
		char *guest_boot = getenv("BOOT2") ?: host_boot;

		if (!guest_cart)
			GBLOG("Warning: no ROM file provided to guest");

		if (!guest_boot)
			GBLOG("Warning: no boot ROM file provided to guest");

		egbe_gameboy_init(&guest, guest_cart, guest_boot);
	}

	struct view view = {
		.screen = {
			.rect = { .x = 232, .y = 264, .w = 160, .h = 144, },
		},
		.alt_screen = {
			.rect = { .x = 396, .y = 264, .w = 160, .h = 144, },
		},
		.dbg_background = {
			.rect = { .x = 136, .y =   4, .w = 256, .h = 256, },
		},
		.dbg_window = {
			.rect = { .x = 396, .y =   4, .w = 256, .h = 256, },
		},
		.dbg_palettes = {
			.rect = { .x = 136, .y = 264, .w =  86, .h = 82, },
		},
		.dbg_vram = {
			.rect = { .x =   4, .y =   4, .w = 128, .h = 192, },
		},
		.dbg_vram_gbc = {
			.rect = { .x =   4, .y = 200, .w = 128, .h = 192, },
		},
	};

	if (view_init(&view)) {
		GBLOG("Failed to initialize SDL view");
	} else {
		host.gb->on_vblank.callback = on_vblank;
		host.gb->on_vblank.context = &view;

		host.gb->screen = (void *)view.screen.pixels;
		host.gb->dbg_background = (void *)view.dbg_background.pixels;
		host.gb->dbg_window = (void *)view.dbg_window.pixels;
		host.gb->dbg_palettes = (void *)view.dbg_palettes.pixels;
		host.gb->dbg_vram = (void *)view.dbg_vram.pixels;
		host.gb->dbg_vram_gbc = (void *)view.dbg_vram_gbc.pixels;

		for (int y = 0; y < 82; ++y) {
			for (int x = 0; x < 86; ++x) {
				if ((x + y) % 2)
					(*host.gb->dbg_palettes)[y][x] = 0x00CCCCCC;
				else
					(*host.gb->dbg_palettes)[y][x] = 0x00DDDDDD;
			}
		}

		if (guest.gb)
			guest.gb->screen = (void *)view.alt_screen.pixels;
	}

	struct audio audio = {
		.device_id = 0,
	};

	if (audio_init(&audio)) {
		GBLOG("Failed to initialize SDL audio");
	} else {
		host.gb->on_apu_buffer_filled.callback = queue_audio;
		host.gb->on_apu_buffer_filled.context = &audio;

		SDL_PauseAudioDevice(audio.device_id, 0);
	}

	if (getenv("MUTED")) {
		host.gb->sq1.super.muted = true;
		host.gb->sq2.super.muted = true;
		host.gb->wave.super.muted = true;
		host.gb->noise.super.muted = true;
	}

	if (guest.gb) {
		guest.gb->sq1.super.muted = true;
		guest.gb->sq2.super.muted = true;
		guest.gb->wave.super.muted = true;
		guest.gb->noise.super.muted = true;
	}

	while (focus->gb->cpu_status != GAMEBOY_CPU_CRASHED) {

		host.tick(&host);

		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			switch (event.type) {
			case SDL_QUIT:
				focus->gb->cpu_status = GAMEBOY_CPU_CRASHED;
				break;

			case SDL_KEYDOWN:
				switch (event.key.keysym.sym) {
				case SDLK_l:
					link_connect(focus);
					break;
				case SDLK_q:
				case SDLK_ESCAPE:
					focus->gb->cpu_status = GAMEBOY_CPU_CRASHED;
					break;
				case SDLK_1:
					toggle_channel(&host.gb->sq1.super, "Square 1");
					break;
				case SDLK_2:
					toggle_channel(&host.gb->sq2.super, "Square 2");
					break;
				case SDLK_3:
					toggle_channel(&host.gb->wave.super, "Wave");
					break;
				case SDLK_4:
					toggle_channel(&host.gb->noise.super, "Noise");
					break;
				case SDLK_LCTRL:
					if (guest.gb) {
						gameboy_update_joypad(focus->gb, NULL);
						focus = (focus == &host) ? &guest : &host;
					}
					break;

				case SDLK_F1:
				case SDLK_F2:
				case SDLK_F3:
				case SDLK_F4:
					egbe_gameboy_set_savestate_num(focus, event.key.keysym.sym - SDLK_F1 + 1);
					GBLOG("State %d selected", focus->state_num);
					break;
				case SDLK_F5:
					if (!gameboy_save_state(focus->gb, focus->state_path))
						GBLOG("State %d saved", focus->state_num);
					break;
				case SDLK_F8:
					if (!gameboy_load_state(focus->gb, focus->state_path))
						GBLOG("State %d loaded", focus->state_num);
					SDL_ClearQueuedAudio(audio.device_id);
					break;

				case SDLK_h:
					focus->gb->rtc_seconds += (60 * 60);
					break;
				case SDLK_j:
					focus->gb->rtc_seconds += (60 * 60 * 24);
					break;

				case SDLK_g:
					if (app->start_debugger)
						app->start_debugger(focus);
					else
						GBLOG("No debugger configured");
					break;
				}
				break;
			}
		}

		const uint8_t *keys = SDL_GetKeyboardState(NULL);

		struct gameboy_joypad jp = {
			.right = keys[SDL_SCANCODE_RIGHT],
			.left  = keys[SDL_SCANCODE_LEFT],
			.up    = keys[SDL_SCANCODE_UP],
			.down  = keys[SDL_SCANCODE_DOWN],

			.a      = keys[SDL_SCANCODE_A],
			.b      = keys[SDL_SCANCODE_D],
			.select = keys[SDL_SCANCODE_RSHIFT],
			.start  = keys[SDL_SCANCODE_RETURN],
		};
		gameboy_update_joypad(focus->gb, &jp);
	}

	if (host.gb->sram)
		gameboy_save_sram(host.gb, host.sram_path);

	if (guest.gb && guest.gb->sram && strcmp(host.sram_path, guest.sram_path) != 0)
		gameboy_save_sram(guest.gb, guest.sram_path);

	egbe_gameboy_cleanup(&host);
	egbe_gameboy_cleanup(&guest);

	view_free(&view);
	audio_free(&audio);
}

static struct egbe_plugin *find_plugin(struct egbe_application *app, char *name)
{
	if (!name)
		return NULL;

	for (int i = 0; i < EGBE_MAX_PLUGINS; ++i) {
		struct egbe_plugin *tmp = app->plugins[i];

		if (tmp && strcmp(tmp->name, name) == 0)
			return tmp;
	}

	return NULL;
}

static struct egbe_plugin *load_plugin(char *path)
{
	char buf[PATH_MAX];

	char *abs = realpath(path, buf);

	int flags = RTLD_LAZY | RTLD_LOCAL;

	void *dll = dlopen(buf, flags);
	if (!dll) {
		GBLOG("Error loading plugin from %s: %s", path, dlerror());
		return NULL;
	}

	struct egbe_plugin *plugin = calloc(1, sizeof(*plugin));
	if (!plugin) {
		GBLOG("Failed to calloc plugin: %m");
		return NULL;
	}

	long *api = dlsym(dll, "egbe_plugin_export_api");
	if (!api) {
		GBLOG("Could not find egbe_plugin_export symbol in %s", path);
		return NULL;
	}
	plugin->api = *api;
	plugin->dll_handle = dll;

	char **str;
	str = dlsym(dll, "egbe_plugin_export_name");
	plugin->name = str ? *str : PLUGIN_UNSPECIFIED;

	str = dlsym(dll, "egbe_plugin_export_description");
	plugin->description = str ? *str : PLUGIN_UNSPECIFIED;

	str = dlsym(dll, "egbe_plugin_export_website");
	plugin->website = str ? *str : PLUGIN_UNSPECIFIED;

	str = dlsym(dll, "egbe_plugin_export_author");
	plugin->author = str ? *str : PLUGIN_UNSPECIFIED;

	str = dlsym(dll, "egbe_plugin_export_version");
	plugin->version = str ? *str : PLUGIN_UNSPECIFIED;

	EGBE_PLUGIN_INIT *init = dlsym(dll, "egbe_plugin_export_init");
	if (init)
		plugin->init = *init;

	EGBE_PLUGIN_EXIT *exit = dlsym(dll, "egbe_plugin_export_exit");
	if (exit)
		plugin->exit = *exit;

	EGBE_PLUGIN_CALL *call = dlsym(dll, "egbe_plugin_export_call");
	if (call)
		plugin->call = *call;

	EGBE_PLUGIN_START_DEBUGGER *dbg = dlsym(dll, "egbe_plugin_export_start_debugger");
	if (dbg)
		plugin->start_debugger = *dbg;

	EGBE_PLUGIN_START_LINK_CLIENT *link = dlsym(dll, "egbe_plugin_export_start_link_client");
	if (link)
		plugin->start_link_client = *link;

	if (getenv("PLUGIN_DEBUG")) {
		GBLOG("\n"
		      "\tPath: %s\n"
		      "\tName: %s\n"
		      "\tDescription: %s\n"
		      "\tWebsite: %s\n"
		      "\tAuthor: %s\n"
		      "\tVersion: %s\n"
		      "\tPlugin API:\n"
		      "%s"
		      "%s"
		      "%s"
		      "%s"
		      "%s",
		      buf,
		      plugin->name, plugin->description, plugin->website,
		      plugin->author, plugin->version,
		      (plugin->init ? "\t- init()\n" : ""),
		      (plugin->exit ? "\t- exit()\n" : ""),
		      (plugin->call ? "\t- call()\n" : ""),
		      (plugin->start_debugger ? "\t- start_debugger()\n" : ""),
		      (plugin->start_link_client ? "\t- start_link_client()\n" : "")
		);
	}

	return plugin;
}

struct plugin_call_context {
	struct egbe_application *app;

	size_t iter;
};

static void call_into_egbe_main(void *tmp)
{
	struct plugin_call_context *context = tmp;
	struct egbe_application *app = context->app;

	while (context->iter < app->plugins_registered) {
		struct egbe_plugin *plugin = app->plugins[context->iter++];

		if (plugin->active && plugin->call) {
			plugin->call(app, plugin, call_into_egbe_main, context);
			return;
		}
	}

	egbe_main(app);
}

int main(int argc, char **argv)
{
	struct egbe_application app = {
		.argc = argc,
		.argv = argv,
	};

	glob_t search = { 0 };

	char *dir = dirname(realpath(argv[0], NULL));
	char plugin_glob[PATH_MAX] = { 0 };
	strcat(plugin_glob, dir);
	strcat(plugin_glob, "/plugins/*/*.so");
	free(dir);

	int flags = 0;//GLOB_NOSORT;
	int rc = glob(plugin_glob, flags, NULL, &search);
	if (!rc) {
		for (size_t i = 0; i < search.gl_pathc; ++i) {
			char *path = search.gl_pathv[i];

			struct egbe_plugin *p = load_plugin(path);
			if (!p)
				continue;

			app.plugins[app.plugins_registered++] = p;

			if (app.plugins_registered == EGBE_MAX_PLUGINS) {
				GBLOG("Hit max plugin limit of %d", EGBE_MAX_PLUGINS);
				break;
			}
		}
	}
	globfree(&search);

	struct egbe_plugin *plugin = NULL;

	plugin = find_plugin(&app, getenv("DEBUG"));
	if (plugin) {
		if (plugin->start_debugger) {
			app.start_debugger = plugin->start_debugger;
			plugin->active = true;

			GBLOG("Loaded \"%s\" as debugger", plugin->name);
		} else {
			GBLOG("\"%s\" is not a debugger plugin", plugin->name);
		}
	}

	plugin = find_plugin(&app, getenv("SERIAL"));
	if (plugin) {
		if (plugin->start_link_client) {
			app.start_link_client = plugin->start_link_client;
			plugin->active = true;

			GBLOG("Loaded \"%s\" as link client", plugin->name);
		} else {
			GBLOG("\"%s\" is not a link client plugin", plugin->name);
		}
	}

	for (size_t i = 0; i < app.plugins_registered; ++i) {
		struct egbe_plugin *tmp = app.plugins[i];

		if (tmp && tmp->active && tmp->init)
			tmp->init(&app, tmp);
	}

	struct plugin_call_context context = {
		.app = &app,
		.iter = 0,
	};
	call_into_egbe_main(&context);

	app.start_debugger = NULL;
	app.start_link_client = NULL;

	do {
		struct egbe_plugin *tmp = app.plugins[app.plugins_registered - 1];
		if (!tmp)
			continue;

		if (tmp->active && tmp->exit)
			tmp->exit(&app, tmp);

		if (tmp->dll_handle) {
			if (dlclose(tmp->dll_handle))
				GBLOG("Error unloading %s plugin: %s", tmp->name, dlerror());

			free(tmp);
		}

		app.plugins[app.plugins_registered - 1] = NULL;
	} while (--app.plugins_registered);

	return 0;
}
