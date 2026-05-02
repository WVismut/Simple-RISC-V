# The Simple RISC-V Emulator
## Description
You are looking at a very simple RISC-V emulator. It's not something you want to use in production (yet), but it may be interesting for you to look at its source code! It's a fully educational project, so no AI was used during development.
## How to use it?
Pass a 64 bit elf executable to the emulator, and it will try to handle it. Use `-n` option to specify the elf's file name. Use `-d` option to launch emulator in debug mode. Right now there are only two commands: `step N` and `reg xN`/`reg pc`, but in the future i will add more!
## How to build it?
Just use `clang` or `gcc`. Also, you can use the provided script to build the emulator. But be carefull: only use compilers that support `__int128_t` type!
## How to build assembly code to test the emulator?
I personally use the `compileasm.sh` script. It uses `clang` to compile RISC-V RV64M ASM code into an `ELF` executable. But you can always use another tool if you want!
