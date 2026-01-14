// src/kernel.c

#include <stdint.h>
#include <stddef.h>

/* ============================================================
 * SYSTEM / BUILD INFO
 * ============================================================ */
#define MYOS_NAME       "myos"
#define MYOS_VERSION    "v0.1.4.3"
#define MYOS_ARCH       "x86_64"
// #define MYOS_CPU
// #define MYOS_RAM
#define MYOS_KEYBOARD   "ps2 set1"
#define MYOS_PLATFORM   "qemu-pc"


/* ============================================================
 * LOW-LEVEL PORT I/O
 * ============================================================ */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void cpu_relax(void) { __asm__ volatile("pause"); }

/* ============================================================
 * SERIAL (COM1) - mirrors terminal output to host
 * ============================================================ */
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
static void serial_hex_nibble(uint8_t n) {
    n &= 0xF;
    serial_write_char(n < 10 ? ('0' + n) : ('A' + (n - 10)));
}
static void serial_hex8(uint8_t v) {
    serial_hex_nibble(v >> 4);
    serial_hex_nibble(v);
}

/* ============================================================
 * VGA TEXT MODE (80x25) - primary console
 * ============================================================ */
static volatile uint16_t* const VGA = (uint16_t*)0xB8000;
static size_t vga_row = 0, vga_col = 0;
static uint8_t vga_color = 0x0F;

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
        vga_row = (vga_row + 1) % 25;
        return;
    }
    VGA[vga_row * 80 + vga_col] = vga_entry(ch, vga_color);
    if (++vga_col >= 80) {
        vga_col = 0;
        vga_row = (vga_row + 1) % 25;
    }
}
static void vga_write(const char* s) {
    for (size_t i = 0; s[i]; i++) vga_putc(s[i]);
}
static void vga_backspace(void) {
    if (vga_col == 0) {
        if (vga_row == 0) return;
        vga_row--;
        vga_col = 79;
    } else {
        vga_col--;
    }
    VGA[vga_row * 80 + vga_col] = vga_entry(' ', vga_color);
}

/* ============================================================
 * TINY STRING HELPERS (no libc)
 * ============================================================ */
static int streq(const char* a, const char* b) {
    size_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == 0 && b[i] == 0;
}
static int starts_with(const char* s, const char* prefix) {
    size_t i = 0;
    while (prefix[i]) {
        if (s[i] != prefix[i]) return 0;
        i++;
    }
    return 1;
}

/* ============================================================
 * i8042 CONTROLLER + PS/2 BYTE READ (polling)
 * ============================================================ */
#define KBD_DATA   0x60
#define KBD_STATUS 0x64
#define KBD_CMD    0x64

static void i8042_wait_input_clear(void) {
    for (int i = 0; i < 300000; i++) {
        if (!(inb(KBD_STATUS) & 0x02)) return;
        cpu_relax();
    }
}
static void i8042_write_cmd(uint8_t cmd) {
    i8042_wait_input_clear();
    outb(KBD_CMD, cmd);
}
static void i8042_write_data(uint8_t data) {
    i8042_wait_input_clear();
    outb(KBD_DATA, data);
}
static uint8_t i8042_read_data_wait(void) {
    for (int i = 0; i < 300000; i++) {
        if (inb(KBD_STATUS) & 0x01) return inb(KBD_DATA);
        cpu_relax();
    }
    return 0x00;
}
static void i8042_flush(void) {
    for (int i = 0; i < 64; i++) {
        uint8_t st = inb(KBD_STATUS);
        if (!(st & 0x01)) break;
        (void)inb(KBD_DATA);
    }
}

static int kbd_read_byte(uint8_t* out) {
    uint8_t st = inb(KBD_STATUS);
    if (!(st & 0x01)) return 0;
    uint8_t b = inb(KBD_DATA);
    *out = b;
    return (st & 0x20) ? 2 : 1;
}

static void kbd_send_cmd(uint8_t cmd) {
    i8042_write_data(cmd);
    for (int i = 0; i < 200000; i++) {
        uint8_t b;
        int kind = kbd_read_byte(&b);
        if (kind == 1 && b == 0xFA) return;
        cpu_relax();
    }
}

/* ============================================================
 * KEYBOARD INIT (Set 1 via translation)
 * ============================================================ */
static void keyboard_init_hard_set1(void) {
    serial_write("KB: hard init i8042\n");

    i8042_write_cmd(0xAD);
    i8042_write_cmd(0xA7);
    i8042_flush();

    // Read command byte
    i8042_write_cmd(0x20);
    uint8_t cb = i8042_read_data_wait();
    serial_write("KB: cmdbyte before = 0x"); serial_hex8(cb); serial_write("\n");

    cb &= ~(1u << 4);
    cb |=  (1u << 5);
    cb |=  (1u << 6);

    i8042_write_cmd(0x60);
    i8042_write_data(cb);

    i8042_write_cmd(0xAE);

    serial_write("KB: send 0xF4 (enable scanning)\n");
    kbd_send_cmd(0xF4);

    i8042_write_cmd(0x20);
    uint8_t cb2 = i8042_read_data_wait();
    serial_write("KB: cmdbyte after  = 0x"); serial_hex8(cb2); serial_write("\n");
}

/* ============================================================
 * TERMINAL (line buffer + prompt + basic commands)
 * ============================================================ */
#define LINE_MAX 128

static char   line_buf[LINE_MAX];
static size_t line_len = 0;

static void term_prompt(void) {
    vga_set_color(0x0A, 0x00);
    vga_write("myos> ");
    serial_write("myos> ");
}

static void term_backspace(void) {
    if (line_len == 0) return;
    line_len--;
    line_buf[line_len] = 0;

    vga_backspace();
    serial_write("\b \b");
}

static void term_put_char(char c) {
    if (line_len + 1 >= LINE_MAX) return;
    line_buf[line_len++] = c;
    line_buf[line_len] = 0;

    serial_write_char(c);
    vga_putc(c);
}

static void term_clear_screen(void) {
    vga_clear(0x0F, 0x00);
}

static void term_println(const char* s) {
    vga_set_color(0x0F, 0x00);
    vga_write(s);
    vga_putc('\n');

    serial_write(s);
    serial_write("\n");
}

static void term_execute_line(const char* line) {
    if (line[0] == 0) return;

    if (streq(line, "help")) {
        vga_set_color(0x0F, 0x00);
        vga_write("Commands:\n");
        vga_write("  help        - show this help\n");
        vga_write("  clear       - clear the screen\n");
        vga_write("  echo <text> - print text\n");
        vga_write("  uname       - show system name and architecture\n");
        vga_write("  version     - show kernel version and build info\n");
        vga_write("  reboot      - reboot (QEMU)\n");
        // (uname/version will be added next)

        serial_write("Commands:\n");
        serial_write("  help        - show this help\n");
        serial_write("  clear       - clear the screen\n");
        serial_write("  echo <text> - print text\n");
        serial_write("  uname       - show system name and architecture\n");
        serial_write("  version     - show kernel version and build info\n");
        serial_write("  reboot      - reboot (QEMU)\n");
        return;
    }

    if (streq(line, "clear")) {
        term_clear_screen();
        return;
    }

    if (starts_with(line, "echo ")) {
        const char* msg = line + 5;
        vga_set_color(0x0F, 0x00);
        vga_write(msg);
        vga_putc('\n');

        serial_write(msg);
        serial_write("\n");
        return;
    }

    if (streq(line, "uname")) {
        term_println(MYOS_NAME " " MYOS_ARCH);
        return;
    }

    if (streq(line, "version")) {
        term_println(
            MYOS_NAME " " MYOS_VERSION
            " (" MYOS_ARCH ", " MYOS_KEYBOARD ", " MYOS_PLATFORM ")"
            );
        return;
    }

    if (streq(line, "reboot")) {
        serial_write("rebooting...\n");
        outb(0x64, 0xFE);
        for (;;) cpu_relax();
    }

    // Unknown command
    vga_set_color(0x0C, 0x00);
    vga_write("Unknown command: ");
    vga_set_color(0x0F, 0x00);
    vga_write(line);
    vga_putc('\n');

    serial_write("Unknown command: ");
    serial_write(line);
    serial_write("\n");
}

static void term_submit_line(void) {
    serial_write("\n");
    vga_putc('\n');

    term_execute_line(line_buf);

    line_len = 0;
    line_buf[0] = 0;

    term_prompt();
}

/* ============================================================
 * KEYBOARD (PS/2 Set 1 decoding)
 * ============================================================ */
static int shift_down = 0;

static char scancode_set1_to_ascii(uint8_t sc) {
    switch (sc) {
        case 0x1E: return shift_down ? 'A' : 'a';
        case 0x30: return shift_down ? 'B' : 'b';
        case 0x2E: return shift_down ? 'C' : 'c';
        case 0x20: return shift_down ? 'D' : 'd';
        case 0x12: return shift_down ? 'E' : 'e';
        case 0x21: return shift_down ? 'F' : 'f';
        case 0x22: return shift_down ? 'G' : 'g';
        case 0x23: return shift_down ? 'H' : 'h';
        case 0x17: return shift_down ? 'I' : 'i';
        case 0x24: return shift_down ? 'J' : 'j';
        case 0x25: return shift_down ? 'K' : 'k';
        case 0x26: return shift_down ? 'L' : 'l';
        case 0x32: return shift_down ? 'M' : 'm';
        case 0x31: return shift_down ? 'N' : 'n';
        case 0x18: return shift_down ? 'O' : 'o';
        case 0x19: return shift_down ? 'P' : 'p';
        case 0x10: return shift_down ? 'Q' : 'q';
        case 0x13: return shift_down ? 'R' : 'r';
        case 0x1F: return shift_down ? 'S' : 's';
        case 0x14: return shift_down ? 'T' : 't';
        case 0x16: return shift_down ? 'U' : 'u';
        case 0x2F: return shift_down ? 'V' : 'v';
        case 0x11: return shift_down ? 'W' : 'w';
        case 0x2D: return shift_down ? 'X' : 'x';
        case 0x15: return shift_down ? 'Y' : 'y';
        case 0x2C: return shift_down ? 'Z' : 'z';

        case 0x39: return ' ';    // space
        case 0x1C: return '\n';   // enter
        case 0x0E: return '\b';   // backspace
        default:   return 0;
    }
}

static void kbd_handle_set1(uint8_t byte) {
    uint8_t sc = byte;
    int is_break = (sc & 0x80) != 0;
    uint8_t make = (uint8_t)(sc & 0x7F);

    if (make == 0x2A || make == 0x36) {
        shift_down = !is_break;
        return;
    }

    if (is_break) return;

    char ch = scancode_set1_to_ascii(make);
    if (!ch) return;

    if (ch == '\b') { term_backspace(); return; }
    if (ch == '\n') { term_submit_line(); return; }

    term_put_char(ch);
}

/* ============================================================
 * FUTURE FEATURES (placeholders, kept in “safe” order)
 * ============================================================ */
/*
 * 1) Terminal upgrades:
 *    - scrolling instead of wrap
 *    - command history (Up/Down)
 *    - cursor movement (Left/Right)
 *    - command table + autocomplete
 *
 * 2) Interrupts:
 *    - IDT + PIC remap + IRQ1 keyboard ISR
 *    - ring buffer for scancodes
 *    - use HLT in idle loop
 *
 * 3) Memory:
 *    - multiboot2 memory map parsing
 *    - bump allocator / kmalloc
 */

/* ============================================================
 * KERNEL ENTRY
 * ============================================================ */
void kmain(void) {
    serial_init();
    serial_write("KERNEL: entered kmain\n");

    vga_clear(0x0F, 0x00);
    term_prompt();

    keyboard_init_hard_set1();
    serial_write("KB: ready (Set1 translation)\n\n");

    for (;;) {
        uint8_t b;
        int kind = kbd_read_byte(&b);
        if (!kind) { cpu_relax(); continue; }
        if (kind == 2) continue;

        kbd_handle_set1(b);
    }
}
