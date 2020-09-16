// SPDX-License-Identifier: GPL-3.0-or-later
#include "egbe_plugin_api.h"
#include <time.h> // Required before ruby.h to declare struct timespec
#include <ruby.h>
#include <ruby/encoding.h>

VALUE mEGBE;
VALUE cGB;
VALUE cAccessor;

#define GB_OFFSETOF(attr) offsetof(struct gameboy, attr)
#define GB_TYPEOF(attr) (size_t)_Generic((((struct gameboy *)NULL)->attr), \
	bool:     1, \
	int8_t:   2, \
	int16_t:  3, \
	int32_t:  4, \
	int64_t:  5, \
	uint8_t:  6, \
	uint16_t: 7, \
	uint32_t: 8, \
	uint64_t: 9)

const char *attr_fmt[] = {
	"bool %d",
	"i8 %d",
	"i16 %d",
	"i32 %d",
	"i64 %ld",
	"u8 0x%02X",
	"u16 0x%04X",
	"u32 0x%X",
	"u64 0x%X",
};

static VALUE cGB_initialize(VALUE self)
{
	struct gameboy *gb = rb_data_object_get(self);

	VALUE gb_accessors = rb_iv_get(cGB, "@accessors");
	size_t max = FIX2LONG(rb_funcall(gb_accessors, rb_intern("length"), 0));

	ID ary = rb_intern("[]");
	for (size_t i = 0; i < max; ++i) {
		VALUE klass = rb_funcall(gb_accessors, ary, 1, LONG2FIX(i));

		size_t offset = NUM2ULONG(rb_iv_get(klass, "@c_offset"));
		void *ptr = (char *)gb + offset;

		VALUE obj = Data_Wrap_Struct(klass, NULL, NULL, ptr);

		VALUE iv = rb_iv_get(klass, "@iv");
		if (iv != Qnil)
			rb_iv_set(self, rb_id2name(iv), obj);
	}

	return self;
}

static VALUE cAccessor_get(VALUE self)
{
	void *ptr = rb_data_object_get(self);

	int type = FIX2INT(rb_iv_get(CLASS_OF(self), "@c_type"));

	switch (type) {
	case 1: return LONG2FIX(*(bool *)ptr);
	case 2: return LONG2FIX(*(int8_t *)ptr);
	case 3: return LONG2FIX(*(int16_t *)ptr);
	case 4: return LONG2FIX(*(int32_t *)ptr);
	case 5: return LONG2FIX(*(int64_t *)ptr);
	case 6: return LONG2FIX(*(uint8_t *)ptr);
	case 7: return LONG2FIX(*(uint16_t *)ptr);
	case 8: return LONG2FIX(*(uint32_t *)ptr);
	case 9: return LONG2FIX(*(uint64_t *)ptr);
	default:
		GBLOG("Invalid type: %d", type);
		return Qnil;
	}
}

static VALUE cAccessor_set(VALUE self, VALUE val)
{
	void *ptr = rb_data_object_get(self);

	int type = FIX2INT(rb_iv_get(CLASS_OF(self), "@c_type"));

	switch (type) {
	case 1: (*(bool *)ptr) = !!FIX2LONG(val); break;
	case 2: (*(int8_t *)ptr) = !!FIX2LONG(val); break;
	case 3: (*(int16_t *)ptr) = FIX2LONG(val); break;
	case 4: (*(int32_t *)ptr) = FIX2LONG(val); break;
	case 5: (*(int64_t *)ptr) = FIX2LONG(val); break;
	case 6: (*(uint8_t *)ptr) = FIX2ULONG(val); break;
	case 7: (*(uint16_t *)ptr) = FIX2ULONG(val); break;
	case 8: (*(uint32_t *)ptr) = FIX2ULONG(val); break;
	case 9: (*(uint64_t *)ptr) = FIX2ULONG(val); break;
	default:
		GBLOG("Invalid type: %d", type);
		break;
	}

	return cAccessor_get(self);
}

static VALUE cAccessor_inspect(VALUE self)
{
	char buf[256];
	VALUE klass = CLASS_OF(self);
	int type = FIX2INT(rb_iv_get(klass, "@c_type"));
	VALUE val = cAccessor_get(self);
	sprintf(buf, attr_fmt[type-1], FIX2LONG(val));

	return rb_sprintf("%s: %s", rb_class2name(klass), buf);
}

// TODO: Named to fit extconf.rb convention, I think.  Should this be moved to
//       a Ruby gem and loaded like other extensions?
static void Init_egbe(void)
{
	mEGBE = rb_define_module("EGBE");

	cGB = rb_define_class_under(mEGBE, "GB", rb_cObject);
	rb_define_method(cGB, "initialize", cGB_initialize, 0);

	ID register_accessor = rb_intern("register_accessor");
	rb_eval_string(
		"class << EGBE::GB\n"
		"	def register_accessor(sym)\n"
		"		define_method sym.to_sym do\n"
		"			iv = '@'+sym.to_s\n"
		"			instance_variable_get(iv).get\n"
		"		end\n"
		"		define_method %Q'#{sym}='.to_sym do |val|\n"
		"			iv = '@'+sym.to_s\n"
		"			instance_variable_get(iv).set val\n"
		"		end\n"
		"	end\n"
		"end\n"
	);

	VALUE cGB_accessors = rb_ary_new();
	rb_iv_set(cGB, "@accessors", cGB_accessors);

	cAccessor = rb_define_class_under(cGB, "Accessor", rb_cObject);
	rb_define_method(cAccessor, "get", cAccessor_get, 0);
	rb_define_method(cAccessor, "set", cAccessor_set, 1);
	rb_define_method(cAccessor, "inspect", cAccessor_inspect, 0);

	#define GB_ATTR(klass_str, attr)                                        \
	do {                                                                    \
		VALUE klass = rb_define_class_under(cGB, klass_str, cAccessor); \
		rb_iv_set(klass, "@iv", rb_intern("@"#attr));                   \
		rb_iv_set(klass, "@c_offset", LONG2FIX(GB_OFFSETOF(attr)));     \
		rb_iv_set(klass, "@c_type", LONG2FIX(GB_TYPEOF(attr)));         \
		rb_ary_push(cGB_accessors, klass);                              \
		rb_funcall(cGB, register_accessor, 1, rb_str_new_cstr(#attr));  \
	} while (0)

	GB_ATTR("Features", features);
	GB_ATTR("MBC", mbc);
	GB_ATTR("System", system);
	GB_ATTR("GBC", gbc);

	GB_ATTR("CPUStatus", cpu_status);
	GB_ATTR("Cycles", cycles);

	GB_ATTR("DoubleSpeed", double_speed);
	GB_ATTR("DoubleSpeedSwitch", double_speed_switch);

	GB_ATTR("IMEStatus", ime_status);
	GB_ATTR("IRQEnabled", irq_enabled);
	GB_ATTR("IRQFlagged", irq_flagged);

	GB_ATTR("JoypadStatus", joypad_status);
	GB_ATTR("P1Arrows", p1_arrows);
	GB_ATTR("P1Buttons", p1_buttons);

	GB_ATTR("NextTimerIn", next_timer_in);
	GB_ATTR("TimerEnabled", timer_enabled);
	GB_ATTR("TimerCounter", timer_counter);
	GB_ATTR("TimerModulo", timer_modulo);
	GB_ATTR("TimerFrequencyCode", timer_frequency_code);
	GB_ATTR("TimerFrequencyCycles", timer_frequency_cycles);

	GB_ATTR("NextSerialIn", next_serial_in);
	GB_ATTR("IsSerialPending", is_serial_pending);
	GB_ATTR("IsSerialInternal", is_serial_internal);
	GB_ATTR("SB", sb);
	GB_ATTR("NextSB", next_sb);

	GB_ATTR("APUEnabled", apu_enabled);
	GB_ATTR("NextAPUFrameIn", next_apu_frame_in);
	GB_ATTR("APUFrame", apu_frame);
	GB_ATTR("SO1Volume", so1_volume);
	GB_ATTR("SO2Volume", so2_volume);
	GB_ATTR("SO1Vin", so1_vin);
	GB_ATTR("SO2Vin", so2_vin);

	GB_ATTR("LCDEnabled", lcd_enabled);
	GB_ATTR("LCDStatus", lcd_status);
	GB_ATTR("NextLCDStatus", next_lcd_status);
	GB_ATTR("NextLCDStatusIn", next_lcd_status_in);

	GB_ATTR("Scanline", scanline);
	GB_ATTR("ScanlineCompare", scanline_compare);
	GB_ATTR("SY", sy);
	GB_ATTR("SX", sx);
	GB_ATTR("WY", wy);
	GB_ATTR("WX", wx);
	GB_ATTR("DMA", dma);
	GB_ATTR("SpriteSize", sprite_size);
	GB_ATTR("SpritesEnabled", sprites_enabled);
	GB_ATTR("BackgroundEnabled", background_enabled);
	GB_ATTR("WindowEnabled", window_enabled);
	GB_ATTR("STATOnHBlank", stat_on_hblank);
	GB_ATTR("STATOnVBlank", stat_on_vblank);
	GB_ATTR("STATOnOAMSearch", stat_on_oam_search);
	GB_ATTR("STATOnScanline", stat_on_scanline);

	GB_ATTR("HDMAEnabled", hdma_enabled);
	GB_ATTR("GDMA", gdma);
	GB_ATTR("HDMABlocks_remaining", hdma_blocks_remaining);
	GB_ATTR("HDMABlocks_queued", hdma_blocks_queued);
	GB_ATTR("HDMASrc", hdma_src);
	GB_ATTR("HDMADst", hdma_dst);

	GB_ATTR("BGPIndex", bgp_index);
	GB_ATTR("BGPIncrement", bgp_increment);
	GB_ATTR("OBPIndex", obp_index);
	GB_ATTR("OBPIncrement", obp_increment);

	GB_ATTR("SpritesUnsorted", sprites_unsorted);

	GB_ATTR("BackgroundTilemap", background_tilemap);
	GB_ATTR("WindowTilemap", window_tilemap);
	GB_ATTR("VRAMBank", vram_bank);
	GB_ATTR("TilemapSigned", tilemap_signed);

	GB_ATTR("BootEnabled", boot_enabled);
	GB_ATTR("BootSize", boot_size);
	GB_ATTR("ROMBank", rom_bank);
	GB_ATTR("ROMBanks", rom_banks);
	GB_ATTR("ROMSize", rom_size);

	GB_ATTR("SRAMEnabled", sram_enabled);
	GB_ATTR("SRAMBank", sram_bank);
	GB_ATTR("SRAMBanks", sram_banks);
	GB_ATTR("SRAMSize", sram_size);

	GB_ATTR("WRAMBank", wram_bank);
	GB_ATTR("WRAMBanks", wram_banks);
	GB_ATTR("WRAMSize", wram_size);

	GB_ATTR("MBC1SRAMMode", mbc1_sram_mode);

	GB_ATTR("RTCStatus", rtc_status);
	GB_ATTR("RTCSeconds", rtc_seconds);
	GB_ATTR("RTCLastLatched", rtc_last_latched);
	GB_ATTR("RTCLatch", rtc_latch);
	GB_ATTR("RTCHalted", rtc_halted);

	GB_ATTR("AF", af);
	GB_ATTR("BC", bc);
	GB_ATTR("DE", de);
	GB_ATTR("HL", hl);
	GB_ATTR("PC", pc);
	GB_ATTR("SP", sp);

	GB_ATTR("A", a);
	GB_ATTR("F", f);
	GB_ATTR("B", b);
	GB_ATTR("C", c);
	GB_ATTR("D", d);
	GB_ATTR("E", e);
	GB_ATTR("H", h);
	GB_ATTR("L", l);

	// GB_ATTR("On_serial_start", on_serial_start);
	// GB_ATTR("Next_apu_sample", next_apu_sample);
	// struct gameboy_audio_sample apu_samples[MAX_APU_SAMPLES][2]);
	// GB_ATTR("Apu_index", apu_index);
	// GB_ATTR("On_apu_buffer_filled", on_apu_buffer_filled);
	// GB_ATTR("Sq1", sq1);
	// GB_ATTR("Sq2", sq2);
	// GB_ATTR("Wave", wave);
	// GB_ATTR("Noise", noise);
	// struct gameboy_palette bgp[8]);
	// struct gameboy_palette obp[8]);
	// struct gameboy_callback on_vblank);
	// int screen[144][160]);
	// int dbg_background[256][256]);
	// int dbg_window[256][256]);
	// int dbg_palettes[82][86]);
	// int dbg_vram[192][128]);
	// int dbg_vram_gbc[192][128]);
	// struct gameboy_sprite sprites[40]);
	// struct gameboy_sprite *sprites_sorted[40]);
	// struct gameboy_tile tiles[2][384]);
	// struct gameboy_background_table tilemaps[2]);
	// GB_ATTR("Boot", boot);
	// uint8_t (*rom)[0x4000]);
	// GB_ATTR("Romx", romx);
	// uint8_t (*sram)[0x2000]);
	// GB_ATTR("Sramx", sramx);
	// uint8_t (*wram)[0x1000]);
	// GB_ATTR("Wramx", wramx);
	// uint8_t hram[0x007F]);

	rb_enc_find_index("encdb");
	rb_require("rubygems");
	rb_require("irb");
	rb_eval_string("$binding = binding");

	rb_eval_string(
		"begin\n"
		"	require_relative 'debugger.rb'\n"
		"rescue => e\n"
		"	puts [e.message, *e.backtrace].join(\"\\n\\t\")\n"
		"end"
	);
}

static void start_debugger(struct egbe_gameboy *egb)
{
	if (rb_gv_get("$gb") == Qnil) {
		VALUE self = Data_Wrap_Struct(cGB, NULL, NULL, egb->gb);
		rb_gv_set("$gb", self);
		rb_funcall(self, rb_intern("initialize"), 0);

		rb_eval_string("$binding.eval 'gb = $gb' ");
	}

	rb_eval_string(
		"begin\n"
		"	$binding.irb\n"
		"rescue => e\n"
		"	puts [e.message, *e.backtrace].join(\"\\n\\t\")\n"
		"end"
	);
}

static void plugin_call(struct egbe_application *app, struct egbe_plugin *plugin,
                        void (*call_next_plugin)(void *context), void *context)
{
	// Ensure that all Ruby code happens on this stack frame
	// See https://silverhammermba.github.io/emberb/embed/#startup-teardown
	RUBY_INIT_STACK;
	ruby_init();
	ruby_init_loadpath();
	ruby_script("EGBE");

	Init_egbe();
	call_next_plugin(context);

	ruby_cleanup(0);
}

PLUGIN_NAME("ruby");
PLUGIN_DESCRIPTION("Exposes EGBE objects to a Ruby debugger");
PLUGIN_WEBSITE("https://github.com/ssh-esoteric/egbe");

PLUGIN_AUTHOR("EGBE");
PLUGIN_VERSION("0.0.1");

PLUGIN_CALL(plugin_call);

PLUGIN_START_DEBUGGER(start_debugger);
