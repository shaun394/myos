/* src/boot.S - Multiboot2 32-bit entry -> long mode -> kmain */

.set COM1, 0x3F8

.section .text
.global _start
.extern kmain

/* GRUB MB2 enters in 32-bit protected mode */
.code32
_start:
    cli
    mov $stack_top, %esp

    call serial_init_32
    mov $msg32, %esi
    call serial_write_32

    /* Load GDT */
    lgdt gdt_desc

    /* Setup CR3 to PML4 */
    mov $pml4, %eax
    mov %eax, %cr3

    /* Enable PAE */
    mov %cr4, %eax
    or  $(1<<5), %eax
    mov %eax, %cr4

    /* Enable long mode (EFER.LME) */
    mov $0xC0000080, %ecx
    rdmsr
    or  $(1<<8), %eax
    wrmsr

    /* Enable paging */
    mov %cr0, %eax
    or  $(1<<31), %eax
    mov %eax, %cr0

    /* Far jump into 64-bit code segment */
    ljmp $0x08, $long_mode_entry

/* ---------------- 64-bit ---------------- */
.code64
long_mode_entry:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss

    lea stack_top(%rip), %rsp

    call serial_init_64
    lea msg64(%rip), %rsi
    call serial_write_64

    call kmain

.hang:
    hlt
    jmp .hang


/************** Serial helpers **************/
.code32
serial_init_32:
    mov $COM1+1, %dx
    mov $0x00, %al
    outb %al, %dx
    mov $COM1+3, %dx
    mov $0x80, %al
    outb %al, %dx
    mov $COM1+0, %dx
    mov $0x03, %al
    outb %al, %dx
    mov $COM1+1, %dx
    mov $0x00, %al
    outb %al, %dx
    mov $COM1+3, %dx
    mov $0x03, %al
    outb %al, %dx
    mov $COM1+2, %dx
    mov $0xC7, %al
    outb %al, %dx
    mov $COM1+4, %dx
    mov $0x0B, %al
    outb %al, %dx
    ret

serial_write_32:
1:
    lodsb
    test %al, %al
    jz 2f
    mov $COM1, %dx
    outb %al, %dx
    jmp 1b
2:
    ret

.code64
serial_init_64:
    mov $COM1+1, %dx
    mov $0x00, %al
    outb %al, %dx
    mov $COM1+3, %dx
    mov $0x80, %al
    outb %al, %dx
    mov $COM1+0, %dx
    mov $0x03, %al
    outb %al, %dx
    mov $COM1+1, %dx
    mov $0x00, %al
    outb %al, %dx
    mov $COM1+3, %dx
    mov $0x03, %al
    outb %al, %dx
    mov $COM1+2, %dx
    mov $0xC7, %al
    outb %al, %dx
    mov $COM1+4, %dx
    mov $0x0B, %al
    outb %al, %dx
    ret

serial_write_64:
1:
    lodsb
    test %al, %al
    jz 2f
    mov $COM1, %dx
    outb %al, %dx
    jmp 1b
2:
    ret


/************** GDT **************/
.section .rodata
.align 8
gdt:
    .quad 0x0000000000000000
    .quad 0x00AF9A000000FFFF      /* 0x08: 64-bit code */
    .quad 0x00AF92000000FFFF      /* 0x10: data */

gdt_desc:
    .word (gdt_end - gdt - 1)
    .long gdt
gdt_end:


/************** Paging (in .data, not .bss) **************/
.section .data
.align 4096
pml4:
    .quad (pdpt + 0x003)
    .fill 511, 8, 0

.align 4096
pdpt:
    .quad (pd + 0x003)
    .fill 511, 8, 0

.align 4096
pd:
    .quad (0x00000000 + 0x083)    /* 2MiB page: P|RW|PS */
    .fill 511, 8, 0


/************** Stack **************/
.section .bss
.align 16
stack_bottom:
    .skip 16384
stack_top:

.section .rodata
msg32: .asciz "BOOT: reached 32-bit _start\n"
msg64: .asciz "BOOT: reached 64-bit long mode\n"
