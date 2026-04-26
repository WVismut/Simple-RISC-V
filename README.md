# The Simple RISC-V Emulator
## Description
You are looking at a very simple RISC-V emulator. It's not something you want to use in production (yet), but it may be interesting for you to look at its source code! It's a fully educational project, so no AI was used during development.
## How to use it?
Right now this emulator can only work with a raw binary `.text` section. You can obtain it by running this command on an ELF object or executable file: `riscv64-linux-gnu-objcopy -O binary -j .text input.o output.bin`. Right now I'm working on parsing ELF headers, so soon you will be able to run actual RISC-V programs, not just the `.text` section :D
## How to build it?
Just use `clang` or `gcc`. Also, you can use the provided script to build the emulator.