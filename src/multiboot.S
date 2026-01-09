/* src/multiboot.S - Multiboot2 header ONLY */
.set MB2_MAGIC, 0xE85250D6
.set MB2_ARCH,  0
.set MB2_LEN,   header_end - header_start
.set MB2_CHKSUM, -(MB2_MAGIC + MB2_ARCH + MB2_LEN)

.section .multiboot
.align 8

header_start:
    .long MB2_MAGIC
    .long MB2_ARCH
    .long MB2_LEN
    .long MB2_CHKSUM

    /* end tag */
    .align 8
    .long 0
    .long 8

header_end:
