#ifndef _ANSI_COLORS_H
#define _ANSI_COLORS_H

/* ANSI Escape Codes for Text Formatting & Colors */
#define ANSI_RESET        "\033[0m"
#define ANSI_BOLD         "\033[1m"
#define ANSI_DIM          "\033[2m"
#define ANSI_UNDERLINE    "\033[4m"

/* Regular Foreground Colors */
#define ANSI_BLACK        "\033[30m"
#define ANSI_RED          "\033[31m"
#define ANSI_GREEN        "\033[32m"
#define ANSI_YELLOW       "\033[33m"
#define ANSI_BLUE         "\033[34m"
#define ANSI_MAGENTA      "\033[35m"
#define ANSI_CYAN         "\033[36m"
#define ANSI_WHITE        "\033[37m"

/* High-Intensity / Bright Foreground Colors */
#define ANSI_GRAY         "\033[90m"
#define ANSI_BRED         "\033[1;31m"
#define ANSI_BGREEN       "\033[1;32m"
#define ANSI_BYELLOW      "\033[1;33m"
#define ANSI_BBLUE        "\033[1;34m"
#define ANSI_BMAGENTA     "\033[1;35m"
#define ANSI_BCYAN        "\033[1;36m"
#define ANSI_BWHITE       "\033[1;37m"

/* Background Colors */
#define ANSI_BG_BLACK     "\033[40m"
#define ANSI_BG_BLUE      "\033[44m"
#define ANSI_BG_DARKGRAY  "\033[100m"

#endif /* _ANSI_COLORS_H */
