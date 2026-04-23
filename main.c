#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

typedef struct {
    uint8_t opcode;
    uint8_t rd;
    uint8_t funct3;
    uint8_t rs1;
    uint8_t rs2;
    uint8_t funct7;
} r_instruction_t;

typedef struct {
    uint8_t opcode;
    uint8_t rd;
    uint8_t funct3;
    uint8_t rs1;
    uint16_t imm;
} i_instruction_t;

typedef struct {
    uint8_t opcode;
    uint8_t funct3;
    uint8_t rs1;
    uint8_t rs2;
    uint16_t imm;
} s_instruction_t;

typedef struct {
    uint8_t opcode;
    uint8_t rd;
    uint32_t imm;
} u_instruction_t;

typedef struct {
    uint32_t x[32];
    uint32_t pc;
} hart_state_t;

r_instruction_t fetch_r(uint32_t instruction) {
    r_instruction_t new_instruction;

    new_instruction.rd     = (instruction >> 7)  & 0x1F;
    new_instruction.funct3 = (instruction >> 12) & 0x07;
    new_instruction.rs1    = (instruction >> 15) & 0x1F;
    new_instruction.rs2    = (instruction >> 20) & 0x1F;
    new_instruction.funct7 = (instruction >> 25) & 0x7F;

    return new_instruction;
}

i_instruction_t fetch_i(uint32_t instruction) {
    i_instruction_t new_instruction;

    new_instruction.rd     = (instruction >> 7)  & 0x1F;
    new_instruction.funct3 = (instruction >> 12) & 0x07;
    new_instruction.rs1    = (instruction >> 15) & 0x1F;
    new_instruction.imm    = (instruction >> 20) & 0xFFF;

    return new_instruction;    
}

s_instruction_t fetch_s(uint32_t instruction) {
    s_instruction_t new_instruction;

    new_instruction.funct3 = (instruction >> 12) & 0x07;
    new_instruction.rs1    = (instruction >> 15) & 0x1F;
    new_instruction.rs2    = (instruction >> 20) & 0x1F;
    new_instruction.imm    = (instruction >> 7)  & 0x1F
                           | (instruction >> 20) & 0x7E0;

    return new_instruction;
}

u_instruction_t fetch_u(uint32_t instruction) {
    u_instruction_t new_instruction;

    new_instruction.rd     = (instruction >> 7)  & 0x1F;
    new_instruction.imm    = (instruction >> 12) & 0xFFFFF;

    return new_instruction;
}

inline static void debug_fn(hart_state_t hart) {
    while (true) {
        char buffer[128];

        printf("debug> ");
        fgets(buffer, sizeof buffer, stdin);
        buffer[strcspn(buffer, "\n")] = 0;

        char *command = strtok(buffer, " ");
        char *reg = NULL;
        if (strcmp(command, "reg") == 0) {
            while ((reg = strtok(NULL, " ")) != NULL) {
                if (strcmp(reg, "pc") == 0) {
                    printf("pc = %d\n", hart.pc);
                } else if (reg[0] == 'x') {
                    char *reg_num_ptr = &reg[1];
                    int reg_num = atoi(reg_num_ptr);
                    printf("x%d = %d\n", reg_num, hart.x[reg_num]);
                } else {
                    printf("Usage: \"reg xN\" or \"reg pc\"\n"
                           "Example 1: reg x0\n"
                           "Example 2: reg pc\n");
                }
            }
        } else if (strcmp(command, "step") == 0) return;
          else if (strcmp(command, "exit") == 0) exit(0);
          else printf("Invalid command: %s\n", command);
    }
    
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: riscv-emul [bin file name] (d)\n");
        return 1;
    }

    FILE *file = fopen(argv[1], "rb");
    if (file == NULL) {
        printf("Can't open file: %s\n", argv[1]);
        return 1;
    }

    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    printf("File size: %zu\n", file_size);

    uint32_t *program = malloc(file_size);
    if (program == NULL) {
        fclose(file);
        printf("Can't allocate memory\n");
    }

    fread(program, 1, file_size, file);
    fclose(file);

    hart_state_t main_hart;
    main_hart.pc = 0;
    main_hart.x[0] = 0;

    r_instruction_t r_instr;
    i_instruction_t i_instr;
    s_instruction_t s_instr;
    u_instruction_t u_instr;

    bool debug = false;
    if (argc > 2) {
        if (strcmp(argv[2], "d") == 0)
            debug = true;
    }

    while (main_hart.pc < (file_size / 4)) {

        uint32_t instruction = program[main_hart.pc];
        uint8_t opcode = instruction & 0x7F;
        
        switch (opcode & 0b11) {
            case 0b11:
                switch ((opcode >> 2) & 0x1F) {
                    case 0b01101: // lui
                        u_instr = fetch_u(instruction);
                        if (u_instr.rd == 0) break;
                        main_hart.x[u_instr.rd] = u_instr.imm << 12;
                        break;
                    
                    case 0b00101: // auipc
                        u_instruction_t u_instr = fetch_u(instruction);
                        main_hart.pc += u_instr.imm << 12;
                        break;

                    case 0b00100: // immediate int instr
                        i_instr = fetch_i(instruction);
                        if (i_instr.rd == 0) break;

                        switch (i_instr.funct3 & 0b111) {
                            case 0b000: // addi
                                if (i_instr.imm & 0x800) 
                                    main_hart.x[i_instr.rd] = (i_instr.imm + main_hart.x[i_instr.rs1]) | 0xFFFFF000;
                                else 
                                    main_hart.x[i_instr.rd] = i_instr.imm + main_hart.x[i_instr.rs1];
                                break;

                            case 0b010: // slti
                                if ((int32_t)main_hart.x[i_instr.rs1] <
                                    (int32_t)i_instr.imm)
                                    main_hart.x[i_instr.rd] = 1;
                                else
                                    main_hart.x[i_instr.rd] = 0;
                                break;

                            case 0b011: // sltiu
                                if (main_hart.x[i_instr.rs1] <
                                    i_instr.imm)
                                    main_hart.x[i_instr.rd] = 1;
                                else
                                    main_hart.x[i_instr.rd] = 0;
                                break;

                            case 0b100: // xori
                                main_hart.x[i_instr.rd] = i_instr.imm ^ main_hart.x[i_instr.rs1];
                                break;

                            case 0b110: // ori
                                main_hart.x[i_instr.rd] = i_instr.imm | main_hart.x[i_instr.rs1];
                                break;

                            case 0b111: // andi
                                main_hart.x[i_instr.rd] = i_instr.imm & main_hart.x[i_instr.rs1];
                                break;

                            case 0b001: // slli
                                main_hart.x[i_instr.rd] = main_hart.x[i_instr.rs1] << (i_instr.imm & 0x1F);
                                break;
                                
                            case 0b101: // srli / srai
                                switch (i_instr.imm >> 7) {
                                    case 0b00000: // srli
                                        main_hart.x[i_instr.rd] = main_hart.x[i_instr.rs1] >> (i_instr.imm & 0x1F);
                                        break;
                                    case 0b01000: // srai
                                        main_hart.x[i_instr.rd] = (int32_t)main_hart.x[i_instr.rs1] >> (i_instr.imm & 0x1F);
                                        break;
                                }
                                break;
                        }
                        break;
                }
                break;
            default:
                printf("16-bit instrucions are not supported right now\n");
                break;
        }

        if (debug) debug_fn(main_hart);

        main_hart.pc++;
    }

    free(program);
    return 0;
}