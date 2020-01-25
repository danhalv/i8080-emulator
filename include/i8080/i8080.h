#ifndef I8080_H
#define I8080_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define I8080_MAX_MEMORY	65536	//i8080's stack pointer holds 2 bytes; 2^16 (65536) is the largest number which can be represented by 16 bits

//structured according to PSW format
typedef union {
	struct {	
		uint8_t s:1;	//sign
		uint8_t z:1;	//zero
		uint8_t bit5:1;	//always 0
		uint8_t	ac:1;	//auxiliary carry
		uint8_t bit3:1;	//always 0
		uint8_t p:1;	//parity, 1=even; 0=odd
		uint8_t bit1:1;	//always 1
		uint8_t c:1;	//carry
	} flags;
	uint8_t byte;
} conditionbits_t;

typedef struct i8080_t {
	uint8_t a, b, c, d, e, h, l;	//7 registers. pairs: PSW, BC, DE, HL
	uint16_t pc, sp;				//program counter, stack pointer
	uint32_t cycles;				//Hz
	conditionbits_t cb;
	uint8_t ie;						//interrupts enabled
	
	uint8_t *external_memory;		
} i8080_t;

typedef struct {
	uint8_t *first;
	uint8_t *second;
} regpair_t;

void init_conditionbits(conditionbits_t *cb);								//inits members to 0 except bit1 which is always 1
void init_i8080(i8080_t *state);				

void i8080_step(i8080_t *state);											//executes one instruction at current pc
void i8080_interrupt(i8080_t *state, uint8_t low, uint8_t high);			//pushes current pc onto stack and jumps to address

uint8_t i8080_disassemble(const unsigned char* buffer, const uint16_t pc);	//prints assembly from hex
void i8080_print(i8080_t *state);											//prints state of cpu

//memory handling, works on external memory
void i8080_write_byte(i8080_t *state, const uint16_t address, const uint8_t byte);
uint8_t i8080_read_byte(i8080_t *state, const uint16_t address);

//carry bit instructions
void i8080_stc(i8080_t *state);					
void i8080_cmc(i8080_t *state);					

//single register instructions, operates on registers or memory location referenced by M (HL pair)
void i8080_inr(i8080_t *state, uint8_t *reg);	
void i8080_dcr(i8080_t *state, uint8_t *reg);	
void i8080_cma(i8080_t *state);					
void i8080_daa(i8080_t *state);					

void i8080_nop(i8080_t *state);					

//data transfer instructions, transfers data between registers or between memory and registers
void i8080_mov(i8080_t *state, uint8_t *dst, const uint8_t *src);	
void i8080_stax(i8080_t *state, const regpair_t pair);				
void i8080_ldax(i8080_t *state, const regpair_t pair);				

//register or memory to accumulator instructions
void i8080_add(i8080_t *state, const uint8_t *reg);		
void i8080_adc(i8080_t *state, const uint8_t *reg);		
void i8080_sub(i8080_t *state, const uint8_t *reg);		
void i8080_sbb(i8080_t *state, const uint8_t *reg);		
void i8080_ana(i8080_t *state, const uint8_t *reg);		
void i8080_xra(i8080_t *state, const uint8_t *reg);		
void i8080_ora(i8080_t *state, const uint8_t *reg);		
void i8080_cmp(i8080_t *state, const uint8_t *reg);		

//rotate accumulator instructions
void i8080_rlc(i8080_t *state);		
void i8080_rrc(i8080_t *state);		
void i8080_ral(i8080_t *state);		
void i8080_rar(i8080_t *state);		

//register pair instructions
void i8080_push(i8080_t *state, const regpair_t pair);					
void i8080_pop(i8080_t *state, const regpair_t pair);					
void i8080_dad(i8080_t *state, uint16_t addend);
void i8080_inx(i8080_t *state, regpair_t pair, uint16_t *sp);			
void i8080_dcx(i8080_t *state, regpair_t pair, uint16_t *sp);			
void i8080_xchg(i8080_t *state);										
void i8080_xthl(i8080_t *state);										
void i8080_sphl(i8080_t *state);										

//immediate instructions
void i8080_lxi(i8080_t *state, regpair_t pair, uint16_t *sp, uint8_t low, uint8_t high);	
void i8080_mvi(i8080_t *state, uint8_t *reg, uint8_t byte);		
void i8080_adi(i8080_t *state, uint8_t byte);					
void i8080_aci(i8080_t *state, uint8_t byte);					
void i8080_sui(i8080_t *state, uint8_t byte);					
void i8080_sbi(i8080_t *state, uint8_t byte);					
void i8080_ani(i8080_t *state, uint8_t byte);					
void i8080_xri(i8080_t *state, uint8_t byte);					
void i8080_ori(i8080_t *state, uint8_t byte);					
void i8080_cpi(i8080_t *state, uint8_t byte);					

//direct addressing instructions
void i8080_sta(i8080_t *state, uint8_t low, uint8_t high);		
void i8080_lda(i8080_t *state, uint8_t low, uint8_t high);		
void i8080_shld(i8080_t *state, uint8_t low, uint8_t high);		
void i8080_lhld(i8080_t *state, uint8_t low, uint8_t high);		

//jump instructions
void i8080_pchl(i8080_t *state);	//H replaces most-sign. 8 bits of pc, L replaces least-sign. 8 bits of pc
void i8080_jmp(i8080_t *state, uint8_t low, uint8_t high);	

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
void i8080_ret(i8080_t *state);

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
void i8080_rst(i8080_t *state, uint8_t rst_num);

//interrupt flip-flop instructions
void i8080_ei(i8080_t *state);	
void i8080_di(i8080_t *state);	


#endif	//I8080_H
