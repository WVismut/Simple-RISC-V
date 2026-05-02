/*
    Note:
    Sometimes you will find expressions like these:
    (void)func();
    casting to void is used to silence warnings from clang-tidy
*/

#include <elf.h>
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
    uint64_t x[31];
    uint64_t pc;
} hart_state_t;

typedef struct {
    char file_name[256];
    bool debug;
    size_t ram_size;
} flags_t;

typedef struct {
    void *vm_memory;
    uint64_t translation_offset;
    uint64_t memory_size;
    uint64_t code_segment_min;
    uint64_t code_segment_max;
} memory_config_t;

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

/* Basically, J instructions are just drunk U instructions */
/* Mission of this function is to sober J instr */
u_instruction_t fetch_j(uint32_t instruction) {
    u_instruction_t new_instruction;

    new_instruction.rd  = (instruction >> 7) & 0x1F;
    new_instruction.imm = 0;

    // wth
    new_instruction.imm |= ((instruction >> 31) & 0x1) << 20;  // imm[20]
    new_instruction.imm |= ((instruction >> 12) & 0xFF) << 12; // imm[19:12]
    new_instruction.imm |= ((instruction >> 20) & 0x1) << 11;  // imm[11]
    new_instruction.imm |= ((instruction >> 21) & 0x3FF) << 1; // imm[10:1]

    return new_instruction;
}

/* the same thing as fetch_j */
s_instruction_t fetch_b(uint32_t instruction) {
    s_instruction_t new_instruction;

    new_instruction.funct3 = (instruction >> 12) & 0x07;
    new_instruction.rs1    = (instruction >> 15) & 0x1F;
    new_instruction.rs2    = (instruction >> 20) & 0x1F;

    new_instruction.imm = ((instruction >> 7) & 0x1E) |       // imm[4:1]
                          ((instruction >> 20) & 0x7E0) |     // imm[10:5]
                          ((instruction >> 31) & 0x1) << 12 | // imm[12]
                          ((instruction << 4) & 0x800);       // imm[11]

    return new_instruction;
}

inline static void debug_fn(hart_state_t hart) {
    static int to_skip = 0;

    if (to_skip > 0) {
        to_skip--;
        return;
    }

    while (true) {
        char buffer[128];

        printf("debug> ");
        if (fgets(buffer, sizeof buffer, stdin) == NULL) {
            printf("Fatal: fread has returned NULL\n");
            return;
        }
        if (buffer[0] == '\n') {
            continue;
        }
        buffer[strcspn(buffer, "\n")] = 0;

        char *endptr;
        char *command = strtok(buffer, " ");
        char *token   = NULL;
        if (strcmp(command, "reg") == 0) {
            while ((token = strtok(NULL, " ")) != NULL) {
                if (strcmp(token, "pc") == 0) {
                    printf("pc = 0x%lx\n", hart.pc);
                } else if (token[0] == 'x') {
                    char *reg_num_ptr = &token[1];
                    int reg_num       = (int) strtol(reg_num_ptr, &endptr, 10);
                    printf("x%d = %ld\n", reg_num, hart.x[reg_num]);
                } else {
                    printf("Usage: \"reg xN\" or \"reg pc\"\n"
                           "Example 1: reg x0\n"
                           "Example 2: reg pc\n");
                }
            }
        } else if (strcmp(command, "step") == 0) {
            token = strtok(NULL, " ");
            if (token != NULL) {
                to_skip = (int) strtol(token, &endptr, 10) - 1;
            }

            return;
        } else if (strcmp(command, "exit") == 0) {
            exit(0);
        } else {
            printf("Fatal: Invalid command: %s\n", command);
        }
    }
}

uint64_t extend_sign(uint64_t value, size_t in_size) {
    if (((uint64_t) 0b1 << (in_size - 1)) & value) {
        return value | (UINT64_MAX << in_size);
    }

    return value;
}

inline static flags_t parse_flags(int argc, char **argv) {
    char *endptr;

    flags_t flags;
    flags.ram_size = 0;

    bool found_filename = false;
    bool found_ram_size = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) {
            flags.debug = true;
        } else if (strcmp(argv[i], "-n") == 0) {
            if (argc - 1 == i) {
                printf("Fatal: Expected filename after -n option\n");
                exit(1);
            }

            i++;
            strncpy(flags.file_name, argv[i], 256);
            found_filename = true;
        } else if (strcmp(argv[i], "-m") == 0) {
            if (argc - 1 == i) {
                printf("Fatal: Expected ram size after -m option\n");
                exit(1);
            }

            i++;
            flags.ram_size = strtol(argv[i], &endptr, 10);
            found_ram_size = true;
        } else {
            printf("Fatal: Invalid command line argument: %s\n", argv[i]);
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

    return flags;
}

inline static memory_config_t setup_memory(char *filename) {
    // what function will return
    memory_config_t memory_config;
    memory_config.code_segment_max = 0;

    // open file
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        printf("Fatal: Can't open file: %s\n", filename);
        exit(1);
    }

    // load & parse elf header
    Elf64_Ehdr elf_header;
    if (fread(&elf_header, sizeof elf_header, 1, file) != 1) {
        printf("Fatal: Can't read elf header\n");
        exit(1);
    }

    // load & parse program headers
    Elf64_Phdr *program_headers = malloc((long) elf_header.e_phnum * elf_header.e_phentsize);

    if (fseek(file, (long) elf_header.e_phoff, SEEK_SET) != 0) {
        printf("Fatal: fseek return non zero value\n");
        exit(1);
    }

    if (fread(program_headers, (long) elf_header.e_phnum * elf_header.e_phentsize, 1, file) != 1) {
        printf("Fatal: fread retrun error\n");
        exit(1);
    }

    // find the right ammount of memory for vm
    uint64_t min_address = UINT64_MAX; // vaddr // this is also offset for translation
    uint64_t max_address = 0;          // vaddr + memsz

    // i = 2 is temporary. Everything breaks when i = 0
    for (int i = 2; i < elf_header.e_phnum; i++) {
        Elf64_Phdr phdr = program_headers[i];

        if (phdr.p_type == PT_LOAD) {
            if (phdr.p_vaddr < min_address) {
                min_address = phdr.p_vaddr;
            }

            if (phdr.p_vaddr + phdr.p_memsz > max_address) {
                max_address = phdr.p_vaddr + phdr.p_memsz;
            }

            // find the code segment
            if (phdr.p_flags & 1) {
                memory_config.code_segment_min = phdr.p_vaddr;
                memory_config.code_segment_max = phdr.p_vaddr + phdr.p_memsz;
            }
        }
    }

    if (memory_config.code_segment_max == 0) {
        printf("Fatal: Didn't find the code segment\n");
        exit(1);
    }

    memory_config.code_segment_min = memory_config.code_segment_min - min_address;
    memory_config.code_segment_max = memory_config.code_segment_max - min_address;

    // allocate enough memory
    void *vm_memory = malloc(max_address - min_address);
    if (vm_memory == NULL) {
        printf("Fatal: malloc returned null\n");
        exit(1);
    }
    memset(vm_memory, 0, max_address - min_address);

    // load segments in memory
    // i = 2 is temporary. Everything breaks when i = 0
    for (int i = 2; i < elf_header.e_phnum; i++) {
        Elf64_Phdr phdr = program_headers[i];

        if (phdr.p_type == PT_LOAD) {
            if (fseek(file, (long) phdr.p_offset, SEEK_SET) != 0) {
                printf("Fatal: fseek returned non zero value");
                exit(1);
            }

            if (fread(&((uint8_t *) vm_memory)[phdr.p_vaddr - min_address], phdr.p_filesz, 1, file) != 1) {
                printf("Fatal: fread retruned error\n");
                exit(1);
            }
        }
    }

    // clean
    free(program_headers);

    // retrun
    memory_config.memory_size        = max_address - min_address;
    memory_config.vm_memory          = vm_memory;
    memory_config.translation_offset = min_address;
    return memory_config;
}

int main(int argc, char **argv) {
    flags_t flags = parse_flags(argc, argv);

    memory_config_t memory_config = setup_memory(flags.file_name);

    hart_state_t main_hart;
    main_hart.pc = memory_config.code_segment_min / 4;
    memset(main_hart.x, 0, sizeof main_hart.x);

    r_instruction_t r_instr;
    i_instruction_t i_instr;
    s_instruction_t s_instr;
    u_instruction_t u_instr;
    uint64_t temp;

    while (main_hart.pc < memory_config.code_segment_max) {

        uint32_t instruction = ((uint32_t *) memory_config.vm_memory)[main_hart.pc / 4];
        uint8_t opcode       = instruction & 0x7F;

        switch (opcode & 0b11) {
        /* This case is respobsible for all 32-bit lenght instructions */
        case 0b11:

            /* because the lower 2 bits of an opcode determine the instruction
               lenght, We don't need them anymore - we are inside of case, where
               all of the instructions are 32-bit in length. Thus, code below
               don't need these bits */
            switch (opcode >> 2) {

            /* The lui instruction */
            case 0b01101:
                u_instr = fetch_u(instruction);
                if (u_instr.rd == 0) {
                    break;
                }
                main_hart.x[u_instr.rd] = u_instr.imm << 12;
                break;

            /* The auipc instruction */
            case 0b00101:
                u_instr                 = fetch_u(instruction);
                main_hart.x[u_instr.rd] = (u_instr.imm << 12) + main_hart.pc + memory_config.translation_offset;
                break;

            /* The next case is responsible for a lot of I-type instructions */
            /* They differ by the "funct" parameter */
            case 0b00100:
                i_instr = fetch_i(instruction);
                if (i_instr.rd == 0) {
                    break;
                }

                switch (i_instr.funct3 & 0b111) {

                /* The addi instruction */
                case 0b000:

                    main_hart.x[i_instr.rd] = extend_sign(i_instr.imm, 12) + main_hart.x[i_instr.rs1];
                    break;

                /* The slti instruction */
                case 0b010:

                    if ((int64_t) main_hart.x[i_instr.rs1] < (int64_t) extend_sign(i_instr.imm, 12)) {
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
                        main_hart.x[i_instr.rd] = (int64_t) main_hart.x[i_instr.rs1] >> (i_instr.imm & 0x1F);
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
                        main_hart.x[r_instr.rd] = main_hart.x[r_instr.rs1] + main_hart.x[r_instr.rs2];
                        break;

                    /* The sll instrucion */
                    case 0b001:
                        main_hart.x[r_instr.rd] = main_hart.x[r_instr.rs1] << (main_hart.x[r_instr.rs2] & 0b11111);
                        break;

                    /* The slt instruction*/
                    case 0b010:
                        if ((int64_t) main_hart.x[r_instr.rs1] < (int64_t) main_hart.x[r_instr.rs2]) {
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
                        main_hart.x[r_instr.rd] = main_hart.x[r_instr.rs1] ^ main_hart.x[r_instr.rs2];
                        break;

                    /* The srl instruction*/
                    case 0b101:
                        main_hart.x[r_instr.rd] = main_hart.x[r_instr.rs1] >> (main_hart.x[r_instr.rs2] & 0b11111);
                        break;

                    /* The or instruction */
                    case 0b110:
                        main_hart.x[r_instr.rd] = main_hart.x[r_instr.rs1] | main_hart.x[r_instr.rs2];
                        break;

                    /* The and instruction */
                    case 0b111:
                        main_hart.x[r_instr.rd] = main_hart.x[r_instr.rs1] & main_hart.x[r_instr.rs2];
                        break;
                    }
                    break;

                case 0b100000:
                    switch (r_instr.funct3) {
                    /* The sub instruction */
                    case 0b000:
                        main_hart.x[r_instr.rd] = main_hart.x[r_instr.rs1] - main_hart.x[r_instr.rs2];
                        break;

                    /* The sra instruction */
                    case 0b101:
                        main_hart.x[r_instr.rd] = (int32_t) main_hart.x[r_instr.rs1] >>
                                                  ((int32_t) main_hart.x[r_instr.rs2] & 0b11111);
                        break;
                    }
                    break;

                case 0b0000001:
                    /* RV64M/RV32M instructions */
                    switch (r_instr.funct3) {
                    case 0b000:

                        main_hart.x[r_instr.rd] = (uint64_t) ((__int128_t) main_hart.x[r_instr.rs1] *
                                                              (__int128_t) main_hart.x[r_instr.rs2]);
                        break;

                    case 0b001:

                        main_hart.x[r_instr.rd] = (uint64_t) (((__int128_t) main_hart.x[r_instr.rs1] *
                                                               (__int128_t) main_hart.x[r_instr.rs2]) >>
                                                              64);
                        break;

                    case 0b010:

                        main_hart.x[r_instr.rd] = (uint64_t) (((__int128_t) main_hart.x[r_instr.rs1] *
                                                               (__uint128_t) main_hart.x[r_instr.rs2]) >>
                                                              64);
                        break;

                    case 0b011:

                        main_hart.x[r_instr.rd] = (uint64_t) (((__uint128_t) main_hart.x[r_instr.rs1] *
                                                               (__uint128_t) main_hart.x[r_instr.rs2]) >>
                                                              64);
                        break;

                    case 0b100:

                        main_hart.x[r_instr.rd] = (int32_t) main_hart.x[r_instr.rs1] /
                                                  (int32_t) main_hart.x[r_instr.rs2];
                        break;

                    case 0b101:

                        main_hart.x[r_instr.rd] = main_hart.x[r_instr.rs1] / main_hart.x[r_instr.rs2];
                        break;

                    case 0b110:

                        main_hart.x[r_instr.rd] = (int64_t) main_hart.x[r_instr.rs1] %
                                                  (int64_t) main_hart.x[r_instr.rs2];
                        break;

                    case 0b111:

                        main_hart.x[r_instr.rd] = main_hart.x[r_instr.rs1] % main_hart.x[r_instr.rs2];
                        break;
                    }
                    break;
                }
                break;

            /* This case is responsible for loading things from memory */
            case 0b00000:
                i_instr = fetch_i(instruction);

                switch (i_instr.funct3) {
                /* The lb instruction */
                case 0b000:

                    temp = extend_sign(i_instr.imm, 12) + main_hart.x[i_instr.rs1] - memory_config.translation_offset;

                    if (temp >= memory_config.memory_size) {
                        printf("Fatal: virtual memory address is out of bounds: 0x%lx\n", temp);
                        free(memory_config.vm_memory);
                        return 1;
                    }

                    main_hart.x[i_instr.rd] = extend_sign(((uint8_t *) memory_config.vm_memory)[temp], 8);
                    break;

                /* The lh instruction */
                case 0b001:

                    temp = extend_sign(i_instr.imm, 12) + main_hart.x[i_instr.rs1] - memory_config.translation_offset;

                    if (temp + 1 >= memory_config.memory_size) {
                        printf("Fatal: virtual memory address is out of bounds: 0x%lx\n", temp);
                        free(memory_config.vm_memory);
                        return 1;
                    }

                    main_hart.x[i_instr.rd] = extend_sign(
                        ((uint8_t *) memory_config.vm_memory)[temp] |
                            (((uint8_t *) memory_config.vm_memory)[temp + 1] << 8),
                        16
                    );
                    break;

                /* The lw instruction */
                case 0b010:

                    temp = extend_sign(i_instr.imm, 12) + main_hart.x[i_instr.rs1] - memory_config.translation_offset;

                    if (temp + 3 >= memory_config.memory_size) {
                        printf("Fatal: virtual memory address is out of bounds: 0x%lx\n", temp);
                        free(memory_config.vm_memory);
                        return 1;
                    }

                    main_hart.x[i_instr.rd] = extend_sign(
                        ((uint64_t) ((uint8_t *) memory_config.vm_memory)[temp]) |
                            ((uint64_t) (((uint8_t *) memory_config.vm_memory)[temp + 1]) << 8) |
                            ((uint64_t) (((uint8_t *) memory_config.vm_memory)[temp + 2]) << 16) |
                            ((uint64_t) (((uint8_t *) memory_config.vm_memory)[temp + 3]) << 24),
                        32
                    );
                    break;

                /* The lbu instruction */
                case 0b100:

                    temp = extend_sign(i_instr.imm, 12) + main_hart.x[i_instr.rs1] - memory_config.translation_offset;

                    if (temp >= memory_config.memory_size) {
                        printf("Fatal: virtual memory address is out of bounds: 0x%lx\n", temp);
                        free(memory_config.vm_memory);
                        return 1;
                    }

                    main_hart.x[i_instr.rd] = ((uint8_t *) memory_config.vm_memory)[temp];
                    break;

                /* The lhu instruction */
                case 0b101:

                    temp = extend_sign(i_instr.imm, 12) + main_hart.x[i_instr.rs1] - memory_config.translation_offset;

                    if (temp + 1 >= memory_config.memory_size) {
                        printf("Fatal: virtual memory address is out of bounds: 0x%lx\n", temp);
                        free(memory_config.vm_memory);
                        return 1;
                    }

                    main_hart.x[i_instr.rd] = ((uint8_t *) memory_config.vm_memory)[temp] |
                                              (((uint8_t *) memory_config.vm_memory)[temp + 1] << 8);
                    break;

                /* The lwu instruction */
                case 0b110:

                    temp = extend_sign(i_instr.imm, 12) + main_hart.x[i_instr.rs1] - memory_config.translation_offset;

                    if (temp + 3 >= memory_config.memory_size) {
                        printf("Fatal: virtual memory address is out of bounds: 0x%lx\n", temp);
                        free(memory_config.vm_memory);
                        return 1;
                    }

                    main_hart.x[i_instr.rd] = ((uint64_t) ((uint8_t *) memory_config.vm_memory)[temp]) |
                                              ((uint64_t) (((uint8_t *) memory_config.vm_memory)[temp + 1]) << 8) |
                                              ((uint64_t) (((uint8_t *) memory_config.vm_memory)[temp + 2]) << 16) |
                                              ((uint64_t) (((uint8_t *) memory_config.vm_memory)[temp + 3]) << 24);
                    break;

                /* The ld instruction */
                case 0b011:

                    temp = extend_sign(i_instr.imm, 12) + main_hart.x[i_instr.rs1] - memory_config.translation_offset;

                    if (temp + 7 >= memory_config.memory_size) {
                        printf("Fatal: virtual memory address is out of bounds: 0x%lx\n", temp);
                        free(memory_config.vm_memory);
                        return 1;
                    }

                    main_hart.x[i_instr.rd] = (((uint64_t) ((uint8_t *) memory_config.vm_memory)[temp])) |
                                              (((uint64_t) (((uint8_t *) memory_config.vm_memory)[temp + 1])) << 8) |
                                              (((uint64_t) (((uint8_t *) memory_config.vm_memory)[temp + 2])) << 16) |
                                              (((uint64_t) (((uint8_t *) memory_config.vm_memory)[temp + 3])) << 24) |
                                              (((uint64_t) (((uint8_t *) memory_config.vm_memory)[temp + 4])) << 32) |
                                              (((uint64_t) (((uint8_t *) memory_config.vm_memory)[temp + 5])) << 40) |
                                              (((uint64_t) (((uint8_t *) memory_config.vm_memory)[temp + 6])) << 48) |
                                              (((uint64_t) (((uint8_t *) memory_config.vm_memory)[temp + 7])) << 56);
                    break;
                }
                break;

            /* This case is responsible for loading things into memory */
            case 0b01000:
                s_instr = fetch_s(instruction);

                switch (s_instr.funct3) {

                case 0b000:

                    temp = extend_sign(s_instr.imm, 12) + main_hart.x[s_instr.rs1] - memory_config.translation_offset;

                    if (temp >= memory_config.memory_size) {
                        printf("Fatal: virtual memory address is out of bounds: 0x%lx\n", temp);
                        free(memory_config.vm_memory);
                        return 1;
                    }

                    ((uint8_t *) memory_config.vm_memory)[temp] = (uint8_t) (main_hart.x[s_instr.rs2]);
                    break;

                case 0b001:

                    temp = extend_sign(s_instr.imm, 12) + main_hart.x[s_instr.rs1] - memory_config.translation_offset;

                    if (temp + 1 >= memory_config.memory_size) {
                        printf("Fatal: virtual memory address is out of bounds: 0x%lx\n", temp);
                        free(memory_config.vm_memory);
                        return 1;
                    }

                    ((uint8_t *) memory_config.vm_memory)[temp]     = (uint8_t) (main_hart.x[s_instr.rs2]);
                    ((uint8_t *) memory_config.vm_memory)[temp + 1] = (uint8_t) (main_hart.x[s_instr.rs2] >> 8);
                    break;

                case 0b010:

                    temp = extend_sign(s_instr.imm, 12) + main_hart.x[s_instr.rs1] - memory_config.translation_offset;

                    if (temp + 3 >= memory_config.memory_size) {
                        printf("Fatal: virtual memory address is out of bounds: 0x%lx\n", temp);
                        free(memory_config.vm_memory);
                        return 1;
                    }

                    ((uint8_t *) memory_config.vm_memory)[temp]     = (uint8_t) (main_hart.x[s_instr.rs2]);
                    ((uint8_t *) memory_config.vm_memory)[temp + 1] = (uint8_t) (main_hart.x[s_instr.rs2] >> 8);
                    ((uint8_t *) memory_config.vm_memory)[temp + 2] = (uint8_t) (main_hart.x[s_instr.rs2] >> 16);
                    ((uint8_t *) memory_config.vm_memory)[temp + 3] = (uint8_t) (main_hart.x[s_instr.rs2] >> 24);
                    break;

                case 0b011:

                    temp = extend_sign(s_instr.imm, 12) + main_hart.x[s_instr.rs1] - memory_config.translation_offset;

                    if (temp + 7 >= memory_config.memory_size) {
                        printf("Fatal: virtual memory address is out of bounds: 0x%lx\n", temp);
                        free(memory_config.vm_memory);
                        return 1;
                    }

                    // whatever this is
                    ((uint8_t *) memory_config.vm_memory)[temp]     = (uint8_t) (main_hart.x[s_instr.rs2]);
                    ((uint8_t *) memory_config.vm_memory)[temp + 1] = (uint8_t) (main_hart.x[s_instr.rs2] >> 8);
                    ((uint8_t *) memory_config.vm_memory)[temp + 2] = (uint8_t) (main_hart.x[s_instr.rs2] >> 16);
                    ((uint8_t *) memory_config.vm_memory)[temp + 3] = (uint8_t) (main_hart.x[s_instr.rs2] >> 24);
                    ((uint8_t *) memory_config.vm_memory)[temp + 4] = (uint8_t) (main_hart.x[s_instr.rs2] >> 24);
                    ((uint8_t *) memory_config.vm_memory)[temp + 5] = (uint8_t) (main_hart.x[s_instr.rs2] >> 24);
                    ((uint8_t *) memory_config.vm_memory)[temp + 6] = (uint8_t) (main_hart.x[s_instr.rs2] >> 24);
                    ((uint8_t *) memory_config.vm_memory)[temp + 7] = (uint8_t) (main_hart.x[s_instr.rs2] >> 24);
                    break;
                }
                break;

            /* The jal instrcution, aka unconditional jump, or just function call */
            case 0b11011:
                u_instr = fetch_j(instruction);

                main_hart.pc += extend_sign(u_instr.imm, 20) - 4;
                if (u_instr.rd != 0) {
                    main_hart.x[u_instr.rd] = main_hart.pc + 4;
                }
                break;

            /* The jalr instruction */
            case 0b11001:
                i_instr = fetch_i(instruction);

                temp         = main_hart.pc + 4;
                main_hart.pc = (main_hart.x[i_instr.rs1] + extend_sign(i_instr.imm, 12));

                if (i_instr.rd != 0) {
                    main_hart.x[i_instr.rd] = temp;
                }
                break;

            /* This case is responsible for branching */
            case 0b11000:
                s_instr = fetch_b(instruction);

                switch (s_instr.funct3) {

                /* The beq instructtion */
                case 0b000:
                    if (main_hart.x[s_instr.rs1] == main_hart.x[s_instr.rs2]) {
                        main_hart.pc += extend_sign(s_instr.imm, 12) - 4;
                    }
                    break;

                /* The bne instructtion */
                case 0b001:
                    if (main_hart.x[s_instr.rs1] != main_hart.x[s_instr.rs2]) {
                        main_hart.pc += extend_sign(s_instr.imm, 12) - 4;
                    }
                    break;

                /* The blt instructtion */
                case 0b100:
                    if ((int64_t) (main_hart.x[s_instr.rs1]) < (int64_t) (main_hart.x[s_instr.rs2])) {
                        main_hart.pc += extend_sign(s_instr.imm, 12) - 4;
                    }
                    break;

                /* The bge instructtion */
                case 0b101:
                    if ((int64_t) (main_hart.x[s_instr.rs1]) >= (int64_t) (main_hart.x[s_instr.rs2])) {
                        main_hart.pc += extend_sign(s_instr.imm, 12) - 4;
                    }
                    break;

                /* The bltu instructtion */
                case 0b110:
                    if (main_hart.x[s_instr.rs1] < main_hart.x[s_instr.rs2]) {
                        main_hart.pc += extend_sign(s_instr.imm, 12) - 4;
                    }
                    break;

                /* The bgeu instructtion */
                case 0b111:
                    if (main_hart.x[s_instr.rs1] >= main_hart.x[s_instr.rs2]) {
                        main_hart.pc += extend_sign(s_instr.imm, 12) - 4;
                    }
                    break;
                }
                break;
                // new cases here

            /* instruction for rv32 support */
            /* These are I type*/
            case 0b00110:
                i_instr = fetch_i(instruction);

                switch (i_instr.funct3) {
                case 0b000:
                    main_hart.x[i_instr.rd] =
                        extend_sign((extend_sign(i_instr.imm, 12) + main_hart.x[i_instr.rs1]) & UINT32_MAX, 32);
                    break;

                case 0b001:
                    main_hart.x[i_instr.rd] =
                        extend_sign((main_hart.x[i_instr.rs1] << (i_instr.imm & 0b11111)) & UINT32_MAX, 32);
                    break;

                case 0b101:
                    switch ((i_instr.imm >> 7) & 0b11111) {
                    case 0b00000:

                        main_hart.x[i_instr.rd] =
                            extend_sign((main_hart.x[i_instr.rs1] >> (i_instr.imm & 0b11111)) & UINT32_MAX, 32);
                        break;

                    case 0b01000:

                        main_hart.x[i_instr.rd] = extend_sign(
                            ((int64_t) (main_hart.x[i_instr.rs1]) >> (i_instr.imm & 0b11111)) & UINT32_MAX, 32
                        );
                        break;
                    }
                    break;
                }
                break;

            /* More instrctions for rv32 support */
            /* These are R type*/
            case 0b01110:
                r_instr = fetch_r(instruction);

                switch (r_instr.funct7) {
                case 0b0000000:
                    switch (r_instr.funct3) {
                    case 0b000:
                        main_hart.x[r_instr.rd] =
                            extend_sign((main_hart.x[r_instr.rs1] + main_hart.x[r_instr.rs2]) & UINT32_MAX, 32);
                        break;
                    case 0b001:
                        main_hart.x[r_instr.rd] = extend_sign(
                            (main_hart.x[r_instr.rs1] << (main_hart.x[r_instr.rs2] & 0b11111)) & UINT32_MAX, 32
                        );
                        break;
                    case 0b101:
                        main_hart.x[r_instr.rd] = extend_sign(
                            (main_hart.x[r_instr.rs1] >> (main_hart.x[r_instr.rs2] & 0b11111)) & UINT32_MAX, 32
                        );
                        break;
                    }
                    break;

                case 0b0100000:
                    switch (r_instr.funct3) {
                    case 0b000:
                        main_hart.x[r_instr.rd] =
                            extend_sign((main_hart.x[r_instr.rs1] - main_hart.x[r_instr.rs2]) & UINT32_MAX, 32);
                        break;
                    case 0b101:
                        main_hart.x[r_instr.rd] = extend_sign(
                            ((int32_t) main_hart.x[r_instr.rs1] >> (main_hart.x[r_instr.rs2] & 0b11111)) & UINT32_MAX,
                            32
                        );
                        break;
                    }
                    break;

                case 0b0000001:
                    switch (r_instr.funct3) {

                    /* mulw */
                    case 0b000:
                        main_hart.x[r_instr.rd] = extend_sign(((int32_t)main_hart.x[r_instr.rs1] * (int32_t)main_hart.x[r_instr.rs2]) & UINT32_MAX, 32);
                        break;

                    /* divw */
                    case 0b100:
                        main_hart.x[r_instr.rd] = extend_sign(((int32_t)main_hart.x[r_instr.rs1] / (int32_t)main_hart.x[r_instr.rs2]) & UINT32_MAX, 32);
                        break;

                    /* divuw */
                    case 0b101:
                        main_hart.x[r_instr.rd] = extend_sign(((uint32_t)main_hart.x[r_instr.rs1] / (uint32_t)main_hart.x[r_instr.rs2]) & UINT32_MAX, 32);
                        break;

                    /* remw */
                    case 0b110:
                        main_hart.x[r_instr.rd] = extend_sign(((int32_t)main_hart.x[r_instr.rs1] % (int32_t)main_hart.x[r_instr.rs2]) & UINT32_MAX, 32);
                        break;

                    /* remuw */
                    case 0b111:
                        main_hart.x[r_instr.rd] = extend_sign(((uint32_t)main_hart.x[r_instr.rs1] % (uint32_t)main_hart.x[r_instr.rs2]) & UINT32_MAX, 32);
                        break;
                    }
                    break;
                }
                break;

            default:
                free(memory_config.vm_memory);
                printf("Fatal: 16-bit instructions are not supported right now\n");
                return 1;
            }
        }

        if (flags.debug) {
            printf("opcode: %x\n", opcode);
            debug_fn(main_hart);
        }

        main_hart.pc += 4;
    }

    free(memory_config.vm_memory);
    return 0;
}
