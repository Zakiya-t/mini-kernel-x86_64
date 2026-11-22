#include "arch/x86_64/vga.h"
#include "arch/x86_64/io.h"

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_BUFFER ((volatile uint16_t *)0xB8000)

static size_t vga_row;
static size_t vga_column;
static uint8_t vga_color_attr;

static int  ansi_state = 0;
static int  ansi_params[4] = {0};
static int  ansi_param_idx = 0;
static bool ansi_bold = false;

static void vga_apply_ansi_sgr(void) {
    enum vga_color fg = (enum vga_color)(vga_color_attr & 0x0F);
    enum vga_color bg = (enum vga_color)((vga_color_attr >> 4) & 0x0F);

    for (int i = 0; i <= ansi_param_idx; i++) {
        int code = ansi_params[i];
        if (code == 0) {
            ansi_bold = false;
            fg = VGA_COLOR_LIGHT_GREY;
            bg = VGA_COLOR_BLACK;
        } else if (code == 1) {
            ansi_bold = true;
        } else if (code >= 30 && code <= 37) {
            switch (code) {
                case 30: fg = ansi_bold ? VGA_COLOR_DARK_GREY : VGA_COLOR_BLACK; break;
                case 31: fg = ansi_bold ? VGA_COLOR_LIGHT_RED : VGA_COLOR_RED; break;
                case 32: fg = ansi_bold ? VGA_COLOR_LIGHT_GREEN : VGA_COLOR_GREEN; break;
                case 33: fg = ansi_bold ? VGA_COLOR_LIGHT_BROWN : VGA_COLOR_BROWN; break;
                case 34: fg = ansi_bold ? VGA_COLOR_LIGHT_BLUE : VGA_COLOR_BLUE; break;
                case 35: fg = ansi_bold ? VGA_COLOR_LIGHT_MAGENTA : VGA_COLOR_MAGENTA; break;
                case 36: fg = ansi_bold ? VGA_COLOR_LIGHT_CYAN : VGA_COLOR_CYAN; break;
                case 37: fg = ansi_bold ? VGA_COLOR_WHITE : VGA_COLOR_LIGHT_GREY; break;
            }
        } else if (code >= 90 && code <= 97) {
            switch (code) {
                case 90: fg = VGA_COLOR_DARK_GREY; break;
                case 91: fg = VGA_COLOR_LIGHT_RED; break;
                case 92: fg = VGA_COLOR_LIGHT_GREEN; break;
                case 93: fg = VGA_COLOR_LIGHT_BROWN; break;
                case 94: fg = VGA_COLOR_LIGHT_BLUE; break;
                case 95: fg = VGA_COLOR_LIGHT_MAGENTA; break;
                case 96: fg = VGA_COLOR_LIGHT_CYAN; break;
                case 97: fg = VGA_COLOR_WHITE; break;
            }
        } else if (code >= 40 && code <= 47) {
            switch (code) {
                case 40: bg = VGA_COLOR_BLACK; break;
                case 41: bg = VGA_COLOR_RED; break;
                case 42: bg = VGA_COLOR_GREEN; break;
                case 43: bg = VGA_COLOR_BROWN; break;
                case 44: bg = VGA_COLOR_BLUE; break;
                case 45: bg = VGA_COLOR_MAGENTA; break;
                case 46: bg = VGA_COLOR_CYAN; break;
                case 47: bg = VGA_COLOR_LIGHT_GREY; break;
            }
        }
    }
    vga_color_attr = vga_entry_color(fg, bg);
}

static void vga_update_cursor(size_t x, size_t y) {
    uint16_t pos = (uint16_t)(y * VGA_WIDTH + x);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void vga_init(void) {
    vga_row = 0;
    vga_column = 0;
    vga_color_attr = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_clear();
}

void vga_clear(void) {
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            VGA_BUFFER[index] = vga_entry(' ', vga_color_attr);
        }
    }
    vga_row = 0;
    vga_column = 0;
    vga_update_cursor(vga_column, vga_row);
}

void vga_set_color(enum vga_color fg, enum vga_color bg) {
    vga_color_attr = vga_entry_color(fg, bg);
}

static void vga_scroll(void) {
    for (size_t y = 0; y < VGA_HEIGHT - 1; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            VGA_BUFFER[y * VGA_WIDTH + x] = VGA_BUFFER[(y + 1) * VGA_WIDTH + x];
        }
    }
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        VGA_BUFFER[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', vga_color_attr);
    }
    vga_row = VGA_HEIGHT - 1;
}

void vga_putchar(char c) {
    /* ANSI Escape Sequence State Machine */
    if (ansi_state == 0) {
        if ((unsigned char)c == 0x1B) { // ESC
            ansi_state = 1;
            return;
        }
    } else if (ansi_state == 1) {
        if (c == '[') {
            ansi_state = 2;
            for (int i = 0; i < 4; i++) ansi_params[i] = 0;
            ansi_param_idx = 0;
            return;
        } else {
            ansi_state = 0;
        }
    } else if (ansi_state == 2) {
        if (c >= '0' && c <= '9') {
            ansi_params[ansi_param_idx] = ansi_params[ansi_param_idx] * 10 + (c - '0');
            return;
        } else if (c == ';') {
            if (ansi_param_idx < 3) ansi_param_idx++;
            return;
        } else if (c == 'm') {
            vga_apply_ansi_sgr();
            ansi_state = 0;
            return;
        } else if (c == 'J' || c == 'H') {
            if (c == 'J') vga_clear();
            else if (c == 'H') { vga_row = 0; vga_column = 0; vga_update_cursor(0, 0); }
            ansi_state = 0;
            return;
        } else {
            ansi_state = 0;
            return;
        }
    }
    if (c == '\n') {
        vga_column = 0;
        if (++vga_row >= VGA_HEIGHT) {
            vga_scroll();
        }
    } else if (c == '\r') {
        vga_column = 0;
    } else if (c == '\t') {
        vga_column = (vga_column + 4) & ~3;
        if (vga_column >= VGA_WIDTH) {
            vga_column = 0;
            if (++vga_row >= VGA_HEIGHT) {
                vga_scroll();
            }
        }
    } else if (c == '\b') {
        if (vga_column > 0) {
            vga_column--;
            const size_t index = vga_row * VGA_WIDTH + vga_column;
            VGA_BUFFER[index] = vga_entry(' ', vga_color_attr);
        }
    } else {
        const size_t index = vga_row * VGA_WIDTH + vga_column;
        VGA_BUFFER[index] = vga_entry((unsigned char)c, vga_color_attr);
        if (++vga_column >= VGA_WIDTH) {
            vga_column = 0;
            if (++vga_row >= VGA_HEIGHT) {
                vga_scroll();
            }
        }
    }
    vga_update_cursor(vga_column, vga_row);
}

void vga_write(const char *data) {
    if (!data) return;
    while (*data) {
        vga_putchar(*data++);
    }
}

void vga_write_len(const char *data, size_t size) {
    if (!data) return;
    for (size_t i = 0; i < size; i++) {
        vga_putchar(data[i]);
    }
}
