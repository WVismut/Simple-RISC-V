/*
    Note:
    Sometimes you will find expressions like these:
    (void)func();
    casting to void is used to silence warnings from that expressions when use clang-tidy
*/

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

typedef enum { ELF, BIN } filetype_t;

typedef struct {
    char file_name[256];
    bool debug;
    size_t ram_size;
    filetype_t filetype;
} flags_t;

r_instruction_t fetch_r(uint32_t instruction) {
    r_instruction_t new_instruction;

    new_instruction.rd     = (instruction >> 7) & 0x1F;
    new_instruction.funct3 = (instruction >> 12) & 0x07;
    new_instruction.rs1    = (instruction >> 15) & 0x1F;
    new_instruction.rs2    = (instruction >> 20) & 0x1F;
    new_instruction.funct7 = (instruction >> 25) & 0x7F;

    return new_instruction;
}

i_instruction_t fetch_i(uint32_t instruction) {
    i_instruction_t new_instruction;

    new_instruction.rd     = (instruction >> 7) & 0x1F;
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
    new_instruction.imm    = ((instruction >> 7) & 0x1F) | ((instruction >> 20) & 0x7E0);

    return new_instruction;
}

u_instruction_t fetch_u(uint32_t instruction) {
    u_instruction_t new_instruction;

    new_instruction.rd  = (instruction >> 7) & 0x1F;
    new_instruction.imm = (instruction >> 12) & 0xFFFFF;

    return new_instruction;
}

inline static void debug_fn(hart_state_t hart) {
    while (true) {
        char buffer[128];

        printf("debug> ");
        if (fgets(buffer, sizeof buffer, stdin) == NULL) {
            printf("fread has returned NULL\n");
            return;
        }
        if (buffer[0] == '\n') {
            continue;
        }
        buffer[strcspn(buffer, "\n")] = 0;

        char *endptr;
        char *command = strtok(buffer, " ");
        char *reg     = NULL;
        if (strcmp(command, "reg") == 0) {
            while ((reg = strtok(NULL, " ")) != NULL) {
                if (strcmp(reg, "pc") == 0) {
                    printf("pc = %d\n", hart.pc);
                } else if (reg[0] == 'x') {
                    char *reg_num_ptr = &reg[1];
                    int reg_num       = (int) strtol(reg_num_ptr, &endptr, 10);
                    printf("x%d = %d\n", reg_num, hart.x[reg_num]);
                } else {
                    printf("Usage: \"reg xN\" or \"reg pc\"\n"
                           "Example 1: reg x0\n"
                           "Example 2: reg pc\n");
                }
            }
        } else if (strcmp(command, "step") == 0) {
            return;
        } else if (strcmp(command, "exit") == 0) {
            exit(0);
        } else {
            printf("Invalid command: %s\n", command);
        }
    }
}

inline static uint32_t extend_sign(uint32_t value) {
    if (value & 0x800) {
        return value | 0xFFFFF000;
    }

    return value;
}

inline static flags_t parse_flags(int argc, char **argv) {
    char *endptr;
    flags_t flags;
    flags.ram_size      = 0;
    bool found_filename = false;
    bool found_ram_size = false;
    bool found_filetype = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) {
            flags.debug = true;
        } else if (strcmp(argv[i], "-n") == 0) {
            if (argc - 1 == i) {
                printf("Expected filename after -n option\n");
                exit(1);
            }

            i++;
            strncpy(flags.file_name, argv[i], 256);
            found_filename = true;
        } else if (strcmp(argv[i], "-m") == 0) {
            if (argc - 1 == i) {
                printf("Expected ram size after -m option\n");
                exit(1);
            }

            i++;
            flags.ram_size = strtol(argv[i], &endptr, 10);
            found_ram_size = true;
        } else if (strcmp(argv[i], "-f") == 0) {
            if (argc - 1 == i) {
                printf("Expected filetype after -f option\n");
                exit(1);
            }

            i++;
            if (strcmp(argv[i], "elf") == 0) {
                flags.filetype = ELF;
            } else if (strcmp(argv[i], "bin") == 0) {
                flags.filetype = BIN;
            } else {
                printf(
                    "No such filetype: %s\n"
                    "Excpected one of these: elf, bin",
                    argv[i]
                );
            }

            found_filetype = true;
        } else {
            printf("Invalid command line argument: %s\n", argv[i]);
            exit(1);
        }
    }

    if (!(found_filename)) {
        printf("Fatal: no filename specified\n");
        exit(1);
    }

    if (!(found_ram_size)) {
        printf("Warning: no ram size was specified. If the code uses stack or "
               "heap it will lead to fatal error\n");
    }

    if (!(found_filetype)) {
        printf("Warning: no filetype was specified. Defaulting to bin\n");
        flags.filetype = BIN;
    }

    return flags;
}

int main(int argc, char **argv) {
    flags_t flags = parse_flags(argc, argv);

    FILE *file = fopen(flags.file_name, "rb");
    if (file == NULL) {
        printf("Can't open file: %s\n", flags.file_name);
        return 1;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        printf("fseek returned non null value\n");
        return 1;
    }

    size_t file_size = ftell(file);

    if (fseek(file, 0, SEEK_SET) != 0) {
        printf("fseek returned non null value\n");
        return 1;
    }

    uint8_t *program = malloc(file_size + flags.ram_size);
    if (program == NULL) {
        (void) fclose(file);
        printf("Can't allocate memory\n");
        return 1;
    }

    uint32_t *code = (uint32_t *) program;
    uint8_t *ram   = program + file_size;
    memset(ram, 0, flags.ram_size);

    if (fread(code, 1, file_size, file) != file_size) {
        printf("can't properly read file\n");
        (void) fclose(file);
        free(code);
        return 1;
    }

    if (fclose(file) != 0) {
        printf("Error closing file!\n");
        return 1;
    }

    hart_state_t main_hart;
    main_hart.pc = 0;
    memset(main_hart.x, 0, sizeof main_hart.x);

    r_instruction_t r_instr;
    i_instruction_t i_instr;
    s_instruction_t s_instr;
    u_instruction_t u_instr;

    while (main_hart.pc < (file_size / 4)) {

        uint32_t instruction = code[main_hart.pc];
        uint8_t opcode       = instruction & 0x7F;

        switch (opcode & 0b11) {
        /* This case is respobsible for all 32-bit lenght instructions */
        case 0b11:

            /* because the lower 2 bits of an opcode determine the instruction
               lenght, We don't need them anymore - we are inside of case, where
               all of the instructions are 32-bit in length. Thus, code below
               don't need these bits */
            switch ((opcode >> 2) & 0x1F) {

            /* The lui instruction */
            case 0b01101:
                u_instr = fetch_u(instruction);
                if (u_instr.rd == 0) {
                    break;
                }
                main_hart.x[u_instr.rd] = u_instr.imm << 12;
                break;

            /* The aupic instruction */
            case 0b00101:
                u_instr = fetch_u(instruction);
                main_hart.pc += u_instr.imm << 12;
                break;

            /* The next case is responsible for a lot of I-type instructions */
            /* They differ by the "funct" parameter */
            case 0b00100:
                i_instr = fetch_i(instruction);
                if (i_instr.rd == 0) {
                    break;
                }

                /* The addi instruction */
                switch (i_instr.funct3 & 0b111) {

                /* The addi instruction */
                case 0b000:
                    main_hart.x[i_instr.rd] = extend_sign(i_instr.imm) + main_hart.x[i_instr.rs1];
                    break;

                /* The slti instruction */
                case 0b010:
                    if ((int32_t) main_hart.x[i_instr.rs1] < (int32_t) extend_sign(i_instr.imm)) {
                        main_hart.x[i_instr.rd] = 1;
                    } else {
                        main_hart.x[i_instr.rd] = 0;
                    }
                    break;

                /* The sltiu instruction */
                case 0b011:
                    if (main_hart.x[i_instr.rs1] < i_instr.imm) {
                        main_hart.x[i_instr.rd] = 1;
                    } else {
                        main_hart.x[i_instr.rd] = 0;
                    }
                    break;

                /* The xori instruction */
                case 0b100:
                    main_hart.x[i_instr.rd] = i_instr.imm ^ main_hart.x[i_instr.rs1];
                    break;

                /* The ori instruction */
                case 0b110:
                    main_hart.x[i_instr.rd] = i_instr.imm | main_hart.x[i_instr.rs1];
                    break;

                /* The andi instruction */
                case 0b111:
                    main_hart.x[i_instr.rd] = i_instr.imm & main_hart.x[i_instr.rs1];
                    break;

                /* The slli instruction */
                case 0b001:
                    main_hart.x[i_instr.rd] = main_hart.x[i_instr.rs1] << (i_instr.imm & 0x1F);
                    break;

                /* The next case is responsible for two instructions: srli and
                 * srai */
                /* they differ by the highest 5 bits of "imm" value */
                case 0b101:
                    switch (i_instr.imm >> 7) {
                    /* The srli instruction */
                    case 0b00000:
                        main_hart.x[i_instr.rd] = main_hart.x[i_instr.rs1] >> (i_instr.imm & 0x1F);
                        break;

                    /* The srai instruction */
                    case 0b01000:
                        main_hart.x[i_instr.rd] = (int32_t) main_hart.x[i_instr.rs1] >>
                                                  (i_instr.imm & 0x1F);
                        break;
                    }
                    break;
                }
                /* The end of case that is responsible for a lot of I-type
                 * instructions */
                break;

            /* This case is responsible for a lot of R-type instructions */
            case 0b01100:
                r_instr = fetch_r(instruction);
                if (r_instr.rd == 0) {
                    break;
                }

                switch (r_instr.funct7) {
                case 0b0000000:
                    switch (r_instr.funct3) {
                    /* The add instruction */
                    case 0b000:
                        main_hart.x[r_instr.rd] = main_hart.x[r_instr.rs1] +
                                                  main_hart.x[r_instr.rs2];
                        break;

                    /* The sll instrucion */
                    case 0b001:
                        main_hart.x[r_instr.rd] = main_hart.x[r_instr.rs1]
                                                  << (main_hart.x[r_instr.rs2] & 0b11111);
                        break;

                    /* The slt instruction*/
                    case 0b010:
                        if ((int32_t) main_hart.x[r_instr.rs1] <
                            (int32_t) main_hart.x[r_instr.rs2]) {
                            main_hart.x[r_instr.rd] = 1;
                        } else {
                            main_hart.x[r_instr.rd] = 0;
                        }
                        break;

                    /* The sltu instruction */
                    case 0b011:
                        if (main_hart.x[r_instr.rs1] < main_hart.x[r_instr.rs2]) {
                            main_hart.x[r_instr.rd] = 1;
                        } else {
                            main_hart.x[r_instr.rd] = 0;
                        }
                        break;

                    /* The xor instruction*/
                    case 0b100:
                        main_hart.x[r_instr.rd] = main_hart.x[r_instr.rs1] ^
                                                  main_hart.x[r_instr.rs2];
                        break;

                    /* The srl instruction*/
                    case 0b101:
                        main_hart.x[r_instr.rd] = main_hart.x[r_instr.rs1] >>
                                                  (main_hart.x[r_instr.rs2] & 0b11111);
                        break;

                    /* The or instruction */
                    case 0b110:
                        main_hart.x[r_instr.rd] = main_hart.x[r_instr.rs1] |
                                                  main_hart.x[r_instr.rs2];
                        break;

                    /* The and instruction */
                    case 0b111:
                        main_hart.x[r_instr.rd] = main_hart.x[r_instr.rs1] &
                                                  main_hart.x[r_instr.rs2];
                        break;
                    }
                    break;

                case 0b100000:
                    switch (r_instr.funct3) {
                    /* The sub instruction */
                    case 0b000:
                        main_hart.x[r_instr.rd] = main_hart.x[r_instr.rs1] -
                                                  main_hart.x[r_instr.rs2];
                        break;

                    /* The sra instruction */
                    case 0b101:
                        main_hart.x[r_instr.rd] = (int32_t) main_hart.x[r_instr.rs1] >>
                                                  ((int32_t) main_hart.x[r_instr.rs2] & 0b11111);
                        break;
                    }
                    break;
                }
                // new cases here
            }
            break;
        default:
            printf("16-bit instrucions are not supported right now\n");
            return 1;
        }

        if (flags.debug) {
            debug_fn(main_hart);
        }

        main_hart.pc++;
    }

    free(code);
    return 0;
}
