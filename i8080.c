#include "i8080/i8080.h"

// table represents cpu cycles taken by each instruction
// duration of conditional calls and returns is different
// when action is taken or not, so remainder is added in individual functions
static const uint8_t OPCODE_CYCLES[] = {
    //  0   1   2   3   4   5   6   7   8   9   a	b	c	d
    //  e	f
    4, 10, 7,  5,  5,  5,  7,  4,  4, 10, 7,  5,  5,  5,  7, 4,   // 0
    4, 10, 7,  5,  5,  5,  7,  4,  4, 10, 7,  5,  5,  5,  7, 4,   // 1
    4, 10, 16, 5,  5,  5,  7,  4,  4, 10, 16, 5,  5,  5,  7, 4,   // 2
    4, 10, 13, 5,  10, 10, 10, 4,  4, 10, 13, 5,  5,  5,  7, 4,   // 3
    5, 5,  5,  5,  5,  5,  7,  5,  5, 5,  5,  5,  5,  5,  7, 5,   // 4
    5, 5,  5,  5,  5,  5,  7,  5,  5, 5,  5,  5,  5,  5,  7, 5,   // 5
    5, 5,  5,  5,  5,  5,  7,  5,  5, 5,  5,  5,  5,  5,  7, 5,   // 6
    7, 7,  7,  7,  7,  7,  7,  7,  5, 5,  5,  5,  5,  5,  7, 5,   // 7
    4, 4,  4,  4,  4,  4,  7,  4,  4, 4,  4,  4,  4,  4,  7, 4,   // 8
    4, 4,  4,  4,  4,  4,  7,  4,  4, 4,  4,  4,  4,  4,  7, 4,   // 9
    4, 4,  4,  4,  4,  4,  7,  4,  4, 4,  4,  4,  4,  4,  7, 4,   // a
    4, 4,  4,  4,  4,  4,  7,  4,  4, 4,  4,  4,  4,  4,  7, 4,   // b
    5, 10, 10, 10, 11, 11, 7,  11, 5, 10, 10, 10, 11, 17, 7, 11,  // c
    5, 10, 10, 10, 11, 11, 7,  11, 5, 10, 10, 10, 11, 17, 7, 11,  // d
    5, 10, 10, 18, 11, 11, 7,  11, 5, 5,  10, 5,  11, 17, 7, 11,  // e
    5, 10, 10, 4,  11, 11, 7,  11, 5, 5,  10, 4,  11, 17, 7, 11   // f
};

static uint16_t get_regpair_val(const regpair_t* pair) {
  const uint16_t result = (*(*pair).first << 8) | *(*pair).second;

  return result;
}

// returns memory reference M (byte at address made up of HL reg. pair)
static uint8_t* mem_ref_m(i8080_t* state) {
  const uint16_t address = (state->h << 8) | state->l;

  uint8_t* M = &state->external_memory[address];

  return M;
}

static bool should_set_parity_bit(const uint8_t byte) {
  int count = 0;  // holds count of 1's

  for (int bit = 0; bit < 8; bit++)
    count += (byte >> bit) & 1;

  return (count % 2 == 0);
}

static void set_flags_zsp(i8080_t* state, uint8_t byte) {
  state->cb.flags.z = (0 == byte);
  state->cb.flags.s = (0x80 & byte) != 0;
  state->cb.flags.p = should_set_parity_bit(byte);
}

static uint8_t add_bytes_set_flags(i8080_t* state,
                                   const uint8_t augend,
                                   const uint8_t addend,
                                   const bool carry) {
  const int16_t result = augend + addend + carry;

  state->cb.flags.c = (0x100 & result) != 0;
  state->cb.flags.ac = ((augend ^ result ^ addend) & 0x10) != 0;
  set_flags_zsp(state, result & 0xff);

  return result & 0xff;
}

static uint8_t sub_bytes_set_flags(i8080_t* state,
                                   const uint8_t minuend,
                                   const uint8_t subtrahend,
                                   const bool carry) {
  const int16_t result = minuend - subtrahend - carry;

  state->cb.flags.c = (0x100 & result) != 0;
  state->cb.flags.ac = (~(minuend ^ result ^ subtrahend) & 0x10) != 0;
  set_flags_zsp(state, result & 0xff);

  return result & 0xff;
}

void init_conditionbits(conditionbits_t* cb) {
  cb->byte = 0;
  cb->flags.s = 0;
  cb->flags.z = 0;
  cb->flags.bit5 = 0;  // always 0
  cb->flags.ac = 0;
  cb->flags.bit3 = 0;  // always 0
  cb->flags.p = 0;
  cb->flags.bit1 = 1;  // always 1
  cb->flags.c = 0;

  return;
}

void init_i8080(i8080_t* state) {
  // registers
  state->a = 0;
  state->b = 0;
  state->c = 0;
  state->d = 0;
  state->e = 0;
  state->h = 0;
  state->l = 0;
  state->pc = 0;
  state->sp = 0;

  // flags
  conditionbits_t cb;
  init_conditionbits(&cb);
  state->cb = cb;

  state->cycles = 0;
  state->ie = 0;

  state->external_memory = NULL;
}

uint8_t i8080_read_byte(i8080_t* state, const uint16_t address) {
  return state->external_memory[address];
}

void i8080_write_byte(i8080_t* state,
                      const uint16_t address,
                      const uint8_t byte) {
  state->external_memory[address] = byte;
}

void i8080_interrupt(i8080_t* state, uint8_t low, uint8_t high) {
  // current pc pushed to stack to continue execution when interrupt is finished
  i8080_write_byte(state, state->sp - 1, state->pc >> 8);
  i8080_write_byte(state, state->sp - 2, state->pc & 0xff);

  state->sp -= 2;

  state->pc = (high << 8) | low;
}

void i8080_stc(i8080_t* state) {
  state->cb.flags.c = 1;

  state->pc++;
}

void i8080_cmc(i8080_t* state) {
  state->cb.flags.c = ~state->cb.flags.c;

  state->pc++;
}

void i8080_inr(i8080_t* state, uint8_t* reg) {
  const uint8_t res = *reg + 1;

  state->cb.flags.ac = ((0x0f & res) == 0);
  set_flags_zsp(state, res);

  *reg = res;
  state->pc++;
}

void i8080_dcr(i8080_t* state, uint8_t* reg) {
  const uint8_t res = *reg - 1;

  state->cb.flags.ac = !((0x0f & res) == 0x0f);
  set_flags_zsp(state, res);

  *reg = res;
  state->pc++;
}

void i8080_cma(i8080_t* state) {
  state->a = ~state->a;

  state->pc++;
}

void i8080_daa(i8080_t* state) {
  bool carry = state->cb.flags.c;
  uint8_t value_to_add = 0;

  const uint8_t lsb = state->a & 0x0F;
  const uint8_t msb = state->a >> 4;

  if (state->cb.flags.ac || lsb > 9) {
    value_to_add += 0x06;
  }
  if (state->cb.flags.c || msb > 9 || (msb >= 9 && lsb > 9)) {
    value_to_add += 0x60;
    carry = 1;
  }

  state->a = add_bytes_set_flags(state, state->a, value_to_add, 0);

  state->cb.flags.c = carry;

  state->pc++;
}

void i8080_nop(i8080_t* state) {
  state->pc++;
}

void i8080_mov(i8080_t* state, uint8_t* dst, const uint8_t* src) {
  *dst = *src;

  state->pc++;
}

void i8080_stax(i8080_t* state, const regpair_t pair) {
  const uint16_t address = (*pair.first << 8) | *pair.second;

  i8080_write_byte(state, address, state->a);

  state->pc++;
}

void i8080_ldax(i8080_t* state, const regpair_t pair) {
  const uint16_t address = (*pair.first << 8) | *pair.second;

  state->a = i8080_read_byte(state, address);

  state->pc++;
}

void i8080_add(i8080_t* state, const uint8_t* reg) {
  state->a = add_bytes_set_flags(state, state->a, *reg, 0);

  state->pc++;
}

void i8080_adc(i8080_t* state, const uint8_t* reg) {
  state->a = add_bytes_set_flags(state, state->a, *reg, state->cb.flags.c);

  state->pc++;
}

void i8080_sub(i8080_t* state, const uint8_t* reg) {
  state->a = sub_bytes_set_flags(state, state->a, *reg, 0);

  state->pc++;
}

void i8080_sbb(i8080_t* state, const uint8_t* reg) {
  state->a = sub_bytes_set_flags(state, state->a, *reg, state->cb.flags.c);

  state->pc++;
}

void i8080_ana(i8080_t* state, const uint8_t* reg) {
  const uint8_t result = state->a & *reg;

  state->cb.flags.c = 0;  // reset
  state->cb.flags.ac = (0x08 & (state->a | *reg)) != 0;
  set_flags_zsp(state, result);

  state->a = result;
  state->pc++;
}

void i8080_xra(i8080_t* state, const uint8_t* reg) {
  state->a = state->a ^ *reg;

  state->cb.flags.c = 0;   // reset
  state->cb.flags.ac = 0;  // reset
  set_flags_zsp(state, state->a);

  state->pc++;
}

void i8080_ora(i8080_t* state, const uint8_t* reg) {
  state->a = state->a | *reg;

  state->cb.flags.c = 0;   // reset
  state->cb.flags.ac = 0;  // reset
  set_flags_zsp(state, state->a);

  state->pc++;
}

void i8080_cmp(i8080_t* state, const uint8_t* reg) {
  sub_bytes_set_flags(state, state->a, *reg, 0);

  state->pc++;
}

void i8080_rlc(i8080_t* state) {
  bool hbit = (0x80 & state->a) != 0;

  state->a = (state->a << 1) | hbit;

  state->cb.flags.c = hbit;

  state->pc++;
}

void i8080_rrc(i8080_t* state) {
  bool lbit = (0x01 & state->a) != 0;

  state->a = (lbit << 7) | (state->a >> 1);

  state->cb.flags.c = lbit;

  state->pc++;
}

void i8080_ral(i8080_t* state) {
  bool hbit = (0x80 & state->a) != 0;

  state->a = (state->a << 1) | state->cb.flags.c;

  state->cb.flags.c = hbit;

  state->pc++;
}

void i8080_rar(i8080_t* state) {
  bool lbit = (0x01 & state->a) != 0;

  state->a = (state->cb.flags.c << 7) | (state->a >> 1);

  state->cb.flags.c = lbit;

  state->pc++;
}

void i8080_push(i8080_t* state, const regpair_t pair) {
  state->sp -= 2;

  i8080_write_byte(state, state->sp, *pair.second);
  i8080_write_byte(state, state->sp + 1, *pair.first);

  state->pc++;
}

static void i8080_push_psw(i8080_t* state) {
  uint8_t flags = 0x02;  // bit 1 always set
  if (state->cb.flags.s)
    flags |= 0x80;
  if (state->cb.flags.z)
    flags |= 0x40;
  if (state->cb.flags.ac)
    flags |= 0x10;
  if (state->cb.flags.p)
    flags |= 0x04;
  if (state->cb.flags.c)
    flags |= 0x01;

  state->sp -= 2;

  i8080_write_byte(state, state->sp, flags);
  i8080_write_byte(state, state->sp + 1, state->a);

  state->pc++;
}

void i8080_pop(i8080_t* state, regpair_t pair) {
  *pair.second = i8080_read_byte(state, state->sp);
  *pair.first = i8080_read_byte(state, state->sp + 1);

  state->sp += 2;

  state->pc++;
}

void i8080_pop_psw(i8080_t* state) {
  uint8_t flags = i8080_read_byte(state, state->sp);
  state->cb.flags.s = (flags & 0x80) != 0;
  state->cb.flags.z = (flags & 0x40) != 0;
  state->cb.flags.ac = (flags & 0x10) != 0;
  state->cb.flags.p = (flags & 0x04) != 0;
  state->cb.flags.c = (flags & 0x01) != 0;
  state->cb.flags.bit5 = 0;
  state->cb.flags.bit3 = 0;
  state->cb.flags.bit1 = 1;

  state->a = i8080_read_byte(state, state->sp + 1);

  state->sp += 2;

  state->pc++;
}

void i8080_dad(i8080_t* state, const uint16_t addend) {
  const uint16_t hl = (state->h << 8) | state->l;

  const uint32_t result = hl + addend;

  state->h = result >> 8;
  state->l = result;

  state->cb.flags.c = result > 0xffff;

  state->pc++;
}

void i8080_inx(i8080_t* state, regpair_t pair, uint16_t* sp) {
  if (sp) {
    state->sp = state->sp + 1;
  } else {
    uint8_t low = *pair.second;
    uint8_t high = *pair.first;
    low = low + 1;
    if (0 == low)
      high = high + 1;
    *pair.first = high;
    *pair.second = low;
  }

  state->pc++;
}

void i8080_dcx(i8080_t* state, regpair_t pair, uint16_t* sp) {
  if (sp)
    state->sp--;
  else {
    uint16_t combined = (*pair.first << 8) | *pair.second;
    combined = combined - 1;
    *pair.first = (combined >> 8);
    *pair.second = combined;
  }

  state->pc++;
}

void i8080_xchg(i8080_t* state) {
  uint8_t temp1 = state->h;
  uint8_t temp2 = state->l;

  state->h = state->d;
  state->l = state->e;

  state->d = temp1;
  state->e = temp2;

  state->pc++;
}

void i8080_xthl(i8080_t* state) {
  uint8_t temp1 = state->l;
  uint8_t temp2 = state->h;

  state->l = i8080_read_byte(state, state->sp);
  state->h = i8080_read_byte(state, state->sp + 1);

  i8080_write_byte(state, state->sp, temp1);
  i8080_write_byte(state, state->sp + 1, temp2);

  state->pc++;
}

void i8080_sphl(i8080_t* state) {
  state->sp = (state->h << 8) | state->l;

  state->pc++;
}

void i8080_lxi(i8080_t* state,
               regpair_t pair,
               uint16_t* sp,
               uint8_t low,
               uint8_t high) {
  if (sp)
    state->sp = (high << 8) | low;
  else {
    *pair.first = high;
    *pair.second = low;
  }

  state->pc += 3;
}

void i8080_mvi(i8080_t* state, uint8_t* reg, uint8_t byte) {
  *reg = byte;

  state->pc += 2;
}

void i8080_adi(i8080_t* state, const uint8_t byte) {
  state->a = add_bytes_set_flags(state, state->a, byte, 0);

  state->pc += 2;
}

void i8080_aci(i8080_t* state, const uint8_t byte) {
  state->a = add_bytes_set_flags(state, state->a, byte, state->cb.flags.c);

  state->pc += 2;
}

void i8080_sui(i8080_t* state, const uint8_t byte) {
  state->a = sub_bytes_set_flags(state, state->a, byte, 0);

  state->pc += 2;
}

void i8080_sbi(i8080_t* state, const uint8_t byte) {
  state->a = sub_bytes_set_flags(state, state->a, byte, state->cb.flags.c);

  state->pc += 2;
}

void i8080_ani(i8080_t* state, const uint8_t byte) {
  const uint8_t result = state->a & byte;

  state->cb.flags.c = 0;  // reset
  state->cb.flags.ac = (0x08 & (state->a | byte)) != 0;
  set_flags_zsp(state, result);

  state->a = result;
  state->pc += 2;
}

void i8080_xri(i8080_t* state, const uint8_t byte) {
  state->a = state->a ^ byte;

  state->cb.flags.c = 0;   // reset
  state->cb.flags.ac = 0;  // reset
  set_flags_zsp(state, state->a);

  state->pc += 2;
}

void i8080_ori(i8080_t* state, uint8_t byte) {
  state->a = state->a | byte;

  state->cb.flags.c = 0;   // reset
  state->cb.flags.ac = 0;  // reset
  set_flags_zsp(state, state->a);

  state->pc += 2;
}

void i8080_cpi(i8080_t* state, uint8_t byte) {
  sub_bytes_set_flags(state, state->a, byte, 0);

  state->pc += 2;
}

void i8080_sta(i8080_t* state, uint8_t low, uint8_t high) {
  const uint16_t address = (high << 8) | low;

  i8080_write_byte(state, address, state->a);

  state->pc += 3;
}

void i8080_lda(i8080_t* state, uint8_t low, uint8_t high) {
  const uint16_t address = (high << 8) | low;

  state->a = i8080_read_byte(state, address);

  state->pc += 3;
}

void i8080_shld(i8080_t* state, uint8_t low, uint8_t high) {
  const uint16_t address = (high << 8) | low;

  i8080_write_byte(state, address, state->l);
  i8080_write_byte(state, address + 1, state->h);

  state->pc += 3;
}

void i8080_lhld(i8080_t* state, uint8_t low, uint8_t high) {
  const uint16_t address = (high << 8) | low;

  state->l = i8080_read_byte(state, address);
  state->h = i8080_read_byte(state, address + 1);

  state->pc += 3;
}

void i8080_pchl(i8080_t* state) {
  state->pc = (state->h << 8) | state->l;
}

void i8080_jmp(i8080_t* state, uint8_t low, uint8_t high) {
  state->pc = (high << 8) | low;
}

static void i8080_cond_jmp(i8080_t* state,
                           uint8_t low,
                           uint8_t high,
                           bool cond) {
  state->pc += 3;
  if (cond)
    i8080_jmp(state, low, high);
}

void i8080_jc(i8080_t* state, uint8_t low, uint8_t high) {
  i8080_cond_jmp(state, low, high, state->cb.flags.c);
}

void i8080_jnc(i8080_t* state, uint8_t low, uint8_t high) {
  i8080_cond_jmp(state, low, high, !state->cb.flags.c);
}

void i8080_jz(i8080_t* state, uint8_t low, uint8_t high) {
  i8080_cond_jmp(state, low, high, state->cb.flags.z);
}

void i8080_jnz(i8080_t* state, uint8_t low, uint8_t high) {
  i8080_cond_jmp(state, low, high, !state->cb.flags.z);
}

void i8080_jm(i8080_t* state, uint8_t low, uint8_t high) {
  i8080_cond_jmp(state, low, high, state->cb.flags.s);
}

void i8080_jp(i8080_t* state, uint8_t low, uint8_t high) {
  i8080_cond_jmp(state, low, high, !state->cb.flags.s);
}

void i8080_jpe(i8080_t* state, uint8_t low, uint8_t high) {
  i8080_cond_jmp(state, low, high, state->cb.flags.p);
}

void i8080_jpo(i8080_t* state, uint8_t low, uint8_t high) {
  i8080_cond_jmp(state, low, high, !state->cb.flags.p);
}

void i8080_call(i8080_t* state, uint8_t low, uint8_t high) {
  // pc of next instruction
  const uint16_t next_pc = state->pc + 3;

  // return address (pc of next instr.) pushed to stack
  i8080_write_byte(state, state->sp - 1, next_pc >> 8);
  i8080_write_byte(state, state->sp - 2, next_pc & 0xff);

  state->sp -= 2;  // new sp pos.

  state->pc = (high << 8) | low;
}

static void i8080_cond_call(i8080_t* state,
                            uint8_t low,
                            uint8_t high,
                            bool cond) {
  if (cond) {
    state->cycles += 6;  // 17 total
    i8080_call(state, low, high);
  } else
    state->pc += 3;
}

void i8080_cc(i8080_t* state, uint8_t low, uint8_t high) {
  i8080_cond_call(state, low, high, state->cb.flags.c);
}

void i8080_cnc(i8080_t* state, uint8_t low, uint8_t high) {
  i8080_cond_call(state, low, high, !state->cb.flags.c);
}

void i8080_cz(i8080_t* state, uint8_t low, uint8_t high) {
  i8080_cond_call(state, low, high, state->cb.flags.z);
}

void i8080_cnz(i8080_t* state, uint8_t low, uint8_t high) {
  i8080_cond_call(state, low, high, !state->cb.flags.z);
}

void i8080_cm(i8080_t* state, uint8_t low, uint8_t high) {
  i8080_cond_call(state, low, high, state->cb.flags.s);
}

void i8080_cp(i8080_t* state, uint8_t low, uint8_t high) {
  i8080_cond_call(state, low, high, !state->cb.flags.s);
}

void i8080_cpe(i8080_t* state, uint8_t low, uint8_t high) {
  i8080_cond_call(state, low, high, state->cb.flags.p);
}

void i8080_cpo(i8080_t* state, uint8_t low, uint8_t high) {
  i8080_cond_call(state, low, high, !state->cb.flags.p);
}

void i8080_ret(i8080_t* state) {
  // retrieve address from stack
  uint8_t low = i8080_read_byte(state, state->sp);
  uint8_t high = i8080_read_byte(state, state->sp + 1);
  state->pc = (high << 8) | low;

  state->sp += 2;  // pop address off stack
}

void i8080_cond_ret(i8080_t* state, bool cond) {
  if (cond) {
    state->cycles += 6;  // 11 cycles total
    i8080_ret(state);
  } else
    state->pc++;
}

void i8080_rc(i8080_t* state) {
  i8080_cond_ret(state, state->cb.flags.c);
}

void i8080_rnc(i8080_t* state) {
  i8080_cond_ret(state, !state->cb.flags.c);
}

void i8080_rz(i8080_t* state) {
  i8080_cond_ret(state, state->cb.flags.z);
}

void i8080_rnz(i8080_t* state) {
  i8080_cond_ret(state, !state->cb.flags.z);
}

void i8080_rm(i8080_t* state) {
  i8080_cond_ret(state, state->cb.flags.s);
}

void i8080_rp(i8080_t* state) {
  i8080_cond_ret(state, !state->cb.flags.s);
}

void i8080_rpe(i8080_t* state) {
  i8080_cond_ret(state, state->cb.flags.p);
}

void i8080_rpo(i8080_t* state) {
  i8080_cond_ret(state, !state->cb.flags.p);
}

void i8080_rst(i8080_t* state, uint8_t rst_num) {
  switch (rst_num) {
    case 0:
      i8080_interrupt(state, 0x00, 0x00);
      break;  // RST 0 (0x00)
    case 1:
      i8080_interrupt(state, 0x08, 0x00);
      break;  // RST 1 (0x08)
    case 2:
      i8080_interrupt(state, 0x10, 0x00);
      break;  // RST 2 (0x10)
    case 3:
      i8080_interrupt(state, 0x18, 0x00);
      break;  // RST 3 (0x18)
    case 4:
      i8080_interrupt(state, 0x20, 0x00);
      break;  // RST 4 (0x20)
    case 5:
      i8080_interrupt(state, 0x28, 0x00);
      break;  // RST 5 (0x28)
    case 6:
      i8080_interrupt(state, 0x30, 0x00);
      break;  // RST 6 (0x30)
    case 7:
      i8080_interrupt(state, 0x38, 0x00);
      break;  // RST 7 (0x38)
  }
}

void i8080_ei(i8080_t* state) {
  state->ie = 1;

  state->pc++;
}

void i8080_di(i8080_t* state) {
  state->ie = 0;

  state->pc++;
}

void i8080_step(i8080_t* state) {
  // shorthand identifiers for registers, makes switch more readable
  uint8_t* A = &state->a;
  uint8_t* B = &state->b;
  uint8_t* C = &state->c;
  uint8_t* D = &state->d;
  uint8_t* E = &state->e;
  uint8_t* H = &state->h;
  uint8_t* L = &state->l;
  uint8_t* M = mem_ref_m(state);
  uint16_t* SP = &state->sp;
  regpair_t BC = {&state->b, &state->c};
  regpair_t DE = {&state->d, &state->e};
  regpair_t HL = {&state->h, &state->l};

  uint8_t* opcode = &state->external_memory[state->pc];

  state->cycles += OPCODE_CYCLES[*opcode];

  switch (*opcode) {
    case 0x00:
      i8080_nop(state);
      break;
    case 0x01:
      i8080_lxi(state, BC, NULL, opcode[1], opcode[2]);
      break;
    case 0x02:
      i8080_stax(state, BC);
      break;
    case 0x03:
      i8080_inx(state, BC, NULL);
      break;
    case 0x04:
      i8080_inr(state, B);
      break;
    case 0x05:
      i8080_dcr(state, B);
      break;
    case 0x06:
      i8080_mvi(state, B, opcode[1]);
      break;
    case 0x07:
      i8080_rlc(state);
      break;
    case 0x08:
      i8080_nop(state);
      break;
    case 0x09:
      i8080_dad(state, get_regpair_val(&BC));
      break;
    case 0x0a:
      i8080_ldax(state, BC);
      break;
    case 0x0b:
      i8080_dcx(state, BC, NULL);
      break;
    case 0x0c:
      i8080_inr(state, C);
      break;
    case 0x0d:
      i8080_dcr(state, C);
      break;
    case 0x0e:
      i8080_mvi(state, C, opcode[1]);
      break;
    case 0x0f:
      i8080_rrc(state);
      break;

    case 0x10:
      i8080_nop(state);
      break;
    case 0x11:
      i8080_lxi(state, DE, NULL, opcode[1], opcode[2]);
      break;
    case 0x12:
      i8080_stax(state, DE);
      break;
    case 0x13:
      i8080_inx(state, DE, NULL);
      break;
    case 0x14:
      i8080_inr(state, D);
      break;
    case 0x15:
      i8080_dcr(state, D);
      break;
    case 0x16:
      i8080_mvi(state, D, opcode[1]);
      break;
    case 0x17:
      i8080_ral(state);
      break;
    case 0x18:
      i8080_nop(state);
      break;
    case 0x19:
      i8080_dad(state, get_regpair_val(&DE));
      break;
    case 0x1a:
      i8080_ldax(state, DE);
      break;
    case 0x1b:
      i8080_dcx(state, DE, NULL);
      break;
    case 0x1c:
      i8080_inr(state, E);
      break;
    case 0x1d:
      i8080_dcr(state, E);
      break;
    case 0x1e:
      i8080_mvi(state, E, opcode[1]);
      break;
    case 0x1f:
      i8080_rar(state);
      break;

    case 0x20:
      i8080_nop(state);
      break;
    case 0x21:
      i8080_lxi(state, HL, NULL, opcode[1], opcode[2]);
      break;
    case 0x22:
      i8080_shld(state, opcode[1], opcode[2]);
      break;
    case 0x23:
      i8080_inx(state, HL, NULL);
      break;
    case 0x24:
      i8080_inr(state, H);
      break;
    case 0x25:
      i8080_dcr(state, H);
      break;
    case 0x26:
      i8080_mvi(state, H, opcode[1]);
      break;
    case 0x27:
      i8080_daa(state);
      break;
    case 0x28:
      i8080_nop(state);
      break;
    case 0x29:
      i8080_dad(state, get_regpair_val(&HL));
      break;
    case 0x2a:
      i8080_lhld(state, opcode[1], opcode[2]);
      break;
    case 0x2b:
      i8080_dcx(state, HL, NULL);
      break;
    case 0x2c:
      i8080_inr(state, L);
      break;
    case 0x2d:
      i8080_dcr(state, L);
      break;
    case 0x2e:
      i8080_mvi(state, L, opcode[1]);
      break;
    case 0x2f:
      i8080_cma(state);
      break;

    case 0x30:
      i8080_nop(state);
      break;
    case 0x31:
      i8080_lxi(state, (regpair_t){}, SP, opcode[1], opcode[2]);
      break;
    case 0x32:
      i8080_sta(state, opcode[1], opcode[2]);
      break;
    case 0x33:
      i8080_inx(state, (regpair_t){}, SP);
      break;
    case 0x34:
      i8080_inr(state, M);
      break;
    case 0x35:
      i8080_dcr(state, M);
      break;
    case 0x36:
      i8080_mvi(state, M, opcode[1]);
      break;
    case 0x37:
      i8080_stc(state);
      break;
    case 0x38:
      i8080_nop(state);
      break;
    case 0x39:
      i8080_dad(state, *SP);
      break;
    case 0x3a:
      i8080_lda(state, opcode[1], opcode[2]);
      break;
    case 0x3b:
      i8080_dcx(state, (regpair_t){}, SP);
      break;
    case 0x3c:
      i8080_inr(state, A);
      break;
    case 0x3d:
      i8080_dcr(state, A);
      break;
    case 0x3e:
      i8080_mvi(state, A, opcode[1]);
      break;
    case 0x3f:
      i8080_cmc(state);
      break;

    case 0x40:
      i8080_mov(state, B, B);
      break;
    case 0x41:
      i8080_mov(state, B, C);
      break;
    case 0x42:
      i8080_mov(state, B, D);
      break;
    case 0x43:
      i8080_mov(state, B, E);
      break;
    case 0x44:
      i8080_mov(state, B, H);
      break;
    case 0x45:
      i8080_mov(state, B, L);
      break;
    case 0x46:
      i8080_mov(state, B, M);
      break;
    case 0x47:
      i8080_mov(state, B, A);
      break;
    case 0x48:
      i8080_mov(state, C, B);
      break;
    case 0x49:
      i8080_mov(state, C, C);
      break;
    case 0x4a:
      i8080_mov(state, C, D);
      break;
    case 0x4b:
      i8080_mov(state, C, E);
      break;
    case 0x4c:
      i8080_mov(state, C, H);
      break;
    case 0x4d:
      i8080_mov(state, C, L);
      break;
    case 0x4e:
      i8080_mov(state, C, M);
      break;
    case 0x4f:
      i8080_mov(state, C, A);
      break;

    case 0x50:
      i8080_mov(state, D, B);
      break;
    case 0x51:
      i8080_mov(state, D, C);
      break;
    case 0x52:
      i8080_mov(state, D, D);
      break;
    case 0x53:
      i8080_mov(state, D, E);
      break;
    case 0x54:
      i8080_mov(state, D, H);
      break;
    case 0x55:
      i8080_mov(state, D, L);
      break;
    case 0x56:
      i8080_mov(state, D, M);
      break;
    case 0x57:
      i8080_mov(state, D, A);
      break;
    case 0x58:
      i8080_mov(state, E, B);
      break;
    case 0x59:
      i8080_mov(state, E, C);
      break;
    case 0x5a:
      i8080_mov(state, E, D);
      break;
    case 0x5b:
      i8080_mov(state, E, E);
      break;
    case 0x5c:
      i8080_mov(state, E, H);
      break;
    case 0x5d:
      i8080_mov(state, E, L);
      break;
    case 0x5e:
      i8080_mov(state, E, M);
      break;
    case 0x5f:
      i8080_mov(state, E, A);
      break;

    case 0x60:
      i8080_mov(state, H, B);
      break;
    case 0x61:
      i8080_mov(state, H, C);
      break;
    case 0x62:
      i8080_mov(state, H, D);
      break;
    case 0x63:
      i8080_mov(state, H, E);
      break;
    case 0x64:
      i8080_mov(state, H, H);
      break;
    case 0x65:
      i8080_mov(state, H, L);
      break;
    case 0x66:
      i8080_mov(state, H, M);
      break;
    case 0x67:
      i8080_mov(state, H, A);
      break;
    case 0x68:
      i8080_mov(state, L, B);
      break;
    case 0x69:
      i8080_mov(state, L, C);
      break;
    case 0x6a:
      i8080_mov(state, L, D);
      break;
    case 0x6b:
      i8080_mov(state, L, E);
      break;
    case 0x6c:
      i8080_mov(state, L, H);
      break;
    case 0x6d:
      i8080_mov(state, L, L);
      break;
    case 0x6e:
      i8080_mov(state, L, M);
      break;
    case 0x6f:
      i8080_mov(state, L, A);
      break;

    case 0x70:
      i8080_mov(state, M, B);
      break;
    case 0x71:
      i8080_mov(state, M, C);
      break;
    case 0x72:
      i8080_mov(state, M, D);
      break;
    case 0x73:
      i8080_mov(state, M, E);
      break;
    case 0x74:
      i8080_mov(state, M, H);
      break;
    case 0x75:
      i8080_mov(state, M, L);
      break;
    case 0x76:
      state->pc--;
      break;
    case 0x77:
      i8080_mov(state, M, A);
      break;
    case 0x78:
      i8080_mov(state, A, B);
      break;
    case 0x79:
      i8080_mov(state, A, C);
      break;
    case 0x7a:
      i8080_mov(state, A, D);
      break;
    case 0x7b:
      i8080_mov(state, A, E);
      break;
    case 0x7c:
      i8080_mov(state, A, H);
      break;
    case 0x7d:
      i8080_mov(state, A, L);
      break;
    case 0x7e:
      i8080_mov(state, A, M);
      break;
    case 0x7f:
      i8080_mov(state, A, A);
      break;

    case 0x80:
      i8080_add(state, B);
      break;
    case 0x81:
      i8080_add(state, C);
      break;
    case 0x82:
      i8080_add(state, D);
      break;
    case 0x83:
      i8080_add(state, E);
      break;
    case 0x84:
      i8080_add(state, H);
      break;
    case 0x85:
      i8080_add(state, L);
      break;
    case 0x86:
      i8080_add(state, M);
      break;
    case 0x87:
      i8080_add(state, A);
      break;
    case 0x88:
      i8080_adc(state, B);
      break;
    case 0x89:
      i8080_adc(state, C);
      break;
    case 0x8a:
      i8080_adc(state, D);
      break;
    case 0x8b:
      i8080_adc(state, E);
      break;
    case 0x8c:
      i8080_adc(state, H);
      break;
    case 0x8d:
      i8080_adc(state, L);
      break;
    case 0x8e:
      i8080_adc(state, M);
      break;
    case 0x8f:
      i8080_adc(state, A);
      break;

    case 0x90:
      i8080_sub(state, B);
      break;
    case 0x91:
      i8080_sub(state, C);
      break;
    case 0x92:
      i8080_sub(state, D);
      break;
    case 0x93:
      i8080_sub(state, E);
      break;
    case 0x94:
      i8080_sub(state, H);
      break;
    case 0x95:
      i8080_sub(state, L);
      break;
    case 0x96:
      i8080_sub(state, M);
      break;
    case 0x97:
      i8080_sub(state, A);
      break;
    case 0x98:
      i8080_sbb(state, B);
      break;
    case 0x99:
      i8080_sbb(state, C);
      break;
    case 0x9a:
      i8080_sbb(state, D);
      break;
    case 0x9b:
      i8080_sbb(state, E);
      break;
    case 0x9c:
      i8080_sbb(state, H);
      break;
    case 0x9d:
      i8080_sbb(state, L);
      break;
    case 0x9e:
      i8080_sbb(state, M);
      break;
    case 0x9f:
      i8080_sbb(state, A);
      break;

    case 0xa0:
      i8080_ana(state, B);
      break;
    case 0xa1:
      i8080_ana(state, C);
      break;
    case 0xa2:
      i8080_ana(state, D);
      break;
    case 0xa3:
      i8080_ana(state, E);
      break;
    case 0xa4:
      i8080_ana(state, H);
      break;
    case 0xa5:
      i8080_ana(state, L);
      break;
    case 0xa6:
      i8080_ana(state, M);
      break;
    case 0xa7:
      i8080_ana(state, A);
      break;
    case 0xa8:
      i8080_xra(state, B);
      break;
    case 0xa9:
      i8080_xra(state, C);
      break;
    case 0xaa:
      i8080_xra(state, D);
      break;
    case 0xab:
      i8080_xra(state, E);
      break;
    case 0xac:
      i8080_xra(state, H);
      break;
    case 0xad:
      i8080_xra(state, L);
      break;
    case 0xae:
      i8080_xra(state, M);
      break;
    case 0xaf:
      i8080_xra(state, A);
      break;

    case 0xb0:
      i8080_ora(state, B);
      break;
    case 0xb1:
      i8080_ora(state, C);
      break;
    case 0xb2:
      i8080_ora(state, D);
      break;
    case 0xb3:
      i8080_ora(state, E);
      break;
    case 0xb4:
      i8080_ora(state, H);
      break;
    case 0xb5:
      i8080_ora(state, L);
      break;
    case 0xb6:
      i8080_ora(state, M);
      break;
    case 0xb7:
      i8080_ora(state, A);
      break;
    case 0xb8:
      i8080_cmp(state, B);
      break;
    case 0xb9:
      i8080_cmp(state, C);
      break;
    case 0xba:
      i8080_cmp(state, D);
      break;
    case 0xbb:
      i8080_cmp(state, E);
      break;
    case 0xbc:
      i8080_cmp(state, H);
      break;
    case 0xbd:
      i8080_cmp(state, L);
      break;
    case 0xbe:
      i8080_cmp(state, M);
      break;
    case 0xbf:
      i8080_cmp(state, A);
      break;

    case 0xc0:
      i8080_rnz(state);
      break;
    case 0xc1:
      i8080_pop(state, BC);
      break;
    case 0xc2:
      i8080_jnz(state, opcode[1], opcode[2]);
      break;
    case 0xc3:
      i8080_jmp(state, opcode[1], opcode[2]);
      break;
    case 0xc4:
      i8080_cnz(state, opcode[1], opcode[2]);
      break;
    case 0xc5:
      i8080_push(state, BC);
      break;
    case 0xc6:
      i8080_adi(state, opcode[1]);
      break;
    case 0xc7:
      i8080_rst(state, 0);
      break;
    case 0xc8:
      i8080_rz(state);
      break;
    case 0xc9:
      i8080_ret(state);
      break;
    case 0xca:
      i8080_jz(state, opcode[1], opcode[2]);
      break;
    case 0xcb:
      i8080_jmp(state, opcode[1], opcode[2]);
      break;
    case 0xcc:
      i8080_cz(state, opcode[1], opcode[2]);
      break;
    case 0xcd:
      i8080_call(state, opcode[1], opcode[2]);
      break;
    case 0xce:
      i8080_aci(state, opcode[1]);
      break;
    case 0xcf:
      i8080_rst(state, 1);
      break;

    case 0xd0:
      i8080_rnc(state);
      break;
    case 0xd1:
      i8080_pop(state, DE);
      break;
    case 0xd2:
      i8080_jnc(state, opcode[1], opcode[2]);
      break;
    case 0xd3:
      break;  // OUT d8 ---unimplemented (ext. hardware)
    case 0xd4:
      i8080_cnc(state, opcode[1], opcode[2]);
      break;
    case 0xd5:
      i8080_push(state, DE);
      break;
    case 0xd6:
      i8080_sui(state, opcode[1]);
      break;
    case 0xd7:
      i8080_rst(state, 2);
      break;
    case 0xd8:
      i8080_rc(state);
      break;
    case 0xd9:
      i8080_ret(state);
      break;
    case 0xda:
      i8080_jc(state, opcode[1], opcode[2]);
      break;
    case 0xdb:
      break;  // IN d8 ---unimplemented(ext. hardware)
    case 0xdc:
      i8080_cc(state, opcode[1], opcode[2]);
      break;
    case 0xdd:
      i8080_call(state, opcode[1], opcode[2]);
      break;
    case 0xde:
      i8080_sbi(state, opcode[1]);
      break;
    case 0xdf:
      i8080_rst(state, 3);
      break;

    case 0xe0:
      i8080_rpo(state);
      break;
    case 0xe1:
      i8080_pop(state, HL);
      break;
    case 0xe2:
      i8080_jpo(state, opcode[1], opcode[2]);
      break;
    case 0xe3:
      i8080_xthl(state);
      break;
    case 0xe4:
      i8080_cpo(state, opcode[1], opcode[2]);
      break;
    case 0xe5:
      i8080_push(state, HL);
      break;
    case 0xe6:
      i8080_ani(state, opcode[1]);
      break;
    case 0xe7:
      i8080_rst(state, 4);
      break;
    case 0xe8:
      i8080_rpe(state);
      break;
    case 0xe9:
      i8080_pchl(state);
      break;
    case 0xea:
      i8080_jpe(state, opcode[1], opcode[2]);
      break;
    case 0xeb:
      i8080_xchg(state);
      break;
    case 0xec:
      i8080_cpe(state, opcode[1], opcode[2]);
      break;
    case 0xed:
      i8080_call(state, opcode[1], opcode[2]);
      break;
    case 0xee:
      i8080_xri(state, opcode[1]);
      break;
    case 0xef:
      i8080_rst(state, 5);
      break;

    case 0xf0:
      i8080_rp(state);
      break;
    case 0xf1:
      i8080_pop_psw(state);
      break;
    case 0xf2:
      i8080_jp(state, opcode[1], opcode[2]);
      break;
    case 0xf3:
      i8080_di(state);
      break;
    case 0xf4:
      i8080_cp(state, opcode[1], opcode[2]);
      break;
    case 0xf5:
      i8080_push_psw(state);
      break;
    case 0xf6:
      i8080_ori(state, opcode[1]);
      break;
    case 0xf7:
      i8080_rst(state, 6);
      break;
    case 0xf8:
      i8080_rm(state);
      break;
    case 0xf9:
      i8080_sphl(state);
      break;
    case 0xfa:
      i8080_jm(state, opcode[1], opcode[2]);
      break;
    case 0xfb:
      i8080_ei(state);
      break;
    case 0xfc:
      i8080_cm(state, opcode[1], opcode[2]);
      break;
    case 0xfd:
      i8080_call(state, opcode[1], opcode[2]);
      break;
    case 0xfe:
      i8080_cpi(state, opcode[1]);
      break;
    case 0xff:
      i8080_rst(state, 7);
      break;
  }
}

// returns bytes of operation at pc
uint8_t i8080_disassemble(const unsigned char* buffer, const uint16_t pc) {
  const unsigned char* opcode = &buffer[pc];

  uint8_t opbytes = 1;  // default byte length of instructions

  printf("%04x ", pc);

  switch (*opcode) {
    case 0x00:
      printf("NOP");
      break;
    case 0x01:
      printf("LXI\tB,#$%02x%02x", opcode[2], opcode[1]);
      opbytes = 3;
      break;
    case 0x02:
      printf("STAX\tB");
      break;
    case 0x03:
      printf("INX\tB");
      break;
    case 0x04:
      printf("INR\tB");
      break;
    case 0x05:
      printf("DCR\tB");
      break;
    case 0x06:
      printf("MVI\tB,#$%02x", opcode[1]);
      opbytes = 2;
      break;
    case 0x07:
      printf("RLC");
      break;
    case 0x08:
      printf("NOP");
      break;
    case 0x09:
      printf("DAD\tB");
      break;
    case 0x0a:
      printf("LDAX\tB");
      break;
    case 0x0b:
      printf("DCX\tB");
      break;
    case 0x0c:
      printf("INR\tC");
      break;
    case 0x0d:
      printf("DCR\tC");
      break;
    case 0x0e:
      printf("MVI\tC,#$%02x", opcode[1]);
      break;
    case 0x0f:
      printf("RRC");
      break;

    case 0x10:
      printf("NOP");
      break;
    case 0x11:
      printf("LXI\tD,#$%02x%02x", opcode[2], opcode[1]);
      opbytes = 3;
      break;
    case 0x12:
      printf("STAX\tD");
      break;
    case 0x13:
      printf("INX\tD");
      break;
    case 0x14:
      printf("INR\tD");
      break;
    case 0x15:
      printf("DCR\tD");
      break;
    case 0x16:
      printf("MVI\tD,#$%02x", opcode[1]);
      opbytes = 2;
      break;
    case 0x17:
      printf("RAL");
      break;
    case 0x18:
      printf("NOP");
      break;
    case 0x19:
      printf("DAD\tD");
      break;
    case 0x1a:
      printf("LDAX\tD");
      break;
    case 0x1b:
      printf("DCX\tD");
      break;
    case 0x1c:
      printf("INR\tE");
      break;
    case 0x1d:
      printf("DCR\tE");
      break;
    case 0x1e:
      printf("MVI\tE,#$%02x", opcode[1]);
      opbytes = 2;
      break;
    case 0x1f:
      printf("RAR");
      break;

    case 0x20:
      printf("NOP");
      break;
    case 0x21:
      printf("LXI\tH,#$%02x%02x", opcode[2], opcode[1]);
      opbytes = 3;
      break;
    case 0x22:
      printf("SHLD\t$%02x%02x", opcode[2], opcode[1]);
      opbytes = 3;
      break;
    case 0x23:
      printf("INX\tH");
      break;
    case 0x24:
      printf("INR\tH");
      break;
    case 0x25:
      printf("DCR\tH");
      break;
    case 0x26:
      printf("MVI\tH,#$%02x", opcode[1]);
      opbytes = 2;
      break;
    case 0x27:
      printf("DAA");
      break;
    case 0x28:
      printf("NOP");
      break;
    case 0x29:
      printf("DAD\tH");
      break;
    case 0x2a:
      printf("LHLD\t$%02x%02x", opcode[2], opcode[1]);
      opbytes = 3;
      break;
    case 0x2b:
      printf("DCX\tH");
      break;
    case 0x2c:
      printf("INR\tL");
      break;
    case 0x2d:
      printf("DCR\tL");
      break;
    case 0x2e:
      printf("MVI\tL,#$%02x", opcode[1]);
      opbytes = 2;
      break;
    case 0x2f:
      printf("CMA");
      break;

    case 0x30:
      printf("NOP");
      break;
    case 0x31:
      printf("LXI\tSP,#$%02x%02x", opcode[2], opcode[1]);
      opbytes = 3;
      break;
    case 0x32:
      printf("STA\t$%02x%02x", opcode[2], opcode[1]);
      opbytes = 3;
      break;
    case 0x33:
      printf("INX\tSP");
      break;
    case 0x34:
      printf("INR\tM");
      break;
    case 0x35:
      printf("DCR\tM");
      break;
    case 0x36:
      printf("MVI\tM,#$%02x", opcode[1]);
      opbytes = 2;
      break;
    case 0x37:
      printf("STC");
      break;
    case 0x38:
      printf("NOP");
      break;
    case 0x39:
      printf("DAD\tSP");
      break;
    case 0x3a:
      printf("LDA\t$%02x%02x", opcode[2], opcode[1]);
      opbytes = 3;
      break;
    case 0x3b:
      printf("DCX\tSP");
      break;
    case 0x3c:
      printf("INR\tA");
      break;
    case 0x3d:
      printf("DCR\tA");
      break;
    case 0x3e:
      printf("MVI\tA,#$%02x", opcode[1]);
      opbytes = 2;
      break;
    case 0x3f:
      printf("CMC");
      break;

    case 0x40:
      printf("MOV\tB,B");
      break;
    case 0x41:
      printf("MOV\tB,C");
      break;
    case 0x42:
      printf("MOV\tB,D");
      break;
    case 0x43:
      printf("MOV\tB,E");
      break;
    case 0x44:
      printf("MOV\tB,H");
      break;
    case 0x45:
      printf("MOV\tB,L");
      break;
    case 0x46:
      printf("MOV\tB,M");
      break;
    case 0x47:
      printf("MOV\tB,A");
      break;
    case 0x48:
      printf("MOV\tC,B");
      break;
    case 0x49:
      printf("MOV\tC,C");
      break;
    case 0x4a:
      printf("MOV\tC,D");
      break;
    case 0x4b:
      printf("MOV\tC,E");
      break;
    case 0x4c:
      printf("MOV\tC,H");
      break;
    case 0x4d:
      printf("MOV\tC,L");
      break;
    case 0x4e:
      printf("MOV\tC,M");
      break;
    case 0x4f:
      printf("MOV\tC,A");
      break;

    case 0x50:
      printf("MOV\tD,B");
      break;
    case 0x51:
      printf("MOV\tD,C");
      break;
    case 0x52:
      printf("MOV\tD,D");
      break;
    case 0x53:
      printf("MOV\tD.E");
      break;
    case 0x54:
      printf("MOV\tD,H");
      break;
    case 0x55:
      printf("MOV\tD,L");
      break;
    case 0x56:
      printf("MOV\tD,M");
      break;
    case 0x57:
      printf("MOV\tD,A");
      break;
    case 0x58:
      printf("MOV\tE,B");
      break;
    case 0x59:
      printf("MOV\tE,C");
      break;
    case 0x5a:
      printf("MOV\tE,D");
      break;
    case 0x5b:
      printf("MOV\tE,E");
      break;
    case 0x5c:
      printf("MOV\tE,H");
      break;
    case 0x5d:
      printf("MOV\tE,L");
      break;
    case 0x5e:
      printf("MOV\tE,M");
      break;
    case 0x5f:
      printf("MOV\tE,A");
      break;

    case 0x60:
      printf("MOV\tH,B");
      break;
    case 0x61:
      printf("MOV\tH,C");
      break;
    case 0x62:
      printf("MOV\tH,D");
      break;
    case 0x63:
      printf("MOV\tH.E");
      break;
    case 0x64:
      printf("MOV\tH,H");
      break;
    case 0x65:
      printf("MOV\tH,L");
      break;
    case 0x66:
      printf("MOV\tH,M");
      break;
    case 0x67:
      printf("MOV\tH,A");
      break;
    case 0x68:
      printf("MOV\tL,B");
      break;
    case 0x69:
      printf("MOV\tL,C");
      break;
    case 0x6a:
      printf("MOV\tL,D");
      break;
    case 0x6b:
      printf("MOV\tL,E");
      break;
    case 0x6c:
      printf("MOV\tL,H");
      break;
    case 0x6d:
      printf("MOV\tL,L");
      break;
    case 0x6e:
      printf("MOV\tL,M");
      break;
    case 0x6f:
      printf("MOV\tL,A");
      break;

    case 0x70:
      printf("MOV\tM,B");
      break;
    case 0x71:
      printf("MOV\tM,C");
      break;
    case 0x72:
      printf("MOV\tM,D");
      break;
    case 0x73:
      printf("MOV\tM.E");
      break;
    case 0x74:
      printf("MOV\tM,H");
      break;
    case 0x75:
      printf("MOV\tM,L");
      break;
    case 0x76:
      printf("HLT");
      break;
    case 0x77:
      printf("MOV\tM,A");
      break;
    case 0x78:
      printf("MOV\tA,B");
      break;
    case 0x79:
      printf("MOV\tA,C");
      break;
    case 0x7a:
      printf("MOV\tA,D");
      break;
    case 0x7b:
      printf("MOV\tA,E");
      break;
    case 0x7c:
      printf("MOV\tA,H");
      break;
    case 0x7d:
      printf("MOV\tA,L");
      break;
    case 0x7e:
      printf("MOV\tA,M");
      break;
    case 0x7f:
      printf("MOV\tA,A");
      break;

    case 0x80:
      printf("ADD\tB");
      break;
    case 0x81:
      printf("ADD\tC");
      break;
    case 0x82:
      printf("ADD\tD");
      break;
    case 0x83:
      printf("ADD\tE");
      break;
    case 0x84:
      printf("ADD\tH");
      break;
    case 0x85:
      printf("ADD\tL");
      break;
    case 0x86:
      printf("ADD\tM");
      break;
    case 0x87:
      printf("ADD\tA");
      break;
    case 0x88:
      printf("ADC\tB");
      break;
    case 0x89:
      printf("ADC\tC");
      break;
    case 0x8a:
      printf("ADC\tD");
      break;
    case 0x8b:
      printf("ADC\tE");
      break;
    case 0x8c:
      printf("ADC\tH");
      break;
    case 0x8d:
      printf("ADC\tL");
      break;
    case 0x8e:
      printf("ADC\tM");
      break;
    case 0x8f:
      printf("ADC\tA");
      break;

    case 0x90:
      printf("SUB\tB");
      break;
    case 0x91:
      printf("SUB\tC");
      break;
    case 0x92:
      printf("SUB\tD");
      break;
    case 0x93:
      printf("SUB\tE");
      break;
    case 0x94:
      printf("SUB\tH");
      break;
    case 0x95:
      printf("SUB\tL");
      break;
    case 0x96:
      printf("SUB\tM");
      break;
    case 0x97:
      printf("SUB\tA");
      break;
    case 0x98:
      printf("SBB\tB");
      break;
    case 0x99:
      printf("SBB\tC");
      break;
    case 0x9a:
      printf("SBB\tD");
      break;
    case 0x9b:
      printf("SBB\tE");
      break;
    case 0x9c:
      printf("SBB\tH");
      break;
    case 0x9d:
      printf("SBB\tL");
      break;
    case 0x9e:
      printf("SBB\tM");
      break;
    case 0x9f:
      printf("SBB\tA");
      break;

    case 0xa0:
      printf("ANA\tB");
      break;
    case 0xa1:
      printf("ANA\tC");
      break;
    case 0xa2:
      printf("ANA\tD");
      break;
    case 0xa3:
      printf("ANA\tE");
      break;
    case 0xa4:
      printf("ANA\tH");
      break;
    case 0xa5:
      printf("ANA\tL");
      break;
    case 0xa6:
      printf("ANA\tM");
      break;
    case 0xa7:
      printf("ANA\tA");
      break;
    case 0xa8:
      printf("XRA\tB");
      break;
    case 0xa9:
      printf("XRA\tC");
      break;
    case 0xaa:
      printf("XRA\tD");
      break;
    case 0xab:
      printf("XRA\tE");
      break;
    case 0xac:
      printf("XRA\tH");
      break;
    case 0xad:
      printf("XRA\tL");
      break;
    case 0xae:
      printf("XRA\tM");
      break;
    case 0xaf:
      printf("XRA\tA");
      break;

    case 0xb0:
      printf("ORA\tB");
      break;
    case 0xb1:
      printf("ORA\tC");
      break;
    case 0xb2:
      printf("ORA\tD");
      break;
    case 0xb3:
      printf("ORA\tE");
      break;
    case 0xb4:
      printf("ORA\tH");
      break;
    case 0xb5:
      printf("ORA\tL");
      break;
    case 0xb6:
      printf("ORA\tM");
      break;
    case 0xb7:
      printf("ORA\tA");
      break;
    case 0xb8:
      printf("CMP\tB");
      break;
    case 0xb9:
      printf("CMP\tC");
      break;
    case 0xba:
      printf("CMP\tD");
      break;
    case 0xbb:
      printf("CMP\tE");
      break;
    case 0xbc:
      printf("CMP\tH");
      break;
    case 0xbd:
      printf("CMP\tL");
      break;
    case 0xbe:
      printf("CMP\tM");
      break;
    case 0xbf:
      printf("CMP\tA");
      break;

    case 0xc0:
      printf("RNZ");
      break;
    case 0xc1:
      printf("POP\tB");
      break;
    case 0xc2:
      printf("JNZ\t$%02x%02x", opcode[2], opcode[1]);
      opbytes = 3;
      break;
    case 0xc3:
      printf("JMP\t$%02x%02x", opcode[2], opcode[1]);
      opbytes = 3;
      break;
    case 0xc4:
      printf("CNZ\t$%02x%02x", opcode[2], opcode[1]);
      opbytes = 3;
      break;
    case 0xc5:
      printf("PUSH\tB");
      break;
    case 0xc6:
      printf("ADI\t#$%02x", opcode[1]);
      opbytes = 2;
      break;
    case 0xc7:
      printf("RST\t0");
      break;
    case 0xc8:
      printf("RZ");
      break;
    case 0xc9:
      printf("RET");
      break;
    case 0xca:
      printf("JZ\t$%02x%02x", opcode[2], opcode[1]);
      opbytes = 3;
      break;
    case 0xcb:
      printf("JMP\t$%02x%02x", opcode[2], opcode[1]);
      opbytes = 3;
      break;
    case 0xcc:
      printf("CZ\t$%02x%02x", opcode[2], opcode[1]);
      opbytes = 3;
      break;
    case 0xcd:
      printf("CALL\t$%02x%02x", opcode[2], opcode[1]);
      opbytes = 3;
      break;
    case 0xce:
      printf("ACI\t#$%02x", opcode[1]);
      opbytes = 2;
      break;
    case 0xcf:
      printf("RST\t1");
      break;

    case 0xd0:
      printf("RNC");
      break;
    case 0xd1:
      printf("POP\tD");
      break;
    case 0xd2:
      printf("JNC\t$%02x%02x", opcode[2], opcode[1]);
      opbytes = 3;
      break;
    case 0xd3:
      printf("OUT\t#$%02x", opcode[1]);
      opbytes = 2;
      break;
    case 0xd4:
      printf("CNC\t$%02x%02x", opcode[2], opcode[1]);
      opbytes = 3;
      break;
    case 0xd5:
      printf("PUSH\tD");
      break;
    case 0xd6:
      printf("SUI\t#$%02x", opcode[1]);
      opbytes = 2;
      break;
    case 0xd7:
      printf("RST\t2");
      break;
    case 0xd8:
      printf("RC");
      break;
    case 0xd9:
      printf("RET");
      break;
    case 0xda:
      printf("JC\t$%02x%02x", opcode[2], opcode[1]);
      opbytes = 3;
      break;
    case 0xdb:
      printf("IN\t#$%02x", opcode[1]);
      opbytes = 2;
      break;
    case 0xdc:
      printf("CC\t$%02x%02x", opcode[2], opcode[1]);
      opbytes = 3;
      break;
    case 0xdd:
      printf("CALL\t$%02x%02x", opcode[2], opcode[1]);
      opbytes = 3;
      break;
    case 0xde:
      printf("SBI\t#$%02x", opcode[1]);
      opbytes = 2;
      break;
    case 0xdf:
      printf("RST\t3");
      break;

    case 0xe0:
      printf("RPO");
      break;
    case 0xe1:
      printf("POP\tH");
      break;
    case 0xe2:
      printf("JPO\t$%02x%02x", opcode[2], opcode[1]);
      opbytes = 3;
      break;
    case 0xe3:
      printf("XTHL");
      break;
    case 0xe4:
      printf("CPO\t$%02x%02x", opcode[2], opcode[1]);
      opbytes = 3;
      break;
    case 0xe5:
      printf("PUSH\tH");
      break;
    case 0xe6:
      printf("ANI\t#$%02x", opcode[1]);
      opbytes = 2;
      break;
    case 0xe7:
      printf("RST\t4");
      break;
    case 0xe8:
      printf("RPE");
      break;
    case 0xe9:
      printf("PCHL");
      break;
    case 0xea:
      printf("JPE\t$%02x%02x", opcode[2], opcode[1]);
      opbytes = 3;
      break;
    case 0xeb:
      printf("XCHG");
      break;
    case 0xec:
      printf("CPE\t$%02x%02x", opcode[2], opcode[1]);
      opbytes = 3;
      break;
    case 0xed:
      printf("CALL\t$%02x%02x", opcode[2], opcode[1]);
      opbytes = 3;
      break;
    case 0xee:
      printf("XRI\t#$%02x", opcode[1]);
      opbytes = 2;
      break;
    case 0xef:
      printf("RST\t5");
      break;

    case 0xf0:
      printf("RP");
      break;
    case 0xf1:
      printf("POP\t");
      break;
    case 0xf2:
      printf("JP\t$%02x%02x", opcode[2], opcode[1]);
      opbytes = 3;
      break;
    case 0xf3:
      printf("DI");
      break;
    case 0xf4:
      printf("CP\t$%02x%02x", opcode[2], opcode[1]);
      opbytes = 3;
      break;
    case 0xf5:
      printf("PUSH\t");
      break;
    case 0xf6:
      printf("ORI\t#$%02x", opcode[1]);
      opbytes = 2;
      break;
    case 0xf7:
      printf("RST\t6");
      break;
    case 0xf8:
      printf("RM");
      break;
    case 0xf9:
      printf("SPHL");
      break;
    case 0xfa:
      printf("JM\t$%02x%02x", opcode[2], opcode[1]);
      opbytes = 3;
      break;
    case 0xfb:
      printf("EI");
      break;
    case 0xfc:
      printf("CM\t$%02x%02x", opcode[2], opcode[1]);
      opbytes = 3;
      break;
    case 0xfd:
      printf("CALL\t$%02x%02x", opcode[2], opcode[1]);
      opbytes = 3;
      break;
    case 0xfe:
      printf("CPI\t#$%02x", opcode[1]);
      opbytes = 2;
      break;
    case 0xff:
      printf("RST\t7");
      break;
  }

  printf("\n");

  return opbytes;
}

void i8080_print(i8080_t* state) {
  printf("a\tbc\tde\thl\tpc\tsp\tz s p c (ac)\tcycles\n");
  printf("%02x\t%02x%02x\t%02x%02x\t%02x%02x\t%04x\t%04x\t%i %i %i %i %i\t%i\n",
         state->a, state->b, state->c,  // registers
         state->d, state->e, state->h, state->l, state->pc, state->sp,
         state->cb.flags.z, state->cb.flags.s,  // flags
         state->cb.flags.p, state->cb.flags.c, state->cb.flags.ac,
         state->cycles  // cycles
  );
  printf("\n");
}
