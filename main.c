#include "i8080/i8080.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void file_to_mem(uint8_t *memory, const char* file_name, uint16_t offset) {
	
	//try open file	
	FILE *file = fopen(file_name, "rb");
	if(!file) {
		printf("Could not read file: %s\n", file_name);
		exit(0);
	}

	//get file size
	fseek(file, 0L, SEEK_END);
	int file_size = ftell(file);
	fseek(file, 0L, SEEK_SET);

	//read file contents into state memory
	uint8_t *buffer = &memory[offset];
	fread(buffer, file_size, 1, file);	

	memory[368] = 0x7;
	fclose(file);
}

void run_testrom(i8080_t *state) {

    state->pc = 0x100;	//tests starting point

	i8080_write_byte(state, 5, 0xc9);

    printf("*******************\n");

    while (1) {

        const uint16_t current_pc = state->pc;

		if(i8080_read_byte(state, state->pc) == 0x76)
            printf("HLT at %04X\n", state->pc);

        if (state->pc == 5) {

            if (state->c == 9) {

				for(uint16_t i = (state->d << 8 | state->e); i8080_read_byte(state, i) != '$'; i++)
                    printf("%c", i8080_read_byte(state, i));

            }

            if (state->c == 2)
                printf("%c", state->e);
        }

		//i8080_disassemble(state->external_memory, state->pc);
        i8080_step(state);
		//i8080_print(state);

        if (state->pc == 0) {
            printf("\nJumped to 0x0000 from 0x%04X\n\n", current_pc);
            break;
        }
    }
}

int main() {
	i8080_t state;
	init_i8080(&state);
	state.external_memory = malloc(I8080_MAX_MEMORY);
	int mem_length = sizeof(state.external_memory);

	memset(state.external_memory, 0, mem_length);
	file_to_mem(state.external_memory, "tests/TST8080.COM", 0x100);		
	run_testrom(&state);

	memset(state.external_memory, 0, mem_length);
	file_to_mem(state.external_memory, "tests/CPUTEST.COM", 0x100);		
	run_testrom(&state);

	memset(state.external_memory, 0, mem_length);
	file_to_mem(state.external_memory, "tests/8080PRE.COM", 0x100);		
	run_testrom(&state);

	memset(state.external_memory, 0, mem_length);
	file_to_mem(state.external_memory, "tests/8080EXM.COM", 0x100);		
	run_testrom(&state);
}
