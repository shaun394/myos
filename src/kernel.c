// src/kernel.c
#include <stdint.h>
#include <stddef.h>

/* -------------------- Port I/O -------------------- */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* -------------------- Serial (COM1) -------------------- */
#define COM1 0x3F8

static void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

static void serial_write_char(char c) {
    while ((inb(COM1 + 5) & 0x20) == 0) { }
    outb(COM1, (uint8_t)c);
}

static void serial_write(const char* s) {
    for (size_t i = 0; s[i]; i++) {
        if (s[i] == '\n') serial_write_char('\r');
        serial_write_char(s[i]);
    }
}

/* -------------------- VGA text mode (80x25) -------------------- */
static volatile uint16_t* const VGA = (uint16_t*)0xB8000;

static size_t vga_row = 0;
static size_t vga_col = 0;

/* color: high nibble background, low nibble foreground */
static uint8_t vga_color = 0x0F; // white on black

static inline uint16_t vga_entry(char ch, uint8_t color) {
    return (uint16_t)ch | ((uint16_t)color << 8);
}

static void vga_set_color(uint8_t fg, uint8_t bg) {
    vga_color = (uint8_t)((bg << 4) | (fg & 0x0F));
}

static void vga_clear(uint8_t fg, uint8_t bg) {
    vga_set_color(fg, bg);
    for (size_t r = 0; r < 25; r++) {
        for (size_t c = 0; c < 80; c++) {
            VGA[r * 80 + c] = vga_entry(' ', vga_color);
        }
    }
    vga_row = 0;
    vga_col = 0;
}

static void vga_putc(char ch) {
    if (ch == '\n') {
        vga_col = 0;
        vga_row++;
        if (vga_row >= 25) vga_row = 0; // wrap for now
        return;
    }

    VGA[vga_row * 80 + vga_col] = vga_entry(ch, vga_color);

    vga_col++;
    if (vga_col >= 80) {
        vga_col = 0;
        vga_row++;
        if (vga_row >= 25) vga_row = 0;
    }
}

static void vga_write(const char* s) {
    for (size_t i = 0; s[i]; i++) vga_putc(s[i]);
}

/* VGA-only write with explicit fg/bg (guarantees color per line) */
static void vga_write_color(uint8_t fg, uint8_t bg, const char* s) {
    vga_set_color(fg, bg);
    vga_write(s);
}

/* -------------------- Kernel entry -------------------- */
void kmain(void) {
    serial_init();
    serial_write("KERNEL: entered kmain\n");

    /* Clear screen first so you can see color changes cleanly */
    vga_clear(/*fg*/0x0F, /*bg*/0x00);

    /* Hard-proof colored cells at top-left */
    VGA[0] = vga_entry('R', (uint8_t)((0x4 << 4) | 0xF)); // white on red bg
    VGA[1] = vga_entry('G', (uint8_t)((0xA << 4) | 0x0)); // black on light-green bg
    VGA[2] = vga_entry('B', (uint8_t)((0x1 << 4) | 0xE)); // yellow on blue bg

    vga_row = 2;
    vga_col = 2;

    vga_write_color(0x0A, 0x00, "GREEN on BLACK\n");
    vga_write_color(0x0E, 0x00, "YELLOW on BLACK\n");
    vga_write_color(0x0F, 0x01, "WHITE on BLUE (background should be blue)\n");
    vga_write_color(0x0C, 0x00, "RED on BLACK\n");

    /* Keep serial alive for debugging */
    serial_write("KERNEL: VGA color demo drawn\n");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
