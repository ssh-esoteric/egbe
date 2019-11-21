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
	struct texture dbg_background;
	struct texture dbg_window;
	struct texture dbg_vram;
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

	return texture_init(&v->screen, v->renderer)
	    || texture_init(&v->dbg_background, v->renderer)
	    || texture_init(&v->dbg_window, v->renderer)
	    || texture_init(&v->dbg_vram, v->renderer);
}

static void view_free(struct view *v)
{
	texture_free(&v->screen);
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
	SDL_UpdateTexture(t->texture, NULL, t->pixels, sizeof(int) * t->rect.w);

	SDL_RenderCopy(v->renderer, t->texture, NULL, &t->rect);
}

static void on_vblank(struct gameboy *gb, void *context)
{
	struct view *v = context;

	SDL_RenderClear(v->renderer);

	view_render_texture(v, &v->screen);
	view_render_texture(v, &v->dbg_background);
	view_render_texture(v, &v->dbg_window);
	view_render_texture(v, &v->dbg_vram);

	SDL_RenderPresent(v->renderer);
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		puts("Usage: egbe <ROM.gb> [<BOOT.bin>]");
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

	struct view view = {
		.screen = {
			.pixels = gb->screen,
			.rect = { .x = 264, .y = 264, .w = 160, .h = 144, },
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

	gameboy_insert_cartridge(gb, argv[1]);
	if (argc >= 3)
		gameboy_insert_boot_rom(gb, argv[2]);

	gameboy_restart(gb);
	long next_joypad_in = 0;
	while (gb->cpu_status != GAMEBOY_CPU_CRASHED) {
		gameboy_tick(gb);

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
				}
				break;
			}
		}

		if (gb->cycles > next_joypad_in) {
			next_joypad_in += 20000;

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
			gameboy_update_joypad(gb, &jp);
		}
	}

	gameboy_free(gb);

	view_free(&view);

	return 0;
}
