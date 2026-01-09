cmake_minimum_required(VERSION 3.20)
project(myos C ASM)

set(CMAKE_C_STANDARD 11)
enable_language(ASM)

add_executable(kernel.elf
        src/multiboot.S
        src/boot.S
        src/kernel.c
)

target_compile_options(kernel.elf PRIVATE
        -ffreestanding
        -fno-pic
        -fno-pie
        -fno-stack-protector
        -m64
        -Wall -Wextra
)

target_link_options(kernel.elf PRIVATE
        -T ${CMAKE_SOURCE_DIR}/linker.ld
        -nostdlib
        -no-pie
        -Wl,--build-id=none
        -m64
)
