// SPDX-License-Identifier: GPL-3.0-or-later
#include "debugger.h"
#include "common.h"
#include <SDL2/SDL.h>

struct args {
	int argc;
	char **argv;
};

struct texture {
	void *pixels;
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

static int texture_init(struct texture *t, struct SDL_Renderer *r)
{
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

	return texture_init(&v->screen, v->renderer)
	    || texture_init(&v->alt_screen, v->renderer)
	    || texture_init(&v->dbg_background, v->renderer)
	    || texture_init(&v->dbg_window, v->renderer)
	    || texture_init(&v->dbg_palettes, v->renderer)
	    || texture_init(&v->dbg_vram, v->renderer)
	    || texture_init(&v->dbg_vram_gbc, v->renderer);
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

struct serial_context {
	struct gameboy *lhs;
	struct gameboy *rhs;
};

static void serial_sync(struct gameboy *gb, void *context)
{
	struct serial_context *serial = context;

	gameboy_start_serial(serial->lhs, serial->rhs->sb);
	gameboy_start_serial(serial->rhs, serial->lhs->sb);
}

int egbe_main(void *context)
{
	struct args *args = context;
	int argc = args->argc;
	char **argv = args->argv;

	if (argc < 2) {
		puts("Usage: egbe <ROM.gb> [<BOOT.bin>] [<SRAM.sram>]");
		return 0;
	}

	if (SDL_Init(SDL_INIT_EVERYTHING)) {
		GBLOG("Failed to initialize SDL: %s", SDL_GetError());
		return 1;
	}
	atexit(SDL_Quit);

	enum gameboy_system system = GAMEBOY_SYSTEM_DMG;
	if (getenv("GBC"))
		system = GAMEBOY_SYSTEM_GBC;

	struct gameboy *gb = gameboy_alloc(system);
	if (!gb)
		return 1;

	struct gameboy *alt_gb = NULL;
	if (getenv("SERIAL"))
		alt_gb = gameboy_alloc(system);

	struct view view = {
		.screen = {
			.pixels = gb->screen,
			.rect = { .x = 232, .y = 264, .w = 160, .h = 144, },
		},
		.alt_screen = {
			.pixels = alt_gb ? alt_gb->screen : NULL,
			.rect = { .x = 396, .y = 264, .w = 160, .h = 144, },
		},
		.dbg_background = {
			.pixels = gb->dbg_background,
			.rect = { .x = 136, .y =   4, .w = 256, .h = 256, },
		},
		.dbg_window = {
			.pixels = gb->dbg_window,
			.rect = { .x = 396, .y =   4, .w = 256, .h = 256, },
		},
		.dbg_palettes = {
			.pixels = gb->dbg_palettes,
			.rect = { .x = 136, .y = 264, .w =  86, .h = 82, },
		},
		.dbg_vram = {
			.pixels = gb->dbg_vram,
			.rect = { .x =   4, .y =   4, .w = 128, .h = 192, },
		},
		.dbg_vram_gbc = {
			.pixels = gb->dbg_vram_gbc,
			.rect = { .x =   4, .y = 200, .w = 128, .h = 192, },
		},
	};

	if (view_init(&view)) {
		GBLOG("Failed to initialize SDL view");
	} else {
		gb->on_vblank.callback = on_vblank;
		gb->on_vblank.context = &view;
	}

	struct audio audio = {
		.device_id = 0,
	};

	if (audio_init(&audio)) {
		GBLOG("Failed to initialize SDL audio");
	} else {
		gb->on_apu_buffer_filled.callback = queue_audio;
		gb->on_apu_buffer_filled.context = &audio;

		SDL_PauseAudioDevice(audio.device_id, 0);
	}

	struct serial_context serial_gbs = {
		.lhs = gb,
		.rhs = alt_gb,
	};
	if (alt_gb) {
		gb->on_serial_start.callback = serial_sync;
		gb->on_serial_start.context = &serial_gbs;

		alt_gb->on_serial_start.callback = serial_sync;
		alt_gb->on_serial_start.context = &serial_gbs;
	}

	gameboy_insert_cartridge(gb, argv[1]);
	if (alt_gb)
		gameboy_insert_cartridge(alt_gb, argv[1]);

	if (argc >= 3) {
		gameboy_insert_boot_rom(gb, argv[2]);
		if (alt_gb)
			gameboy_insert_boot_rom(alt_gb, argv[2]);
	}

	if (argc >= 4) {
		gameboy_load_sram(gb, argv[3]);
		if (alt_gb)
			gameboy_load_sram(alt_gb, argv[3]);
	}

	// gb->sq1.super.muted = true;
	// gb->sq2.super.muted = true;
	// gb->wave.super.muted = true;
	// gb->noise.super.muted = true;
	gameboy_restart(gb);

	if (alt_gb) {
		alt_gb->sq1.super.muted = true;
		alt_gb->sq2.super.muted = true;
		alt_gb->wave.super.muted = true;
		alt_gb->noise.super.muted = true;
		gameboy_restart(alt_gb);
	}

	struct gameboy *curr_gb = gb;

	size_t ss_len = strlen(argv[1]);
	size_t ss_num = 1;
	char *ss_buf = calloc(1, ss_len + 5);
	strncpy(ss_buf, argv[1], ss_len);
	strcpy(ss_buf + ss_len, ".ss1");

	long joypad_ticks = 0;
	while (gb->cpu_status != GAMEBOY_CPU_CRASHED) {
		gameboy_tick(gb);
		if (alt_gb)
			gameboy_tick(alt_gb);

		if (++joypad_ticks > 5000) {
			joypad_ticks = 0;

			SDL_Event event;
			while (SDL_PollEvent(&event)) {
				switch (event.type) {
				case SDL_QUIT:
					gb->cpu_status = GAMEBOY_CPU_CRASHED;
					break;

				case SDL_KEYDOWN:
					switch (event.key.keysym.sym) {
					case SDLK_q:
					case SDLK_ESCAPE:
						gb->cpu_status = GAMEBOY_CPU_CRASHED;
						break;
					case SDLK_1:
						toggle_channel(&gb->sq1.super, "Square 1");
						break;
					case SDLK_2:
						toggle_channel(&gb->sq2.super, "Square 2");
						break;
					case SDLK_3:
						toggle_channel(&gb->wave.super, "Wave");
						break;
					case SDLK_4:
						toggle_channel(&gb->noise.super, "Noise");
						break;
					case SDLK_LCTRL:
						if (alt_gb) {
							gameboy_update_joypad(curr_gb, NULL);
							curr_gb = (curr_gb == gb) ? alt_gb : gb;
						}
						break;

					case SDLK_F1:
					case SDLK_F2:
					case SDLK_F3:
					case SDLK_F4:
						ss_num = event.key.keysym.sym - SDLK_F1 + 1;
						ss_buf[ss_len + 3] = ss_num + '0';
						GBLOG("State %ld selected", ss_num);
						break;
					case SDLK_F5:
						if (!gameboy_save_state(gb, ss_buf))
							GBLOG("State %ld saved", ss_num);
						break;
					case SDLK_F8:
						if (!gameboy_load_state(gb, ss_buf))
							GBLOG("State %ld loaded", ss_num);
						SDL_ClearQueuedAudio(audio.device_id);
						break;

					case SDLK_h:
						gb->rtc_seconds += (60 * 60);
						break;
					case SDLK_j:
						gb->rtc_seconds += (60 * 60 * 24);
						break;

					case SDLK_g:
						debugger_open(gb);
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
			gameboy_update_joypad(curr_gb, &jp);
		}
	}

	if (argc >= 4)
		gameboy_save_sram(gb, argv[3]);

	gameboy_free(gb);
	if (alt_gb)
		gameboy_free(alt_gb);

	view_free(&view);
	audio_free(&audio);

	return 0;
}

int main(int argc, char **argv)
{
	struct args args = {argc, argv};

	return debugger_callback(egbe_main, &args);
}
