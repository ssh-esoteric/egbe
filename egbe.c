#include "common.h"
#include <SDL2/SDL.h>

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
	struct texture dbg_vram;
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
		520,
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
		SDL_RENDERER_TARGETTEXTURE // | SDL_RENDERER_PRESENTVSYNC
	);

	if (!v->renderer) {
		GBLOG("Failure in SDL_CreateRenderer: %s", SDL_GetError());
		return 1;
	}

	SDL_RenderClear(v->renderer);

	return texture_init(&v->screen, v->renderer)
	    || texture_init(&v->alt_screen, v->renderer)
	    || texture_init(&v->dbg_background, v->renderer)
	    || texture_init(&v->dbg_window, v->renderer)
	    || texture_init(&v->dbg_vram, v->renderer);
}

static void view_free(struct view *v)
{
	texture_free(&v->screen);
	texture_free(&v->alt_screen);
	texture_free(&v->dbg_background);
	texture_free(&v->dbg_window);
	texture_free(&v->dbg_vram);

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

	view_render_texture(v, &v->screen);
	view_render_texture(v, &v->alt_screen);
	view_render_texture(v, &v->dbg_background);
	view_render_texture(v, &v->dbg_window);
	view_render_texture(v, &v->dbg_vram);

	SDL_RenderPresent(v->renderer);
}

static int audio_init(struct audio *audio)
{
	int samples = 4096;
	int channels = 2;
	struct SDL_AudioSpec want = {
		.freq = 44100,
		.format = AUDIO_F32,
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

	SDL_QueueAudio(audio->device_id, gb->apu_sample, sizeof(float)*gb->apu_index);
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

int main(int argc, char **argv)
{
	if (argc < 2) {
		puts("Usage: egbe <ROM.gb> [<BOOT.bin>] [<SRAM.sram>]");
		return 0;
	}

	if (SDL_Init(SDL_INIT_EVERYTHING)) {
		GBLOG("Failed to initialize SDL: %s", SDL_GetError());
		return 1;
	}
	atexit(SDL_Quit);

	struct gameboy *gb = gameboy_alloc(GAMEBOY_SYSTEM_DMG);
	if (!gb)
		return 1;

	struct gameboy *alt_gb = NULL;
	if (getenv("SERIAL"))
		alt_gb = gameboy_alloc(GAMEBOY_SYSTEM_DMG);

	struct view view = {
		.screen = {
			.pixels = gb->screen,
			.rect = { .x = 180, .y = 264, .w = 160, .h = 144, },
		},
		.alt_screen = {
			.pixels = alt_gb ? alt_gb->screen : NULL,
			.rect = { .x = 348, .y = 264, .w = 160, .h = 144, },
		},
		.dbg_background = {
			.pixels = gb->dbg_background,
			.rect = { .x =   4, .y =   4, .w = 256, .h = 256, },
		},
		.dbg_window = {
			.pixels = gb->dbg_window,
			.rect = { .x = 264, .y =   4, .w = 256, .h = 256, },
		},
		.dbg_vram = {
			.pixels = gb->dbg_vram,
			.rect = { .x =   4, .y = 264, .w = 128, .h = 192, },
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

	// TODO: Temporary until the audio doesn't sound terrible
	gb->sq1.super.muted = true;
	gb->sq2.super.muted = true;
	gb->wave.super.muted = true;
	gb->noise.super.muted = true;
	gameboy_restart(gb);

	if (alt_gb) {
		alt_gb->sq1.super.muted = true;
		alt_gb->sq2.super.muted = true;
		alt_gb->wave.super.muted = true;
		alt_gb->noise.super.muted = true;
		gameboy_restart(alt_gb);
	}

	struct gameboy *curr_gb = gb;

	long joypad_ticks = 0;
	while (gb->cpu_status != GAMEBOY_CPU_CRASHED) {
		gameboy_tick(gb);
		if (alt_gb)
			gameboy_tick(alt_gb);

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
				}
				break;
			}
		}

		if (++joypad_ticks > 5000) {
			joypad_ticks = 0;

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
