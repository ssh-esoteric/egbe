#include "common.h"
#include "mmu.h"

enum {
	FLAG_CARRY     = 0x10,
	FLAG_HALFCARRY = 0x20,
	FLAG_SUBTRACT  = 0x40,
	FLAG_ZERO      = 0x80,
};

static void tick(struct gameboy *gb)
{
	; // TODO: synchronize CPU cycles to other components (LCD, APU, etc)
}

static uint8_t timed_read(struct gameboy *gb, uint16_t addr)
{
	tick(gb);

	return mmu_read(gb, addr);
}

static void timed_write(struct gameboy *gb, uint16_t addr, uint8_t val)
{
	tick(gb);

	mmu_write(gb, addr, val);
}

static uint8_t iv(struct gameboy *gb)
{
	return timed_read(gb, gb->pc++);
}

static uint16_t iv16(struct gameboy *gb)
{
	uint8_t lo = iv(gb);
	uint8_t hi = iv(gb);

	return (hi << 8) | lo;
}

static inline void set_flags(struct gameboy *gb, bool c, bool h, bool n, bool z)
{
	gb->f = (c ? FLAG_CARRY     : 0)
	      | (h ? FLAG_HALFCARRY : 0)
	      | (n ? FLAG_SUBTRACT  : 0)
	      | (z ? FLAG_ZERO      : 0);
}

static inline void set_flags_chn(struct gameboy *gb, bool c, bool h, bool n)
{
	gb->f = (gb->f & ~(FLAG_CARRY | FLAG_HALFCARRY | FLAG_SUBTRACT))
	      | (c ? FLAG_CARRY     : 0)
	      | (h ? FLAG_HALFCARRY : 0)
	      | (n ? FLAG_SUBTRACT  : 0);
}

static inline void set_flags_hnz(struct gameboy *gb, bool h, bool n, bool z)
{
	gb->f = (gb->f & ~(FLAG_HALFCARRY | FLAG_SUBTRACT | FLAG_ZERO))
	      | (h ? FLAG_HALFCARRY : 0)
	      | (n ? FLAG_SUBTRACT  : 0)
	      | (z ? FLAG_ZERO      : 0);
}

static inline bool check_overflow(int mask, int lhs, int rhs, bool carry)
{
	return (lhs & mask) + (rhs & mask) + carry > mask;
}

static inline bool check_underflow(int mask, int lhs, int rhs, bool carry)
{
	return (lhs & mask) < (rhs & mask) + carry;
}

#define _overflow(m, x, y, c, ...) check_overflow(m, x, y, c)
#define overflow4(x, y, ...)  _overflow(0x000F, x, y, ##__VA_ARGS__, false)
#define overflow8(x, y, ...)  _overflow(0x00FF, x, y, ##__VA_ARGS__, false)
#define overflow12(x, y, ...) _overflow(0x0FFF, x, y, ##__VA_ARGS__, false)
#define overflow16(x, y, ...) _overflow(0xFFFF, x, y, ##__VA_ARGS__, false)

#define _underflow(m, x, y, c, ...) check_underflow(m, x, y, c)
#define underflow4(x, y, ...)  _underflow(0x000F, x, y, ##__VA_ARGS__, false)
#define underflow8(x, y, ...)  _underflow(0x00FF, x, y, ##__VA_ARGS__, false)
#define underflow12(x, y, ...) _underflow(0x0FFF, x, y, ##__VA_ARGS__, false)
#define underflow16(x, y, ...) _underflow(0xFFFF, x, y, ##__VA_ARGS__, false)

static inline void instr_adc_r_aa(struct gameboy *gb, uint8_t *r, uint16_t aa);
static inline void instr_adc_r_v(struct gameboy *gb, uint8_t *r, uint8_t v);
static inline void instr_add_r_aa(struct gameboy *gb, uint8_t *r, uint16_t aa);
static inline void instr_add_r_v(struct gameboy *gb, uint8_t *r, uint8_t v);
static inline void instr_add_rr_vv(struct gameboy *gb, uint16_t *rr, uint16_t vv);
static inline void instr_and_r_aa(struct gameboy *gb, uint8_t *r, uint16_t aa);
static inline void instr_and_r_v(struct gameboy *gb, uint8_t *r, uint8_t v);
static inline void instr_bit_n_aa(struct gameboy *gb, int n, uint16_t aa);
static inline void instr_bit_n_v(struct gameboy *gb, int n, uint8_t v);
static inline void instr_call(struct gameboy *gb, bool condition);
static inline void instr_ccf(struct gameboy *gb);
static inline void instr_cp_r_aa(struct gameboy *gb, uint8_t *r, uint16_t aa);
static inline void instr_cp_r_v(struct gameboy *gb, uint8_t *r, uint8_t v);
static inline void instr_cpl_r(struct gameboy *gb, uint8_t *r);
static inline void instr_daa_r(struct gameboy *gb, uint8_t *r);
static inline void instr_dec_aa(struct gameboy *gb, uint16_t aa);
static inline void instr_dec_r(struct gameboy *gb, uint8_t *r);
static inline void instr_dec_rr(struct gameboy *gb, uint16_t *rr);
static inline void instr_di(struct gameboy *gb);
static inline void instr_ei(struct gameboy *gb);
static inline void instr_halt(struct gameboy *gb);
static inline void instr_inc_aa(struct gameboy *gb, uint16_t aa);
static inline void instr_inc_r(struct gameboy *gb, uint8_t *r);
static inline void instr_inc_rr(struct gameboy *gb, uint16_t *rr);
static inline void instr_jp(struct gameboy *gb, bool condition);
static inline void instr_jr(struct gameboy *gb, bool condition);
static inline void instr_ld_aa_v(struct gameboy *gb, uint16_t aa, uint8_t v);
static inline void instr_ld_aa_vv(struct gameboy *gb, uint16_t aa, uint16_t vv);
static inline void instr_ld_r_aa(struct gameboy *gb, uint8_t *r, uint16_t aa);
static inline void instr_ld_r_v(struct gameboy *gb, uint8_t *r, uint8_t v);
static inline void instr_ld_rr_vv(struct gameboy *gb, uint16_t *rr, uint16_t vv);
static inline void instr_ld_rr_vv_jr(struct gameboy *gb, uint16_t *rr, uint16_t vv);
static inline void instr_nop(struct gameboy *gb);
static inline void instr_or_r_aa(struct gameboy *gb, uint8_t *r, uint16_t aa);
static inline void instr_or_r_v(struct gameboy *gb, uint8_t *r, uint8_t v);
static inline void instr_pop(struct gameboy *gb, uint16_t *rr);
static inline void instr_push(struct gameboy *gb, uint16_t vv);
static inline void instr_res_n_aa(struct gameboy *gb, int n, uint16_t aa);
static inline void instr_res_n_r(struct gameboy *gb, int n, uint8_t *r);
static inline void instr_ret(struct gameboy *gb, bool condition);
static inline void instr_reti(struct gameboy *gb);
static inline void instr_rl_aa(struct gameboy *gb, uint16_t aa);
static inline void instr_rl_r(struct gameboy *gb, uint8_t *r);
static inline void instr_rla(struct gameboy *gb);
static inline void instr_rlc_aa(struct gameboy *gb, uint16_t aa);
static inline void instr_rlc_r(struct gameboy *gb, uint8_t *r);
static inline void instr_rlca(struct gameboy *gb);
static inline void instr_rr_aa(struct gameboy *gb, uint16_t aa);
static inline void instr_rr_r(struct gameboy *gb, uint8_t *r);
static inline void instr_rra(struct gameboy *gb);
static inline void instr_rrc_aa(struct gameboy *gb, uint16_t aa);
static inline void instr_rrc_r(struct gameboy *gb, uint8_t *r);
static inline void instr_rrca(struct gameboy *gb);
static inline void instr_rst(struct gameboy *gb, uint16_t aa);
static inline void instr_sbc_r_aa(struct gameboy *gb, uint8_t *r, uint16_t aa);
static inline void instr_sbc_r_v(struct gameboy *gb, uint8_t *r, uint8_t v);
static inline void instr_scf(struct gameboy *gb);
static inline void instr_set_n_aa(struct gameboy *gb, int n, uint16_t aa);
static inline void instr_set_n_r(struct gameboy *gb, int n, uint8_t *r);
static inline void instr_sla_aa(struct gameboy *gb, uint16_t aa);
static inline void instr_sla_r(struct gameboy *gb, uint8_t *r);
static inline void instr_sra_aa(struct gameboy *gb, uint16_t aa);
static inline void instr_sra_r(struct gameboy *gb, uint8_t *r);
static inline void instr_srl_aa(struct gameboy *gb, uint16_t aa);
static inline void instr_srl_r(struct gameboy *gb, uint8_t *r);
static inline void instr_stop(struct gameboy *gb);
static inline void instr_sub_r_aa(struct gameboy *gb, uint8_t *r, uint16_t aa);
static inline void instr_sub_r_v(struct gameboy *gb, uint8_t *r, uint8_t v);
static inline void instr_swap_aa(struct gameboy *gb, uint16_t aa);
static inline void instr_swap_r(struct gameboy *gb, uint8_t *r);
static inline void instr_undefined(struct gameboy *gb, uint8_t opcode);
static inline void instr_xor_r_aa(struct gameboy *gb, uint8_t *r, uint16_t aa);
static inline void instr_xor_r_v(struct gameboy *gb, uint8_t *r, uint8_t v);

void instr_adc_r_aa(struct gameboy *gb, uint8_t *r, uint16_t aa)
{
	instr_adc_r_v(gb, r, timed_read(gb, aa));
}

void instr_adc_r_v(struct gameboy *gb, uint8_t *r, uint8_t v)
{
	bool c = overflow8(*r, v, gb->carry);
	bool h = overflow4(*r, v, gb->carry);

	*r = *r + v + gb->carry;

	set_flags(gb, c, h, false, *r == 0);
}

void instr_add_r_aa(struct gameboy *gb, uint8_t *r, uint16_t aa)
{
	instr_add_r_v(gb, r, timed_read(gb, aa));
}

void instr_add_r_v(struct gameboy *gb, uint8_t *r, uint8_t v)
{
	bool c = overflow8(*r, v);
	bool h = overflow4(*r, v);

	*r = *r + v;

	set_flags(gb, c, h, false, *r == 0);
}

void instr_add_rr_vv(struct gameboy *gb, uint16_t *rr, uint16_t vv)
{
	bool c = overflow16(*rr, vv);
	bool h = overflow12(*rr, vv);

	*rr = *rr + vv;

	set_flags_chn(gb, c, h, false);
}

void instr_and_r_aa(struct gameboy *gb, uint8_t *r, uint16_t aa)
{
	instr_and_r_v(gb, r, timed_read(gb, aa));
}

void instr_and_r_v(struct gameboy *gb, uint8_t *r, uint8_t v)
{
	*r = *r & v;

	set_flags(gb, false, true, false, *r == 0);
}

void instr_bit_n_aa(struct gameboy *gb, int n, uint16_t aa)
{
	instr_bit_n_v(gb, n, timed_read(gb, aa));
}

void instr_bit_n_v(struct gameboy *gb, int n, uint8_t v)
{
	set_flags_hnz(gb, true, false, ((1 << n) & v) == 0);
}

void instr_call(struct gameboy *gb, bool condition)
{
	uint16_t next = iv16(gb);

	if (condition)
		instr_rst(gb, next);
}

void instr_ccf(struct gameboy *gb)
{
	set_flags_chn(gb, !gb->carry, false, false);
}

void instr_cp_r_aa(struct gameboy *gb, uint8_t *r, uint16_t aa)
{
	instr_cp_r_v(gb, r, timed_read(gb, aa));
}

// CP is equivalent to a SUB with the results discarded
void instr_cp_r_v(struct gameboy *gb, uint8_t *r, uint8_t v)
{
	uint8_t tmp = *r;

	instr_sub_r_v(gb, &tmp, v);
}

void instr_cpl_r(struct gameboy *gb, uint8_t *r)
{
	*r = ~*r;

	gb->f = gb->f | FLAG_HALFCARRY | FLAG_SUBTRACT;
}

void instr_daa_r(struct gameboy *gb, uint8_t *r)
{
	GBLOG("TODO"); gb->cpu_status = GAMEBOY_CPU_CRASHED;
}

void instr_dec_aa(struct gameboy *gb, uint16_t aa)
{
	uint8_t tmp = timed_read(gb, aa);
	instr_dec_r(gb, &tmp);
	timed_write(gb, aa, tmp);
}

void instr_dec_r(struct gameboy *gb, uint8_t *r)
{
	bool h = underflow4(*r, 1);
	--(*r);

	set_flags_hnz(gb, h, true, *r == 0);
}

void instr_dec_rr(struct gameboy *gb, uint16_t *rr)
{
	--(*rr);
}

void instr_di(struct gameboy *gb)
{
	GBLOG("TODO"); gb->cpu_status = GAMEBOY_CPU_CRASHED;
}

void instr_ei(struct gameboy *gb)
{
	GBLOG("TODO"); gb->cpu_status = GAMEBOY_CPU_CRASHED;
}

void instr_halt(struct gameboy *gb)
{
	GBLOG("TODO"); gb->cpu_status = GAMEBOY_CPU_CRASHED;
}

void instr_inc_aa(struct gameboy *gb, uint16_t aa)
{
	uint8_t tmp = timed_read(gb, aa);
	instr_inc_r(gb, &tmp);
	timed_write(gb, aa, tmp);
}

void instr_inc_r(struct gameboy *gb, uint8_t *r)
{
	bool h = overflow4(*r, 1);
	++(*r);

	set_flags_hnz(gb, h, false, *r == 0);
}

void instr_inc_rr(struct gameboy *gb, uint16_t *rr)
{
	++(*rr);
}

void instr_jp(struct gameboy *gb, bool condition)
{
	uint16_t next = iv16(gb);

	if (condition)
		gb->pc = next;
}

void instr_jr(struct gameboy *gb, bool condition)
{
	int8_t diff = iv(gb);

	if (condition)
		gb->pc += diff;
}

void instr_ld_aa_v(struct gameboy *gb, uint16_t aa, uint8_t v)
{
	timed_write(gb, aa, v);
}

void instr_ld_aa_vv(struct gameboy *gb, uint16_t aa, uint16_t vv)
{
	timed_write(gb, aa + 0, vv >> 8);
	timed_write(gb, aa + 1, vv & 0xFF);
}

void instr_ld_r_aa(struct gameboy *gb, uint8_t *r, uint16_t aa)
{
	*r = timed_read(gb, aa);
}

void instr_ld_r_v(struct gameboy *gb, uint8_t *r, uint8_t v)
{
	*r = v;
}

void instr_ld_rr_vv(struct gameboy *gb, uint16_t *rr, uint16_t vv)
{
	*rr = vv;
}

void instr_ld_rr_vv_jr(struct gameboy *gb, uint16_t *rr, uint16_t vv)
{
	int8_t diff = (int8_t)iv(gb);

	bool c = overflow8(vv, diff);
	bool h = overflow4(vv, diff);

	*rr = vv + diff;

	// TODO: Verify the flags on this one
	set_flags(gb, c, h, false, false);
}

void instr_nop(struct gameboy *gb)
{
	;
}

void instr_or_r_aa(struct gameboy *gb, uint8_t *r, uint16_t aa)
{
	instr_or_r_v(gb, r, timed_read(gb, aa));
}

void instr_or_r_v(struct gameboy *gb, uint8_t *r, uint8_t v)
{
	*r = *r | v;

	gb->f = (*r == 0) ? FLAG_ZERO : 0;
}

void instr_pop(struct gameboy *gb, uint16_t *rr)
{
	uint8_t lo = timed_read(gb, gb->sp++);
	uint8_t hi = timed_read(gb, gb->sp++);

	*rr = (hi << 8) | lo;
}

void instr_push(struct gameboy *gb, uint16_t vv)
{
	timed_write(gb, --gb->sp, vv >> 8);
	timed_write(gb, --gb->sp, vv & 0xFF);
}

void instr_res_n_aa(struct gameboy *gb, int n, uint16_t aa)
{
	uint8_t tmp = timed_read(gb, aa);
	instr_res_n_r(gb, n, &tmp);
	timed_write(gb, aa, tmp);
}

void instr_res_n_r(struct gameboy *gb, int n, uint8_t *r)
{
	*r = *r & ~(1 << n);
}

void instr_ret(struct gameboy *gb, bool condition)
{
	if (condition)
		instr_pop(gb, &gb->pc);
}

void instr_reti(struct gameboy *gb)
{
	instr_ret(gb, true);

	GBLOG("TODO"); gb->cpu_status = GAMEBOY_CPU_CRASHED;
}

void instr_rl_aa(struct gameboy *gb, uint16_t aa)
{
	uint8_t tmp = timed_read(gb, aa);
	instr_rl_r(gb, &tmp);
	timed_write(gb, aa, tmp);
}

void instr_rl_r(struct gameboy *gb, uint8_t *r)
{
	int tmp = (*r << 1) | gb->carry;
	*r = tmp;

	set_flags(gb, tmp > 0xFF, false, false, *r == 0);
}

void instr_rla(struct gameboy *gb)
{
	instr_rl_r(gb, &gb->a);

	gb->zero = false;
}

void instr_rlc_aa(struct gameboy *gb, uint16_t aa)
{
	uint8_t tmp = timed_read(gb, aa);
	instr_rlc_r(gb, &tmp);
	timed_write(gb, aa, tmp);
}

void instr_rlc_r(struct gameboy *gb, uint8_t *r)
{
	*r = (*r << 1) | (*r >> 7);

	set_flags(gb, *r & 0x01, false, false, *r == 0);
}

void instr_rlca(struct gameboy *gb)
{
	instr_rlc_r(gb, &gb->a);

	gb->zero = false;
}

void instr_rr_aa(struct gameboy *gb, uint16_t aa)
{
	uint8_t tmp = timed_read(gb, aa);
	instr_rr_r(gb, &tmp);
	timed_write(gb, aa, tmp);
}

void instr_rr_r(struct gameboy *gb, uint8_t *r)
{
	bool c = *r & 0x01;

	*r = (*r >> 1) | (gb->carry << 7);

	set_flags(gb, c, false, false, *r == 0);
}

void instr_rra(struct gameboy *gb)
{
	instr_rr_r(gb, &gb->a);

	gb->zero = false;
}

void instr_rrc_aa(struct gameboy *gb, uint16_t aa)
{
	uint8_t tmp = timed_read(gb, aa);
	instr_rrc_r(gb, &tmp);
	timed_write(gb, aa, tmp);
}

void instr_rrc_r(struct gameboy *gb, uint8_t *r)
{
	*r = (*r >> 1) | (*r << 7);

	set_flags(gb, *r & 0x80, false, false, *r == 0);
}

void instr_rrca(struct gameboy *gb)
{
	instr_rrc_r(gb, &gb->a);

	gb->zero = false;
}

void instr_rst(struct gameboy *gb, uint16_t aa)
{
	instr_push(gb, gb->pc);

	gb->pc = aa;
}

void instr_sbc_r_aa(struct gameboy *gb, uint8_t *r, uint16_t aa)
{
	instr_sbc_r_v(gb, r, timed_read(gb, aa));
}

void instr_sbc_r_v(struct gameboy *gb, uint8_t *r, uint8_t v)
{
	bool c = underflow8(*r, v, gb->carry);
	bool h = underflow4(*r, v, gb->carry);

	*r = *r - v - gb->carry;

	set_flags(gb, c, h, true, *r == 0);
}

void instr_scf(struct gameboy *gb)
{
	set_flags_chn(gb, true, false, false);
}

void instr_set_n_aa(struct gameboy *gb, int n, uint16_t aa)
{
	uint8_t tmp = timed_read(gb, aa);
	instr_set_n_r(gb, n, &tmp);
	timed_write(gb, aa, tmp);
}

void instr_set_n_r(struct gameboy *gb, int n, uint8_t *r)
{
	*r = *r | (1 << n);
}

void instr_sla_aa(struct gameboy *gb, uint16_t aa)
{
	uint8_t tmp = timed_read(gb, aa);
	instr_sla_r(gb, &tmp);
	timed_write(gb, aa, tmp);
}

void instr_sla_r(struct gameboy *gb, uint8_t *r)
{
	int tmp = *r;
	*r = *r << 1;

	set_flags(gb, tmp & 0x80, false, false, *r == 0);
}

void instr_sra_aa(struct gameboy *gb, uint16_t aa)
{
	uint8_t tmp = timed_read(gb, aa);
	instr_sra_r(gb, &tmp);
	timed_write(gb, aa, tmp);
}

void instr_sra_r(struct gameboy *gb, uint8_t *r)
{
	bool c = *r & 0x01;

	*r = (*r & 0x80) | (*r >> 1);

	set_flags(gb, c, false, false, *r == 0);
}

void instr_srl_aa(struct gameboy *gb, uint16_t aa)
{
	uint8_t tmp = timed_read(gb, aa);
	instr_srl_r(gb, &tmp);
	timed_write(gb, aa, tmp);
}

void instr_srl_r(struct gameboy *gb, uint8_t *r)
{
	bool c = *r & 0x01;

	*r = *r >> 1;

	set_flags(gb, c, false, false, *r == 0);
}

void instr_stop(struct gameboy *gb)
{
	GBLOG("TODO"); gb->cpu_status = GAMEBOY_CPU_CRASHED;
}

void instr_sub_r_aa(struct gameboy *gb, uint8_t *r, uint16_t aa)
{
	instr_sub_r_v(gb, r, timed_read(gb, aa));
}

void instr_sub_r_v(struct gameboy *gb, uint8_t *r, uint8_t v)
{
	bool c = underflow8(*r, v);
	bool h = underflow4(*r, v);

	*r = *r - v;

	set_flags(gb, c, h, true, *r == 0);
}

void instr_swap_aa(struct gameboy *gb, uint16_t aa)
{
	uint8_t tmp = timed_read(gb, aa);
	instr_swap_r(gb, &tmp);
	timed_write(gb, aa, tmp);
}

void instr_swap_r(struct gameboy *gb, uint8_t *r)
{
	*r = (*r << 4) | (*r >> 4);

	gb->f = (*r == 0) ? FLAG_ZERO : 0;
}

void instr_undefined(struct gameboy *gb, uint8_t opcode)
{
	GBLOG("Undefined opcode: %02X", opcode);

	gb->cpu_status = GAMEBOY_CPU_CRASHED;
}

void instr_xor_r_aa(struct gameboy *gb, uint8_t *r, uint16_t aa)
{
	instr_xor_r_v(gb, r, timed_read(gb, aa));
}

void instr_xor_r_v(struct gameboy *gb, uint8_t *r, uint8_t v)
{
	*r = *r ^ v;

	gb->f = (*r == 0) ? FLAG_ZERO : 0;
}

static void process_cb_opcode(struct gameboy *gb, uint8_t opcode)
{
	switch (opcode) {
	case 0x00: instr_rlc_r(gb, &gb->b); break;
	case 0x01: instr_rlc_r(gb, &gb->c); break;
	case 0x02: instr_rlc_r(gb, &gb->d); break;
	case 0x03: instr_rlc_r(gb, &gb->e); break;
	case 0x04: instr_rlc_r(gb, &gb->h); break;
	case 0x05: instr_rlc_r(gb, &gb->l); break;
	case 0x07: instr_rlc_r(gb, &gb->a); break;

	case 0x08: instr_rrc_r(gb, &gb->b); break;
	case 0x09: instr_rrc_r(gb, &gb->c); break;
	case 0x0A: instr_rrc_r(gb, &gb->d); break;
	case 0x0B: instr_rrc_r(gb, &gb->e); break;
	case 0x0C: instr_rrc_r(gb, &gb->h); break;
	case 0x0D: instr_rrc_r(gb, &gb->l); break;
	case 0x0F: instr_rrc_r(gb, &gb->a); break;

	case 0x10: instr_rl_r(gb, &gb->b); break;
	case 0x11: instr_rl_r(gb, &gb->c); break;
	case 0x12: instr_rl_r(gb, &gb->d); break;
	case 0x13: instr_rl_r(gb, &gb->e); break;
	case 0x14: instr_rl_r(gb, &gb->h); break;
	case 0x15: instr_rl_r(gb, &gb->l); break;
	case 0x17: instr_rl_r(gb, &gb->a); break;

	case 0x18: instr_rr_r(gb, &gb->b); break;
	case 0x19: instr_rr_r(gb, &gb->c); break;
	case 0x1A: instr_rr_r(gb, &gb->d); break;
	case 0x1B: instr_rr_r(gb, &gb->e); break;
	case 0x1C: instr_rr_r(gb, &gb->h); break;
	case 0x1D: instr_rr_r(gb, &gb->l); break;
	case 0x1F: instr_rr_r(gb, &gb->a); break;

	case 0x20: instr_sla_r(gb, &gb->b); break;
	case 0x21: instr_sla_r(gb, &gb->c); break;
	case 0x22: instr_sla_r(gb, &gb->d); break;
	case 0x23: instr_sla_r(gb, &gb->e); break;
	case 0x24: instr_sla_r(gb, &gb->h); break;
	case 0x25: instr_sla_r(gb, &gb->l); break;
	case 0x27: instr_sla_r(gb, &gb->a); break;

	case 0x28: instr_sra_r(gb, &gb->b); break;
	case 0x29: instr_sra_r(gb, &gb->c); break;
	case 0x2A: instr_sra_r(gb, &gb->d); break;
	case 0x2B: instr_sra_r(gb, &gb->e); break;
	case 0x2C: instr_sra_r(gb, &gb->h); break;
	case 0x2D: instr_sra_r(gb, &gb->l); break;
	case 0x2F: instr_sra_r(gb, &gb->a); break;

	case 0x30: instr_swap_r(gb, &gb->b); break;
	case 0x31: instr_swap_r(gb, &gb->c); break;
	case 0x32: instr_swap_r(gb, &gb->d); break;
	case 0x33: instr_swap_r(gb, &gb->e); break;
	case 0x34: instr_swap_r(gb, &gb->h); break;
	case 0x35: instr_swap_r(gb, &gb->l); break;
	case 0x37: instr_swap_r(gb, &gb->a); break;

	case 0x38: instr_srl_r(gb, &gb->b); break;
	case 0x39: instr_srl_r(gb, &gb->c); break;
	case 0x3A: instr_srl_r(gb, &gb->d); break;
	case 0x3B: instr_srl_r(gb, &gb->e); break;
	case 0x3C: instr_srl_r(gb, &gb->h); break;
	case 0x3D: instr_srl_r(gb, &gb->l); break;
	case 0x3F: instr_srl_r(gb, &gb->a); break;

	case 0x06: instr_rlc_aa(gb, gb->hl); break;
	case 0x0E: instr_rrc_aa(gb, gb->hl); break;
	case 0x16: instr_rl_aa(gb, gb->hl); break;
	case 0x1E: instr_rr_aa(gb, gb->hl); break;
	case 0x26: instr_sla_aa(gb, gb->hl); break;
	case 0x2E: instr_sra_aa(gb, gb->hl); break;
	case 0x36: instr_swap_aa(gb, gb->hl); break;
	case 0x3E: instr_srl_aa(gb, gb->hl); break;

	case 0x40: case 0x50: case 0x60: case 0x70:
	case 0x48: case 0x58: case 0x68: case 0x78:
		instr_bit_n_v(gb, (opcode / 8 % 8), gb->b);
		break;

	case 0x41: case 0x51: case 0x61: case 0x71:
	case 0x49: case 0x59: case 0x69: case 0x79:
		instr_bit_n_v(gb, (opcode / 8 % 8), gb->c);
		break;

	case 0x42: case 0x52: case 0x62: case 0x72:
	case 0x4A: case 0x5A: case 0x6A: case 0x7A:
		instr_bit_n_v(gb, (opcode / 8 % 8), gb->d);
		break;

	case 0x43: case 0x53: case 0x63: case 0x73:
	case 0x4B: case 0x5B: case 0x6B: case 0x7B:
		instr_bit_n_v(gb, (opcode / 8 % 8), gb->e);
		break;

	case 0x44: case 0x54: case 0x64: case 0x74:
	case 0x4C: case 0x5C: case 0x6C: case 0x7C:
		instr_bit_n_v(gb, (opcode / 8 % 8), gb->h);
		break;

	case 0x45: case 0x55: case 0x65: case 0x75:
	case 0x4D: case 0x5D: case 0x6D: case 0x7D:
		instr_bit_n_v(gb, (opcode / 8 % 8), gb->l);
		break;

	case 0x46: case 0x56: case 0x66: case 0x76:
	case 0x4E: case 0x5E: case 0x6E: case 0x7E:
		instr_bit_n_aa(gb, (opcode / 8 % 8), gb->hl);
		break;

	case 0x47: case 0x57: case 0x67: case 0x77:
	case 0x4F: case 0x5F: case 0x6F: case 0x7F:
		instr_bit_n_v(gb, (opcode / 8 % 8), gb->a);
		break;

	case 0x80: case 0x90: case 0xA0: case 0xB0:
	case 0x88: case 0x98: case 0xA8: case 0xB8:
		instr_res_n_r(gb, (opcode / 8 % 8), &gb->b);
		break;

	case 0x81: case 0x91: case 0xA1: case 0xB1:
	case 0x89: case 0x99: case 0xA9: case 0xB9:
		instr_res_n_r(gb, (opcode / 8 % 8), &gb->c);
		break;

	case 0x82: case 0x92: case 0xA2: case 0xB2:
	case 0x8A: case 0x9A: case 0xAA: case 0xBA:
		instr_res_n_r(gb, (opcode / 8 % 8), &gb->d);
		break;

	case 0x83: case 0x93: case 0xA3: case 0xB3:
	case 0x8B: case 0x9B: case 0xAB: case 0xBB:
		instr_res_n_r(gb, (opcode / 8 % 8), &gb->e);
		break;

	case 0x84: case 0x94: case 0xA4: case 0xB4:
	case 0x8C: case 0x9C: case 0xAC: case 0xBC:
		instr_res_n_r(gb, (opcode / 8 % 8), &gb->h);
		break;

	case 0x85: case 0x95: case 0xA5: case 0xB5:
	case 0x8D: case 0x9D: case 0xAD: case 0xBD:
		instr_res_n_r(gb, (opcode / 8 % 8), &gb->l);
		break;

	case 0x86: case 0x96: case 0xA6: case 0xB6:
	case 0x8E: case 0x9E: case 0xAE: case 0xBE:
		instr_res_n_aa(gb, (opcode / 8 % 8), gb->hl);
		break;

	case 0x87: case 0x97: case 0xA7: case 0xB7:
	case 0x8F: case 0x9F: case 0xAF: case 0xBF:
		instr_res_n_r(gb, (opcode / 8 % 8), &gb->a);
		break;

	case 0xC0: case 0xD0: case 0xE0: case 0xF0:
	case 0xC8: case 0xD8: case 0xE8: case 0xF8:
		instr_set_n_r(gb, (opcode / 8 % 8), &gb->b);
		break;

	case 0xC1: case 0xD1: case 0xE1: case 0xF1:
	case 0xC9: case 0xD9: case 0xE9: case 0xF9:
		instr_set_n_r(gb, (opcode / 8 % 8), &gb->c);
		break;

	case 0xC2: case 0xD2: case 0xE2: case 0xF2:
	case 0xCA: case 0xDA: case 0xEA: case 0xFA:
		instr_set_n_r(gb, (opcode / 8 % 8), &gb->d);
		break;

	case 0xC3: case 0xD3: case 0xE3: case 0xF3:
	case 0xCB: case 0xDB: case 0xEB: case 0xFB:
		instr_set_n_r(gb, (opcode / 8 % 8), &gb->e);
		break;

	case 0xC4: case 0xD4: case 0xE4: case 0xF4:
	case 0xCC: case 0xDC: case 0xEC: case 0xFC:
		instr_set_n_r(gb, (opcode / 8 % 8), &gb->h);
		break;

	case 0xC5: case 0xD5: case 0xE5: case 0xF5:
	case 0xCD: case 0xDD: case 0xED: case 0xFD:
		instr_set_n_r(gb, (opcode / 8 % 8), &gb->l);
		break;

	case 0xC6: case 0xD6: case 0xE6: case 0xF6:
	case 0xCE: case 0xDE: case 0xEE: case 0xFE:
		instr_set_n_aa(gb, (opcode / 8 % 8), gb->hl);
		break;

	case 0xC7: case 0xD7: case 0xE7: case 0xF7:
	case 0xCF: case 0xDF: case 0xEF: case 0xFF:
		instr_set_n_r(gb, (opcode / 8 % 8), &gb->a);
		break;
	}
}

static void process_opcode(struct gameboy *gb, uint8_t opcode)
{
	switch (opcode) {
	case 0x00: instr_nop(gb); break;
	case 0x10: instr_stop(gb); break;
	case 0x76: instr_halt(gb); break;
	case 0xCB: process_cb_opcode(gb, iv(gb)); break;
	case 0xF3: instr_di(gb); break;
	case 0xFB: instr_ei(gb); break;

	case 0x01: instr_ld_rr_vv(gb, &gb->bc, iv16(gb)); break;
	case 0x11: instr_ld_rr_vv(gb, &gb->de, iv16(gb)); break;
	case 0x21: instr_ld_rr_vv(gb, &gb->hl, iv16(gb)); break;
	case 0x31: instr_ld_rr_vv(gb, &gb->sp, iv16(gb)); break;

	case 0x02: instr_ld_aa_v(gb, gb->bc, gb->a); break;
	case 0x12: instr_ld_aa_v(gb, gb->de, gb->a); break;
	case 0x22: instr_ld_aa_v(gb, gb->hl++, gb->a); break;
	case 0x32: instr_ld_aa_v(gb, gb->hl--, gb->a); break;

	case 0x03: instr_inc_rr(gb, &gb->bc); break;
	case 0x13: instr_inc_rr(gb, &gb->de); break;
	case 0x23: instr_inc_rr(gb, &gb->hl); break;
	case 0x33: instr_inc_rr(gb, &gb->sp); break;

	case 0x04: instr_inc_r(gb, &gb->b); break;
	case 0x14: instr_inc_r(gb, &gb->d); break;
	case 0x24: instr_inc_r(gb, &gb->h); break;
	case 0x34: instr_inc_aa(gb, gb->hl); break;

	case 0x05: instr_dec_r(gb, &gb->b); break;
	case 0x15: instr_dec_r(gb, &gb->d); break;
	case 0x25: instr_dec_r(gb, &gb->h); break;
	case 0x35: instr_dec_aa(gb, gb->hl); break;

	case 0x06: instr_ld_r_v(gb, &gb->b, iv(gb)); break;
	case 0x16: instr_ld_r_v(gb, &gb->d, iv(gb)); break;
	case 0x26: instr_ld_r_v(gb, &gb->h, iv(gb)); break;
	case 0x36: instr_ld_aa_v(gb, gb->hl, iv(gb)); break;

	case 0x07: instr_rlca(gb); break;
	case 0x0F: instr_rrca(gb); break;
	case 0x17: instr_rla(gb); break;
	case 0x1F: instr_rra(gb); break;
	case 0x27: instr_daa_r(gb, &gb->a); break;
	case 0x2F: instr_cpl_r(gb, &gb->a); break;
	case 0x37: instr_scf(gb); break;
	case 0x3F: instr_ccf(gb); break;

	case 0x08: instr_ld_aa_vv(gb, iv16(gb), gb->sp); break;

	case 0x18: instr_jr(gb, true); break;
	case 0x20: instr_jr(gb, !gb->zero); break;
	case 0x28: instr_jr(gb, gb->zero); break;
	case 0x30: instr_jr(gb, !gb->carry); break;
	case 0x38: instr_jr(gb, gb->carry); break;

	case 0x09: instr_add_rr_vv(gb, &gb->hl, gb->bc); break;
	case 0x19: instr_add_rr_vv(gb, &gb->hl, gb->de); break;
	case 0x29: instr_add_rr_vv(gb, &gb->hl, gb->hl); break;
	case 0x39: instr_add_rr_vv(gb, &gb->hl, gb->sp); break;

	case 0x0A: instr_ld_r_aa(gb, &gb->a, gb->bc); break;
	case 0x1A: instr_ld_r_aa(gb, &gb->a, gb->de); break;
	case 0x2A: instr_ld_r_aa(gb, &gb->a, gb->hl++); break;
	case 0x3A: instr_ld_r_aa(gb, &gb->a, gb->hl--); break;

	case 0x0B: instr_dec_rr(gb, &gb->bc); break;
	case 0x1B: instr_dec_rr(gb, &gb->de); break;
	case 0x2B: instr_dec_rr(gb, &gb->hl); break;
	case 0x3B: instr_dec_rr(gb, &gb->sp); break;

	case 0x0C: instr_inc_r(gb, &gb->c); break;
	case 0x1C: instr_inc_r(gb, &gb->e); break;
	case 0x2C: instr_inc_r(gb, &gb->l); break;
	case 0x3C: instr_inc_r(gb, &gb->a); break;

	case 0x0D: instr_dec_r(gb, &gb->c); break;
	case 0x1D: instr_dec_r(gb, &gb->e); break;
	case 0x2D: instr_dec_r(gb, &gb->l); break;
	case 0x3D: instr_dec_r(gb, &gb->a); break;

	case 0x0E: instr_ld_r_v(gb, &gb->c, iv(gb)); break;
	case 0x1E: instr_ld_r_v(gb, &gb->e, iv(gb)); break;
	case 0x2E: instr_ld_r_v(gb, &gb->l, iv(gb)); break;
	case 0x3E: instr_ld_r_v(gb, &gb->a, iv(gb)); break;

	case 0x40: instr_ld_r_v(gb, &gb->b, gb->b); break;
	case 0x41: instr_ld_r_v(gb, &gb->b, gb->c); break;
	case 0x42: instr_ld_r_v(gb, &gb->b, gb->d); break;
	case 0x43: instr_ld_r_v(gb, &gb->b, gb->e); break;
	case 0x44: instr_ld_r_v(gb, &gb->b, gb->h); break;
	case 0x45: instr_ld_r_v(gb, &gb->b, gb->l); break;
	case 0x47: instr_ld_r_v(gb, &gb->b, gb->a); break;

	case 0x48: instr_ld_r_v(gb, &gb->c, gb->b); break;
	case 0x49: instr_ld_r_v(gb, &gb->c, gb->c); break;
	case 0x4A: instr_ld_r_v(gb, &gb->c, gb->d); break;
	case 0x4B: instr_ld_r_v(gb, &gb->c, gb->e); break;
	case 0x4C: instr_ld_r_v(gb, &gb->c, gb->h); break;
	case 0x4D: instr_ld_r_v(gb, &gb->c, gb->l); break;
	case 0x4F: instr_ld_r_v(gb, &gb->c, gb->a); break;

	case 0x50: instr_ld_r_v(gb, &gb->d, gb->b); break;
	case 0x51: instr_ld_r_v(gb, &gb->d, gb->c); break;
	case 0x52: instr_ld_r_v(gb, &gb->d, gb->d); break;
	case 0x53: instr_ld_r_v(gb, &gb->d, gb->e); break;
	case 0x54: instr_ld_r_v(gb, &gb->d, gb->h); break;
	case 0x55: instr_ld_r_v(gb, &gb->d, gb->l); break;
	case 0x57: instr_ld_r_v(gb, &gb->d, gb->a); break;

	case 0x58: instr_ld_r_v(gb, &gb->e, gb->b); break;
	case 0x59: instr_ld_r_v(gb, &gb->e, gb->c); break;
	case 0x5A: instr_ld_r_v(gb, &gb->e, gb->d); break;
	case 0x5B: instr_ld_r_v(gb, &gb->e, gb->e); break;
	case 0x5C: instr_ld_r_v(gb, &gb->e, gb->h); break;
	case 0x5D: instr_ld_r_v(gb, &gb->e, gb->l); break;
	case 0x5F: instr_ld_r_v(gb, &gb->e, gb->a); break;

	case 0x60: instr_ld_r_v(gb, &gb->h, gb->b); break;
	case 0x61: instr_ld_r_v(gb, &gb->h, gb->c); break;
	case 0x62: instr_ld_r_v(gb, &gb->h, gb->d); break;
	case 0x63: instr_ld_r_v(gb, &gb->h, gb->e); break;
	case 0x64: instr_ld_r_v(gb, &gb->h, gb->h); break;
	case 0x65: instr_ld_r_v(gb, &gb->h, gb->l); break;
	case 0x67: instr_ld_r_v(gb, &gb->h, gb->a); break;

	case 0x68: instr_ld_r_v(gb, &gb->l, gb->b); break;
	case 0x69: instr_ld_r_v(gb, &gb->l, gb->c); break;
	case 0x6A: instr_ld_r_v(gb, &gb->l, gb->d); break;
	case 0x6B: instr_ld_r_v(gb, &gb->l, gb->e); break;
	case 0x6C: instr_ld_r_v(gb, &gb->l, gb->h); break;
	case 0x6D: instr_ld_r_v(gb, &gb->l, gb->l); break;
	case 0x6F: instr_ld_r_v(gb, &gb->l, gb->a); break;

	case 0x70: instr_ld_aa_v(gb, gb->hl, gb->b); break;
	case 0x71: instr_ld_aa_v(gb, gb->hl, gb->c); break;
	case 0x72: instr_ld_aa_v(gb, gb->hl, gb->d); break;
	case 0x73: instr_ld_aa_v(gb, gb->hl, gb->e); break;
	case 0x74: instr_ld_aa_v(gb, gb->hl, gb->h); break;
	case 0x75: instr_ld_aa_v(gb, gb->hl, gb->l); break;
	case 0x77: instr_ld_aa_v(gb, gb->hl, gb->a); break;

	case 0x78: instr_ld_r_v(gb, &gb->a, gb->b); break;
	case 0x79: instr_ld_r_v(gb, &gb->a, gb->c); break;
	case 0x7A: instr_ld_r_v(gb, &gb->a, gb->d); break;
	case 0x7B: instr_ld_r_v(gb, &gb->a, gb->e); break;
	case 0x7C: instr_ld_r_v(gb, &gb->a, gb->h); break;
	case 0x7D: instr_ld_r_v(gb, &gb->a, gb->l); break;
	case 0x7F: instr_ld_r_v(gb, &gb->a, gb->a); break;

	case 0x46: instr_ld_r_aa(gb, &gb->b, gb->hl); break;
	case 0x4E: instr_ld_r_aa(gb, &gb->c, gb->hl); break;
	case 0x56: instr_ld_r_aa(gb, &gb->d, gb->hl); break;
	case 0x5E: instr_ld_r_aa(gb, &gb->e, gb->hl); break;
	case 0x66: instr_ld_r_aa(gb, &gb->h, gb->hl); break;
	case 0x6E: instr_ld_r_aa(gb, &gb->l, gb->hl); break;
	case 0x7E: instr_ld_r_aa(gb, &gb->a, gb->hl); break;

	case 0x80: instr_add_r_v(gb, &gb->a, gb->b); break;
	case 0x81: instr_add_r_v(gb, &gb->a, gb->c); break;
	case 0x82: instr_add_r_v(gb, &gb->a, gb->d); break;
	case 0x83: instr_add_r_v(gb, &gb->a, gb->e); break;
	case 0x84: instr_add_r_v(gb, &gb->a, gb->h); break;
	case 0x85: instr_add_r_v(gb, &gb->a, gb->l); break;
	case 0x87: instr_add_r_v(gb, &gb->a, gb->a); break;

	case 0x88: instr_adc_r_v(gb, &gb->a, gb->b); break;
	case 0x89: instr_adc_r_v(gb, &gb->a, gb->c); break;
	case 0x8A: instr_adc_r_v(gb, &gb->a, gb->d); break;
	case 0x8B: instr_adc_r_v(gb, &gb->a, gb->e); break;
	case 0x8C: instr_adc_r_v(gb, &gb->a, gb->h); break;
	case 0x8D: instr_adc_r_v(gb, &gb->a, gb->l); break;
	case 0x8F: instr_adc_r_v(gb, &gb->a, gb->a); break;

	case 0x90: instr_sub_r_v(gb, &gb->a, gb->b); break;
	case 0x91: instr_sub_r_v(gb, &gb->a, gb->c); break;
	case 0x92: instr_sub_r_v(gb, &gb->a, gb->d); break;
	case 0x93: instr_sub_r_v(gb, &gb->a, gb->e); break;
	case 0x94: instr_sub_r_v(gb, &gb->a, gb->h); break;
	case 0x95: instr_sub_r_v(gb, &gb->a, gb->l); break;
	case 0x97: instr_sub_r_v(gb, &gb->a, gb->a); break;

	case 0x98: instr_sbc_r_v(gb, &gb->a, gb->b); break;
	case 0x99: instr_sbc_r_v(gb, &gb->a, gb->c); break;
	case 0x9A: instr_sbc_r_v(gb, &gb->a, gb->d); break;
	case 0x9B: instr_sbc_r_v(gb, &gb->a, gb->e); break;
	case 0x9C: instr_sbc_r_v(gb, &gb->a, gb->h); break;
	case 0x9D: instr_sbc_r_v(gb, &gb->a, gb->l); break;
	case 0x9F: instr_sbc_r_v(gb, &gb->a, gb->a); break;

	case 0xA0: instr_and_r_v(gb, &gb->a, gb->b); break;
	case 0xA1: instr_and_r_v(gb, &gb->a, gb->c); break;
	case 0xA2: instr_and_r_v(gb, &gb->a, gb->d); break;
	case 0xA3: instr_and_r_v(gb, &gb->a, gb->e); break;
	case 0xA4: instr_and_r_v(gb, &gb->a, gb->h); break;
	case 0xA5: instr_and_r_v(gb, &gb->a, gb->l); break;
	case 0xA7: instr_and_r_v(gb, &gb->a, gb->a); break;

	case 0xA8: instr_xor_r_v(gb, &gb->a, gb->b); break;
	case 0xA9: instr_xor_r_v(gb, &gb->a, gb->c); break;
	case 0xAA: instr_xor_r_v(gb, &gb->a, gb->d); break;
	case 0xAB: instr_xor_r_v(gb, &gb->a, gb->e); break;
	case 0xAC: instr_xor_r_v(gb, &gb->a, gb->h); break;
	case 0xAD: instr_xor_r_v(gb, &gb->a, gb->l); break;
	case 0xAF: instr_xor_r_v(gb, &gb->a, gb->a); break;

	case 0xB0: instr_or_r_v(gb, &gb->a, gb->b); break;
	case 0xB1: instr_or_r_v(gb, &gb->a, gb->c); break;
	case 0xB2: instr_or_r_v(gb, &gb->a, gb->d); break;
	case 0xB3: instr_or_r_v(gb, &gb->a, gb->e); break;
	case 0xB4: instr_or_r_v(gb, &gb->a, gb->h); break;
	case 0xB5: instr_or_r_v(gb, &gb->a, gb->l); break;
	case 0xB7: instr_or_r_v(gb, &gb->a, gb->a); break;

	case 0xB8: instr_cp_r_v(gb, &gb->a, gb->b); break;
	case 0xB9: instr_cp_r_v(gb, &gb->a, gb->c); break;
	case 0xBA: instr_cp_r_v(gb, &gb->a, gb->d); break;
	case 0xBB: instr_cp_r_v(gb, &gb->a, gb->e); break;
	case 0xBC: instr_cp_r_v(gb, &gb->a, gb->h); break;
	case 0xBD: instr_cp_r_v(gb, &gb->a, gb->l); break;
	case 0xBF: instr_cp_r_v(gb, &gb->a, gb->a); break;

	case 0x86: instr_add_r_aa(gb, &gb->a, gb->hl); break;
	case 0x8E: instr_adc_r_aa(gb, &gb->a, gb->hl); break;
	case 0x96: instr_sub_r_aa(gb, &gb->a, gb->hl); break;
	case 0x9E: instr_sbc_r_aa(gb, &gb->a, gb->hl); break;
	case 0xA6: instr_and_r_aa(gb, &gb->a, gb->hl); break;
	case 0xAE: instr_xor_r_aa(gb, &gb->a, gb->hl); break;
	case 0xB6: instr_or_r_aa(gb, &gb->a, gb->hl); break;
	case 0xBE: instr_cp_r_aa(gb, &gb->a, gb->hl); break;

	case 0xC6: instr_add_r_v(gb, &gb->a, iv(gb)); break;
	case 0xCE: instr_adc_r_v(gb, &gb->a, iv(gb)); break;
	case 0xD6: instr_sub_r_v(gb, &gb->a, iv(gb)); break;
	case 0xDE: instr_sbc_r_v(gb, &gb->a, iv(gb)); break;
	case 0xE6: instr_and_r_v(gb, &gb->a, iv(gb)); break;
	case 0xEE: instr_xor_r_v(gb, &gb->a, iv(gb)); break;
	case 0xF6: instr_or_r_v(gb, &gb->a, iv(gb)); break;
	case 0xFE: instr_cp_r_v(gb, &gb->a, iv(gb)); break;

	case 0xC0: instr_ret(gb, !gb->zero); break;
	case 0xC8: instr_ret(gb, gb->zero); break;
	case 0xC9: instr_ret(gb, true); break;
	case 0xD0: instr_ret(gb, !gb->carry); break;
	case 0xD8: instr_ret(gb, gb->carry); break;
	case 0xD9: instr_reti(gb); break;

	case 0xE0: instr_ld_aa_v(gb, 0xFF00 | iv(gb), gb->a); break;
	case 0xE2: instr_ld_aa_v(gb, 0xFF00 | gb->c, gb->a); break;
	case 0xEA: instr_ld_aa_v(gb, iv16(gb), gb->a); break;
	case 0xF0: instr_ld_r_aa(gb, &gb->a, 0xFF00 | iv(gb)); break;
	case 0xF2: instr_ld_r_aa(gb, &gb->a, 0xFF00 | gb->c); break;
	case 0xFA: instr_ld_r_aa(gb, &gb->a, iv16(gb)); break;

	case 0xC1: instr_pop(gb, &gb->bc); break;
	case 0xD1: instr_pop(gb, &gb->de); break;
	case 0xE1: instr_pop(gb, &gb->hl); break;
	case 0xF1: instr_pop(gb, &gb->af); gb->f &= 0xF0; break;

	case 0xC2: instr_jp(gb, !gb->zero); break;
	case 0xC3: instr_jp(gb, true); break;
	case 0xCA: instr_jp(gb, gb->zero); break;
	case 0xD2: instr_jp(gb, !gb->carry); break;
	case 0xDA: instr_jp(gb, gb->carry); break;

	case 0xC4: instr_call(gb, !gb->zero); break;
	case 0xCC: instr_call(gb, gb->zero); break;
	case 0xCD: instr_call(gb, true); break;
	case 0xD4: instr_call(gb, !gb->carry); break;
	case 0xDC: instr_call(gb, gb->carry); break;

	case 0xC5: instr_push(gb, gb->bc); break;
	case 0xD5: instr_push(gb, gb->de); break;
	case 0xE5: instr_push(gb, gb->hl); break;
	case 0xF5: instr_push(gb, gb->af); break;

	case 0xC7: instr_rst(gb, 0x0000); break;
	case 0xCF: instr_rst(gb, 0x0008); break;
	case 0xD7: instr_rst(gb, 0x0010); break;
	case 0xDF: instr_rst(gb, 0x0018); break;
	case 0xE7: instr_rst(gb, 0x0020); break;
	case 0xEF: instr_rst(gb, 0x0028); break;
	case 0xF7: instr_rst(gb, 0x0030); break;
	case 0xFF: instr_rst(gb, 0x0038); break;

	case 0xE8: instr_ld_rr_vv_jr(gb, &gb->sp, gb->sp); break;
	case 0xF8: instr_ld_rr_vv_jr(gb, &gb->hl, gb->sp); break;

	case 0xE9: instr_ld_rr_vv(gb, &gb->pc, gb->hl); break;
	case 0xF9: instr_ld_rr_vv(gb, &gb->sp, gb->hl); break;

	case 0xD3: case 0xDB: case 0xDD:
	case 0xE3: case 0xE4: case 0xEB: case 0xEC: case 0xED:
	case 0xF4: case 0xFC: case 0xFD:
		instr_undefined(gb, opcode);
		break;

	default:
		GBLOG("Opcode not yet implemented: %02X", opcode);
		gb->cpu_status = GAMEBOY_CPU_CRASHED;
	}
}

void gameboy_tick(struct gameboy *gb)
{
	switch (gb->cpu_status) {
	case GAMEBOY_CPU_CRASHED:
	case GAMEBOY_CPU_HALTED:
	case GAMEBOY_CPU_STOPPED:
		exit(1); // TODO: tmp
		break;
	case GAMEBOY_CPU_RUNNING:
		process_opcode(gb, iv(gb));
		break;
	}
}
