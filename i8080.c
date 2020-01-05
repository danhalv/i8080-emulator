#include "i8080.h"

conditionbits_t init_conditionbits() {
	conditionbits_t cb = {.pad3=1};
	return cb;
}

static uint8_t read_byte(const uint16_t address, uint8_t *memory) {
	return memory[address];
}

static void write_byte(const uint16_t address, const uint8_t byte, uint8_t *memory) {
	memory[address] = byte;
}

static uint8_t should_set_parity_bit(const uint16_t val, const size_t bytes) {

	int count = 0;	//holds count of 1's	

	for(int bit = 0; bit < (bytes * 8); bit++) {
		if((val >> bit) & 1)		
			count++;
	}	

	return (count % 2 == 0);
}

static void set_flags_zsp(i8080_t *state, uint8_t byte) {

	state->cb.z = (0 == byte);
	
	state->cb.s = (0x80 & byte) != 0;

	state->cb.p = should_set_parity_bit(byte, sizeof(byte));
}

static uint8_t add_and_set_flags(i8080_t *state, const uint8_t augend, const uint8_t addend) {

	const uint16_t result = augend + addend;

	set_flags_zsp(state, result);

	//carry set if overflow past bit 7
	state->cb.c = (0xff00 & result) != 0;

	//aux carry set if overflow past low-order bits (0-3)
	uint8_t augend_lbits = (0x0f & augend);
	uint8_t addend_lbits = (0x0f & addend);
	state->cb.ac = ((augend_lbits + addend_lbits) & 0xf0) != 0;

	return result;
};

static int8_t add_twos_comp_and_set_flags(i8080_t *state, const int8_t augend, const int8_t addend) {	

	const int8_t result = augend + addend;

	set_flags_zsp(state, result);
	
	bool sign_augend = (0x80 & augend);
	bool sign_addend = (0x80 & addend);
	bool sign_result = (0x80 & result);

	//carry bit set if overflow
	//overflow in addition two's complement requires two conditions:
	bool cond1 = sign_augend == sign_addend;	//terms have same sign AND
	bool cond2 = sign_result != sign_addend;	//result has opposite sign
	state->cb.c = cond1 && cond2;

	//aux carry set if overflow past low-order bits(0-3)
	uint8_t augend_lbits = (0x0f & augend);
	uint8_t addend_lbits = (0x0f & addend);
	state->cb.ac = ((augend_lbits + addend_lbits) & 0xf0) != 0;

	return result;
};

static int8_t sub_twos_comp_and_set_flags(i8080_t *state, const int8_t minuend, const int8_t subtrahend) {

	//x-y => x+(-y)
	const int8_t result = add_twos_comp_and_set_flags(state, minuend, -subtrahend);

	state->cb.c = ~state->cb.c;	//opposite of add

	return result;
};

void i8080_inr(i8080_t *state, uint8_t *reg) {

	*reg++;

	state->cb.z = (0 == *reg);
	state->cb.s = (0x80 & *reg) != 0;
	state->cb.ac = ((0x0f & *reg) == 0);	
	state->cb.p = should_set_parity_bit(*reg, sizeof(*reg));

	state->pc++;
}

void i8080_dcr(i8080_t *state, uint8_t *reg) {
	
	*reg--;	

	state->cb.z = (0 == *reg);
	state->cb.s = (0x80 & *reg) != 0;
	state->cb.ac = !((0x0f & *reg) == 0x0f);
	state->cb.p = should_set_parity_bit(*reg, sizeof(*reg));

	state->pc++;
}

void i8080_cma(i8080_t *state) {

	state->a = ~state->a;

	state->pc++;
}

void i8080_daa(i8080_t *state) {

	uint8_t least_significant_4 = (0x0f & state->a);
	uint8_t value_to_add = 0;

	//** step 1 **
	if(least_significant_4 > 9 || state->cb.ac == 1) 
		value_to_add = 0x06;

	//aux carry set after step 1, if carry out of least-significant 4 bits (0-3)
	uint8_t lbits_a = (0x0f & state->a);				//lower order bits of A register
	uint8_t lbits_vta = (0x0f & value_to_add);			//lower order bits of value_to_add
	state->cb.ac = ((lbits_a + lbits_vta) & 0xf0) != 0;

	//if value_to_add is set, increments register A by 0x06
	state->a += value_to_add;

	uint8_t most_significant_4 = (0xf0 & state->a);

	//** step 2 **
	if(most_significant_4 > 9 || state->c == 1)		
		value_to_add = 0x60;
	else
		value_to_add = 0;

	//carry set after step 2, if overflow past bit 7
	uint16_t result = state->a + value_to_add;
	state->cb.c = (0xff00 & result) != 0;

	//if value_to_add is set, increments register A by 0x60
	state->a += value_to_add;

	state->cb.z = (0 == state->a);	
	state->cb.s = (0x80 & state->a) != 0;
	state->cb.p = should_set_parity_bit(state->a, sizeof(state->a));

	state->pc++;
}

void i8080_nop(i8080_t *state) {
	state->pc++;
}

void i8080_mov(i8080_t *state, uint8_t *dst, const uint8_t *src) {

	*dst = *src;	

	state->pc++;
}


void i8080_stax(i8080_t *state, const regpair_t pair) {

	const uint16_t address = (*pair.first << 8) | *pair.second;

	write_byte(address, state->a, state->external_memory);

	state->pc++;
}

void i8080_ldax(i8080_t *state, const regpair_t pair) {

	const uint16_t address = (*pair.first << 8) | *pair.second;

	state->a = read_byte(address, state->external_memory);

	state->pc++;
}

void i8080_add(i8080_t *state, const uint8_t *reg) {

	state->a = add_twos_comp_and_set_flags(state, state->a, *reg);

	state->pc++;
}

void i8080_adc(i8080_t *state, const uint8_t *reg) {

	const uint8_t addend = *reg + (state->cb.c != 0);
	
	state->a = add_and_set_flags(state, state->a, addend);

	state->pc++;
}

void i8080_sub(i8080_t *state, const uint8_t *reg) {

	state->a = sub_twos_comp_and_set_flags(state, state->a, *reg);

	state->pc++;
}

void i8080_sbb(i8080_t *state, const uint8_t *reg) {

	const uint8_t subtrahend = *reg + (state->cb.c != 0);

	state->a = sub_twos_comp_and_set_flags(state, state->a, subtrahend);

	state->pc++;
}

void i8080_ana(i8080_t *state, const uint8_t *reg) {

	state->a = state->a & *reg;

	state->cb.c = 0;	//reset
	set_flags_zsp(state, state->a);

	state->pc++;
}


void i8080_xra(i8080_t *state, const uint8_t *reg) {

	state->a = state->a ^ *reg;
	
	state->cb.c = 0;	//reset
	state->cb.ac = 0;	//reset
	set_flags_zsp(state, state->a);

	state->pc++;
}

void i8080_ora(i8080_t *state, const uint8_t *reg) {

	state->a = state->a | *reg;

	state->cb.c = 0;	//reset
	set_flags_zsp(state, state->a);

	state->pc++;
}

void i8080_cmp(i8080_t *state, const uint8_t *reg) {

	sub_twos_comp_and_set_flags(state, state->a, *reg);

	state->pc++;	
}

void i8080_rlc(i8080_t *state) {

	bool hbit = (0x80 & state->a) != 0;			

	state->cb.c = hbit;							

	state->a = (state->a << 1) | hbit;			
	
	state->pc++;
}

void i8080_rrc(i8080_t *state) {

	bool lbit = (0x01 & state->a) != 0;			

	state->cb.c = lbit;							

	state->a = (lbit << 7) | (state->a >> 1);	

	state->pc++;
}

void i8080_ral(i8080_t *state) {

	bool hbit = (0x80 & state->a) != 0;			

	state->a = (state->a << 1) | state->cb.c;

	state->cb.c = hbit;							

	state->pc++;
}

void i8080_rar(i8080_t *state) {

	bool lbit = (0x01 & state->a) != 0;

	state->a = (state->cb.c << 7) | (state->a >> 1);		

	state->cb.c = lbit;

	state->pc++;
}

void i8080_push(i8080_t *state, const regpair_t pair) {

	write_byte(state->sp-1, *pair.first, state->external_memory);
	write_byte(state->sp-2, *pair.second, state->external_memory);

	state->sp -= 2;

	state->pc++;
}	

void i8080_pop(i8080_t *state, const regpair_t pair) {

	*pair.second = read_byte(state->sp, state->external_memory);
	*pair.first = read_byte(state->sp+1, state->external_memory);

	state->sp += 2;

	state->pc++;
}

void i8080_dad(i8080_t *state, const regpair_t pair, uint16_t *sp) {

	int16_t addend;
	if(sp)
		addend = (int16_t)*sp;
	else
		addend = (*pair.first << 8) | *pair.second;

	int16_t hl = (state->h << 8) | state->l; 
	int16_t result = hl + addend;

	bool sign_augend = (0x8000 & hl);
	bool sign_addend = (0x8000 & addend);
	bool sign_result = (0x8000 & result);

	//overflow of addition in two's complement requires two conditions:
	bool cond1 = sign_augend == sign_addend;	//terms have same sign AND
	bool cond2 = sign_result != sign_augend;	//result has opposite sign from terms
	state->cb.c = cond1 && cond2;

	state->h = (result >> 8);	
	state->l = result;

	state->pc++;
}	

void i8080_inx(i8080_t *state, regpair_t pair, uint16_t *sp) {

	if(sp)
		state->sp++;	
	else {
		*pair.second++;
		if(0 == *pair.second)
			*pair.first++;
	}

	state->pc++;
}

void i8080_dcx(i8080_t *state, regpair_t pair, uint16_t *sp) {

	if(sp)
		state->sp--;
	else {
		uint16_t combined = (*pair.first << 8) | *pair.second;
		combined -= 1;
		*pair.first = (combined >> 8);
		*pair.second = combined;	
	}

	state->pc++;
}	

void i8080_xchg(i8080_t *state) {

	uint8_t temp1 = state->h;
	uint8_t temp2 = state->l;	

	state->h = state->d;
	state->l = state->e;

	state->d = temp1;
	state->e = temp2;

	state->pc++;
}

void i8080_xthl(i8080_t *state) {
	
	uint8_t temp1 = state->l;
	uint8_t temp2 = state->h;

	state->l = read_byte(state->sp, state->external_memory);
	state->h = read_byte(state->sp+1, state->external_memory);

	write_byte(state->sp, temp1, state->external_memory);
	write_byte(state->sp+1, temp2, state->external_memory);
	
	state->pc++;
}

void i8080_sphl(i8080_t *state) {

	state->sp = (state->h << 8) | state->l;

	state->pc++;
}

void i8080_lxi(i8080_t *state, regpair_t pair, uint16_t *sp, uint8_t low, uint8_t high) {
	
	if(sp)
		state->sp = (high << 8) | low;
	else {
		*pair.first = high;
		*pair.second = low;
	}

	state->pc += 3;	
}	

void i8080_mvi(i8080_t *state, uint8_t *reg, uint8_t byte) {

	*reg = byte;

	state->pc += 2;
}	

void i8080_adi(i8080_t *state, uint8_t byte) {

	state->a = add_twos_comp_and_set_flags(state, state->a, byte);

	state->pc += 2;
}

void i8080_aci(i8080_t *state, uint8_t byte) {
	
	const uint8_t addend = byte + (state->cb.c != 0);

	state->a = add_twos_comp_and_set_flags(state, state->a, addend);

	state->pc += 2;
}

void i8080_sui(i8080_t *state, uint8_t byte) {
	
	state->a = sub_twos_comp_and_set_flags(state, state->a, byte);

	state->pc += 2;
}

void i8080_sbi(i8080_t *state, uint8_t byte) {
	
	uint8_t subtrahend = byte + (state->cb.c != 0);

	state->pc += 2;
}	

void i8080_ani(i8080_t *state, uint8_t byte) {

	state->a = state->a & byte;

	state->cb.c = 0;	//reset
	set_flags_zsp(state, state->a);

	state->pc += 2;
}

void i8080_xri(i8080_t *state, uint8_t byte) {

	state->a = state->a ^ byte;	

	state->cb.c = 0;	//reset
	set_flags_zsp(state, state->a);

	state->pc += 2;
}

void i8080_ori(i8080_t *state, uint8_t byte) {
	
	state->a = state->a | byte;

	state->cb.c = 0;	//reset
	set_flags_zsp(state, state->a);

	state->pc += 2;
}

void i8080_cpi(i8080_t *state, uint8_t byte) {
	
	sub_twos_comp_and_set_flags(state, state->a, byte);

	state->pc += 2;	
}

void i8080_sta(i8080_t *state, uint8_t low, uint8_t high) {
	
	const uint16_t address = (high << 8) | low;
	
	write_byte(address, state->a, state->external_memory);

	state->pc += 3;
}

void i8080_lda(i8080_t *state, uint8_t low, uint8_t high) {
	
	const uint16_t address = (high << 8) | low;

	state->a = read_byte(address, state->external_memory);

	state->pc += 3;
}	

void i8080_shld(i8080_t *state, uint8_t low, uint8_t high) {
	
	const uint16_t address = (high << 8) | low;

	write_byte(address, state->l, state->external_memory);
	write_byte(address+1, state->h, state->external_memory);

	state->pc += 3;
}

void i8080_lhld(i8080_t *state, uint8_t low, uint8_t high) {
	
	const uint16_t address = (high << 8) | low;

	state->l = read_byte(address, state->external_memory);		
	state->h = read_byte(address+1, state->external_memory);

	state->pc += 3;
}

void i8080_pchl(i8080_t *state) {
	state->pc = (state->h << 8) | state->l;
}

void i8080_jmp(i8080_t *state, uint8_t low, uint8_t high) {
	state->pc = (high << 8) | low;
}

static void i8080_cond_jmp(i8080_t *state, uint8_t low, uint8_t high, bool cond) {

	if(cond)	
		i8080_jmp(state, low, high);	
	else
		state->pc += 3;
}

void i8080_jc(i8080_t *state, uint8_t low, uint8_t high) {
	i8080_cond_jmp(state, low, high, state->cb.c);
}

void i8080_jnc(i8080_t *state, uint8_t low, uint8_t high) {
	i8080_cond_jmp(state, low, high, !state->cb.c);
}

void i8080_jz(i8080_t *state, uint8_t low, uint8_t high) {
	i8080_cond_jmp(state, low, high, state->cb.z);
}

void i8080_jnz(i8080_t *state, uint8_t low, uint8_t high) {
	i8080_cond_jmp(state, low, high, !state->cb.z);
}

void i8080_jm(i8080_t *state, uint8_t low, uint8_t high) {
	i8080_cond_jmp(state, low, high, state->cb.s);
}

void i8080_jp(i8080_t *state, uint8_t low, uint8_t high) {
	i8080_cond_jmp(state, low, high, !state->cb.s);
}

void i8080_jpe(i8080_t *state, uint8_t low, uint8_t high) {
	i8080_cond_jmp(state, low, high, state->cb.p);
}

void i8080_jpo(i8080_t *state, uint8_t low, uint8_t high) {
	i8080_cond_jmp(state, low, high, !state->cb.p);
}

void i8080_call(i8080_t *state, uint8_t low, uint8_t high) {

	//return address pushed to stack
	write_byte(state->sp-1, state->pc >> 8, state->external_memory);
	write_byte(state->sp-2, state->pc, state->external_memory);

	state->sp -= 2;	//new sp pos.

	state->pc = (high << 8) | low;	
}

static void i8080_cond_call(i8080_t *state, uint8_t low, uint8_t high, bool cond) {
	
	if(cond)
		i8080_call(state, low, high);
	else
		state->pc += 3;
}

void i8080_cc(i8080_t *state, uint8_t low, uint8_t high) {
	i8080_cond_call(state, low, high, state->cb.c);
}

void i8080_cnc(i8080_t *state, uint8_t low, uint8_t high) {
	i8080_cond_call(state, low, high, !state->cb.c);
}

void i8080_cz(i8080_t *state, uint8_t low, uint8_t high) {
	i8080_cond_call(state, low, high, state->cb.z);
}

void i8080_cnz(i8080_t *state, uint8_t low, uint8_t high) {
	i8080_cond_call(state, low, high, !state->cb.z);
}

void i8080_cm(i8080_t *state, uint8_t low, uint8_t high) {
	i8080_cond_call(state, low, high, state->cb.s);
}

void i8080_cp(i8080_t *state, uint8_t low, uint8_t high) {
	i8080_cond_call(state, low, high, !state->cb.s);
}

void i8080_cpe(i8080_t *state, uint8_t low, uint8_t high) {
	i8080_cond_call(state, low, high, state->cb.p);
}

void i8080_cpo(i8080_t *state, uint8_t low, uint8_t high) {
	i8080_cond_call(state, low, high, !state->cb.p);
}

void i8080_ret(i8080_t *state) {
	
	//retrieve address from stack
	uint8_t low = read_byte(state->sp, state->external_memory);
	uint8_t high = read_byte(state->sp+1, state->external_memory);
	state->pc = (high << 8) | low;

	state->sp += 2;	//pop address off stack
}

void i8080_cond_ret(i8080_t *state, bool cond) {

	if(cond)	
		i8080_ret(state);
	else
		state->pc++;
}

void i8080_rc(i8080_t *state) {
	i8080_cond_ret(state, state->cb.c);
}

void i8080_rnc(i8080_t *state) {
	i8080_cond_ret(state, !state->cb.c);
}

void i8080_rz(i8080_t *state) {
	i8080_cond_ret(state, state->cb.z);
}

void i8080_rnz(i8080_t *state) {
	i8080_cond_ret(state, !state->cb.z);
}

void i8080_rm(i8080_t *state) {
	i8080_cond_ret(state, state->cb.s);
}

void i8080_rp(i8080_t *state) {
	i8080_cond_ret(state, !state->cb.s);
}

void i8080_rpe(i8080_t *state) {
	i8080_cond_ret(state, state->cb.p);
}

void i8080_rpo(i8080_t *state) {
	i8080_cond_ret(state, !state->cb.p);
}

void i8080_rst(i8080_t *state, uint8_t rst_num) {
	
	switch(rst_num) {
		case 0: i8080_call(state, 0x00, 0x00); break;	//RST 0 (0x00)
		case 1: i8080_call(state, 0x00, 0x00); break;	//RST 1 (0x08)
		case 2:	i8080_call(state, 0x10, 0x00); break;	//RST 2 (0x10)
		case 3:	i8080_call(state, 0x18, 0x00); break;	//RST 3 (0x18)
		case 4:	i8080_call(state, 0x20, 0x00); break;	//RST 4 (0x20)
		case 5:	i8080_call(state, 0x28, 0x00); break;	//RST 5 (0x28)
		case 6:	i8080_call(state, 0x30, 0x00); break;	//RST 6 (0x30)
		case 7:	i8080_call(state, 0x38, 0x00); break;	//RST 7 (0x38)
	}
}

void i8080_ei(i8080_t *state) {

	state->ie = 1;

	state->pc++;
}

void i8080_di(i8080_t *state) {

	state->ie = 0;

	state->pc++;	
}

void i8080_step(i8080_t *state) {

	uint8_t *opcode = &state->external_memory[state->pc];

	uint8_t *A = &state->a;
	uint8_t *B = &state->b;
	uint8_t *C = &state->c;
	uint8_t *D = &state->d;
	uint8_t *E = &state->e;
	uint8_t *H = &state->h;
	uint8_t *L = &state->l;
	uint16_t *SP = &state->sp;
	regpair_t BC = {&state->b, &state->c};
	regpair_t DE = {&state->d, &state->e};
	regpair_t HL = {&state->h, &state->l};

	switch(*opcode) {
		case 0x00: 	i8080_nop(state); 									break;	//NOP
		case 0x01: 	i8080_lxi(state, BC, NULL, opcode[1], opcode[2]); 	break;	//LXI B, d16
		case 0x02: 	i8080_stax(state, BC); 								break;	//STAX B
		case 0x03: 	i8080_inx(state, BC, NULL);							break; 	//INX B
		case 0x04: 	i8080_inr(state, B);								break;	//INR B
		case 0x05: 	i8080_dcr(state, B);								break;	//DCR B
		case 0x06: 	i8080_mvi(state, B, opcode[1]);						break;	//MVI B, d8
		case 0x07: 	i8080_rlc(state);									break;	//RLC
		case 0x08: 	i8080_nop(state);									break;	//NOP
		case 0x09: 	i8080_dad(state, BC, NULL);							break;	//DAD B
		case 0x0a: 	i8080_ldax(state, BC);								break;	//LDAX B
		case 0x0b: 	i8080_dcx(state, BC, NULL);							break;	//DCX B
		case 0x0c: 	i8080_inr(state, C);								break;	//INR C
		case 0x0d: 	i8080_dcr(state, C);	 							break;	//DCR C
		case 0x0e: 	i8080_mvi(state, C, opcode[1]);						break;	//MVI C, d8
		case 0x0f:	i8080_rrc(state);									break;	//RRC

		case 0x10:	i8080_nop(state);									break;	//NOP		
		case 0x11:	i8080_lxi(state, DE, NULL, opcode[1], opcode[2]);	break;	//LXI D, d16
		case 0x12:	i8080_stax(state, DE);								break;	//STAX D
		case 0x13:	i8080_inx(state, DE, NULL);							break;	//INX D
		case 0x14:	i8080_inr(state, D);								break;	//INR D
		case 0x15:	i8080_dcr(state, D);								break;	//DCR D
		case 0x16:	i8080_mvi(state, D, opcode[1]);						break;	//MVI D, d8
		case 0x17:	i8080_ral(state);									break;	//RAL
		case 0x18:	i8080_nop(state);									break;	//NOP
		case 0x19:	i8080_dad(state, DE, NULL);							break;	//DAD D
		case 0x1a:	i8080_ldax(state, DE);								break;	//LDAX D
		case 0x1b:	i8080_dcx(state, DE, NULL);							break;	//DCX D
		case 0x1c:	i8080_inr(state, E);								break;	//INR E
		case 0x1d:	i8080_dcr(state, E);								break;	//DCR E
		case 0x1e:	i8080_mvi(state, E, opcode[1]);						break;	//MVI E
		case 0x1f:	i8080_rar(state);									break;	//RAR

		case 0x20:	i8080_nop(state);									break;	//NOP
		case 0x21:	i8080_lxi(state, HL, NULL, opcode[1], opcode[2]);	break;	//LXI H, d16
		case 0x22:	i8080_shld(state, opcode[1], opcode[2]);			break;	//SHLD d16
		case 0x23:	i8080_inx(state, HL, NULL);							break;	//INX H
		case 0x24:	i8080_inr(state, H);								break;	//INR H
		case 0x25:	i8080_dcr(state, H);								break;	//DCR H
		case 0x26:	i8080_mvi(state, H, opcode[1]);						break;	//MVI H
		case 0x27:	i8080_daa(state);									break;	//DAA
		case 0x28:	i8080_nop(state);									break;	//NOP
		case 0x29:	i8080_dad(state, HL, NULL);							break;	//DAD H
		case 0x2a:	i8080_lhld(state, opcode[1], opcode[2]);			break;	//LHLD d16
		case 0x2b:	i8080_dcx(state, HL, NULL);							break;	//DCX H
		case 0x2c:	i8080_inr(state, L);								break;	//INR L
		case 0x2d:	i8080_dcr(state, L);								break;	//DCR L
		case 0x2e:	i8080_mvi(state, L, opcode[1]);						break;	//MVI L
		case 0x2f:	i8080_cma(state);									break;	//CMA
	}
}

//prints instruction and operand
static void printi(const char* instr, const char* operand) {
	printf("%s\t%s\n", instr, operand);
}

//returns opbytes, add to pc to get new pc after instruction
//uint8_t i8080_disassemble(i8080_t *state) {
uint8_t i8080_disassemble(unsigned char* buffer, uint16_t pc) {

	//unsigned char *opcode = &state->external_memory[state->pc];
	unsigned char *opcode = &buffer[pc];

	uint8_t opbytes = 1;		//byte size of most instructions, else changed in switch statement

	//printf("%04x ", state->pc);
	printf("%04x ", pc);
	
	switch(*opcode) {
		case 0x00:	printi("NOP", ""); 	 													break;	//NOP
		case 0x01: 	printf("LXI\tB, %02x%02x\n", opcode[1], opcode[2]);		opbytes = 3;	break;	//LXI B, d16
		case 0x02: 	printi("STAX", "B"); 													break;	//STAX B
		case 0x03: 	printi("INX", "B"); 								 					break;	//INX B
		case 0x04: 	printi("INR", "B");														break;	//INR B
		case 0x05: 	printi("DCR", "B");														break;	//DCR B
		case 0x06: 	printf("MVI\tB, %02x\n", opcode[1]);					opbytes = 2;	break;	//MVI B, d8
		case 0x07:	printi("RLC", "");														break;	//RLC
		case 0x08:	printi("NOP", "");														break;	//NOP	
		case 0x09:	printi("DAD", "B");														break;	//DAD B
		case 0x0a:	printi("LDAX", "B");													break;	//LDAX B
		case 0x0b:	printi("DCX", "B");														break;	//DCX B
		case 0x0c:	printi("INR", "C");														break;	//INR C
		case 0x0d:	printi("DCR", "C");														break;	//DCR C
		case 0x0e:	printf("MVI\tC, %02x\n", opcode[1]);					opbytes = 2;	break;	//MVI C, d8
		case 0x0f:	printi("RRC", "");														break;	//RRC

		case 0x10:	printi("NOP", "");														break;	//NOP
		case 0x11:	printf("LXI\tD, %02x%02x\n", opcode[1], opcode[2]);		opbytes = 3;	break;	//LXI D, 16
		case 0x12:	printi("STAX", "D");													break;	//STAX D
		case 0x13:	printi("INX", "D");														break;	//INX D
		case 0x14:	printi("INR", "D");														break;	//INR D
		case 0x15:	printi("DCR", "D");														break;	//DCR D
		case 0x16:	printf("MVI\tD, %02x\n", opcode[1]);					opbytes = 2;	break;	//MVI D, d8
		case 0x17:	printi("RAL", "");														break;	//RAL
		case 0x18:	printi("NOP", "");														break;	//NOP
		case 0x19:	printi("DAD", "D");														break;	//DAD D
		case 0x1a:	printi("LDAX", "D");													break;	//LDAX D
		case 0x1b:	printi("DCX", "D");														break; 	//DCX D
		case 0x1c:	printi("INR", "E");														break;	//INR E
		case 0x1d:	printi("DCR", "E");														break;	//DCR E
		case 0x1e:	printf("MVI\tE, %02x\n", opcode[1]);					opbytes = 2;	break;	//MVI E, d8
		case 0x1f:	printi("RAR", "");														break;	//RAR
	}

	return opbytes;
}
