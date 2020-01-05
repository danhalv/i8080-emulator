#ifndef I8080_H
#define I8080_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define I8080_MAX_MEMORY	65536	//i8080's stack pointer holds 2 bytes; 2^16 (65536) is the largest number which can be represented by 16 bits

//padding structure allows for correct format for PSW register pair
typedef struct {
	uint8_t s:1;	//sign, flags whether bit 7 is set or not
	uint8_t z:1;	//zero, result of operation leaves byte with value 0
	uint8_t pad1:1;	//padding, always 0 in PSW
	uint8_t	ac:1;	//auxiliary carry, carry out of bit 3
	uint8_t pad2:1;	//padding, always 0 in PSW
	uint8_t p:1;	//parity, number of 1 bits of byte is counted: if odd p=0; if even p=1
	uint8_t pad3:1;	//padding, always 1 in PSW
	uint8_t c:1;	//carry, carry out of byte
} conditionbits_t;

/*
	Some i8080 instructions operate
	on registers in the following pairs:
	B and C
	D and E
	H and L
	
	Register pair PSW (Program Status Word)
	refers to register A and special byte reflecting
	the machine flags (conditionbits_t)
*/
typedef struct i8080_t {
	uint8_t a, b, c, d, e, h, l;	//7 registers
	uint16_t pc, sp;				//program counter, stack pointer
	conditionbits_t cb;
	uint8_t ie;						//interrupts enabled
	
	uint8_t *external_memory;		
} i8080_t;

typedef struct {
	uint8_t *first;
	uint8_t *second;
} regpair_t;

conditionbits_t init_conditionbits();					//sets all bits to 0, but pad3 to 1 for PSW format

void i8080_step(i8080_t *state);						//executes one instruction at pc and increments pc

//uint8_t i8080_disassemble(i8080_t *state);				//disassembles instruction of memory at pc and returns new pc
uint8_t i8080_disassemble(unsigned char* buffer, uint16_t pc);

//single register instructions, operates on registers or memory location referenced by M (HL pair)
void i8080_inr(i8080_t *state, uint8_t *reg);	//increment
void i8080_dcr(i8080_t *state, uint8_t *reg);	//decrement
void i8080_cma(i8080_t *state);					//complement accumulator, flips all bits
void i8080_daa(i8080_t *state);					//decimal adjust accumulator

void i8080_nop(i8080_t *state);					//NOP (no operation) instruction

//data transfer instructions, transfers data between registers or between memory and registers
void i8080_mov(i8080_t *state, uint8_t *dst, const uint8_t *src);	//move byte from src to dst
void i8080_stax(i8080_t *state, const regpair_t pair);				//contents of accumulator stored in memory location addressed by register pair BC or DE
void i8080_ldax(i8080_t *state, const regpair_t pair);				//contents of memory location addressed by register pair BC or DE replaces contents of accumulator

//register or memory to accumulator instructions
void i8080_add(i8080_t *state, const uint8_t *reg);		//byte at register or memory ref. is added to the contents of the accumulator using two's complement arithmetic
void i8080_adc(i8080_t *state, const uint8_t *reg);		//byte + content of carry bit is added to contents of accumulator
void i8080_sub(i8080_t *state, const uint8_t *reg);		//byte is subtracted from accumulator using two's complement arithmetic
void i8080_sbb(i8080_t *state, const uint8_t *reg);		//byte + content of carry bit is subtracted from accumulator using two's complement arithmetic
void i8080_ana(i8080_t *state, const uint8_t *reg);		//byte is logically ANDed bit by bit with contents of accumulator
void i8080_xra(i8080_t *state, const uint8_t *reg);		//byte is logically XORed bit by bit with contents of accumulator
void i8080_ora(i8080_t *state, const uint8_t *reg);		//byte is logically ORed bit by bit with contents of accumulator
void i8080_cmp(i8080_t *state, const uint8_t *reg);		//byte compared to contents of accumulator. Subtracts accumulator by byte and set condition bits

//rotate accumulator instructions
void i8080_rlc(i8080_t *state);		//contents of accumulator rotated 1 bit position to the left, with high order bit transferred to low-order bit position of the accumulator
void i8080_rrc(i8080_t *state);		//contents of acc. rotated 1 bit position to the right, with low order bit set to high order bit
void i8080_ral(i8080_t *state);		//acc. rotated 1 bit to the left, carry bit sets lbit, hbit replaces carry bit
void i8080_rar(i8080_t *state);		//acc. rotated 1 bit to the right, carry bit sets hbit, lbit sets carry

//register pair instructions
void i8080_push(i8080_t *state, const regpair_t pair);					//contents of register pair are saved in two bytes of memory indicated by stack pointer
void i8080_pop(i8080_t *state, const regpair_t pair);					//register pair is assigned two bytes of memory at stack pointer
void i8080_dad(i8080_t *state, const regpair_t pair, uint16_t *sp);		//register pair or SP is added to the contents of HL using two's complement replacing HL
void i8080_inx(i8080_t *state, regpair_t pair, uint16_t *sp);			//register pair is incremented by one
void i8080_dcx(i8080_t *state, regpair_t pair, uint16_t *sp);			//register pair is decremented by one
void i8080_xchg(i8080_t *state);										//contents of HL swapped with contents of DE
void i8080_xthl(i8080_t *state);										//contents of L register exchanged with memory byte at stack pointer
void i8080_sphl(i8080_t *state);										//contents of sp replaced by contents of HL

//immediate instructions
void i8080_lxi(i8080_t *state, regpair_t pair, uint16_t *sp, uint8_t low, uint8_t high);	//replaces reg pair with immediate data
void i8080_mvi(i8080_t *state, uint8_t *reg, uint8_t byte);		//byte of immediate data is stored in register or memory byte
void i8080_adi(i8080_t *state, uint8_t byte);					//byte is added to acc. using two's complement
void i8080_aci(i8080_t *state, uint8_t byte);					//byte + carry bit is added to the acc.
void i8080_sui(i8080_t *state, uint8_t byte);					//byte is subtracted from the acc. using two's comp.
void i8080_sbi(i8080_t *state, uint8_t byte);					//byte + carry bit is subtracted from acc. using two's comp.
void i8080_ani(i8080_t *state, uint8_t byte);					//byte is logically ANDed with acc. 
void i8080_xri(i8080_t *state, uint8_t byte);					//byte is XORed with acc.
void i8080_ori(i8080_t *state, uint8_t byte);					//byte is ORed with acc.
void i8080_cpi(i8080_t *state, uint8_t byte);					//byte compared with acc. Subtracts byte from acc. using two's comp. leaving acc. unchanged

//direct addressing instructions
void i8080_sta(i8080_t *state, uint8_t low, uint8_t high);		//acc. replaces byte at mem. addr. formed by concat. high and low
void i8080_lda(i8080_t *state, uint8_t low, uint8_t high);		//byte at mem. addr. (high | low) replaces acc.
void i8080_shld(i8080_t *state, uint8_t low, uint8_t high);		//L stored at address, h stored at address+1
void i8080_lhld(i8080_t *state, uint8_t low, uint8_t high);		//byte at address stored in L, byte at address+1 stored at H

//jump instructions
void i8080_pchl(i8080_t *state);	//H replaces most-sign. 8 bits of pc, L replaces least-sign. 8 bits of pc
void i8080_jmp(i8080_t *state, uint8_t low, uint8_t high);	//program execution continues at mem. addr.

//conditional jump instructions, depends on conditionbits
void i8080_jc(i8080_t *state, uint8_t low, uint8_t high);	
void i8080_jnc(i8080_t *state, uint8_t low, uint8_t high);	
void i8080_jz(i8080_t *state, uint8_t low, uint8_t high);	
void i8080_jnz(i8080_t *state, uint8_t low, uint8_t high);	
void i8080_jm(i8080_t *state, uint8_t low, uint8_t high);	
void i8080_jp(i8080_t *state, uint8_t low, uint8_t high);	
void i8080_jpe(i8080_t *state, uint8_t low, uint8_t high);	
void i8080_jpo(i8080_t *state, uint8_t low, uint8_t high);	

//call subroutine instructions
void i8080_call(i8080_t *state, uint8_t low, uint8_t high);	//return address pushed to stack, pc set to address

//conditional call instructions, depends on conditionbits
void i8080_cc(i8080_t *state, uint8_t low, uint8_t high);	
void i8080_cnc(i8080_t *state, uint8_t low, uint8_t high);	
void i8080_cz(i8080_t *state, uint8_t low, uint8_t high);	
void i8080_cnz(i8080_t *state, uint8_t low, uint8_t high);	
void i8080_cm(i8080_t *state, uint8_t low, uint8_t high);	
void i8080_cp(i8080_t *state, uint8_t low, uint8_t high);	
void i8080_cpe(i8080_t *state, uint8_t low, uint8_t high);	
void i8080_cpo(i8080_t *state, uint8_t low, uint8_t high);	

//return from subroutine instructions
void i8080_ret(i8080_t *state);	//pops address off stack and assigns it to pc

//conditional return instructions, depends on conditionbits
void i8080_rc(i8080_t *state);	
void i8080_rnc(i8080_t *state);	
void i8080_rz(i8080_t *state);	
void i8080_rnz(i8080_t *state);	
void i8080_rm(i8080_t *state);	
void i8080_rp(i8080_t *state);	
void i8080_rpe(i8080_t *state);	
void i8080_rpo(i8080_t *state);	

//restart instruction
void i8080_rst(i8080_t *state, uint8_t rst_num);	//pc pushed to stack, rst_num 0-7 determines subroutine location in memory which pc is set to

//interrupt flip-flop instructions
void i8080_ei(i8080_t *state);	//enable interrupts, sets ie
void i8080_di(i8080_t *state);	//disable interrupts, sets ie to 0



#endif	//I8080_H
