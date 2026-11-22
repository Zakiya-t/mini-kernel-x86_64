CC = gcc
NASM = nasm

CFLAGS = -Wall -Wextra -std=gnu11 -m64 -ffreestanding -fno-builtin -fno-stack-protector \
         -mno-red-zone -no-pie -O2 -Iinclude -Ilibc/include -nostdinc

NASMFLAGS = -f elf64
LDFLAGS = -nostdlib -no-pie -Wl,-T,linker.ld -Wl,--build-id=none

TARGET = myshell.elf

# ── Libc Sources ─────────────────────────────────────────────────────────────
LIBC_C_SRCS = libc/src/syscall.c \
              libc/src/string.c \
              libc/src/stdlib.c \
              libc/src/stdio.c \
              libc/src/unistd.c \
              libc/src/dirent.c \
              libc/src/stat.c \
              libc/src/term.c

CRT0_OBJ = libc/src/crt0.o

# ── Shell Sources ────────────────────────────────────────────────────────────
SHELL_SRCS = src/main.c \
             src/shell.c \
             src/ui.c \
             src/lexer.c \
             src/parser.c \
             src/executor.c \
             src/builtins/builtin_core.c \
             src/builtins/builtin_ls.c \
             src/builtins/builtin_cat.c \
             src/builtins/builtin_fileops.c \
             src/builtins/builtin_sysinfo.c \
             src/builtins/builtin_lscpu.c \
             src/builtins/builtin_top.c \
             src/builtins/builtin_nano.c

OBJS = $(CRT0_OBJ) $(LIBC_C_SRCS:.c=.o) $(SHELL_SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS) linker.ld
	$(CC) $(LDFLAGS) -o $(TARGET) $(OBJS)
	@echo "=========================================================="
	@echo " [SUCCESS] Independent MyShell ELF binary built: $@"
	@readelf -h $(TARGET) | grep -E "Class|Machine|Entry"
	@echo "=========================================================="

libc/src/%.o: libc/src/%.asm
	$(NASM) $(NASMFLAGS) $< -o $@

libc/src/%.o: libc/src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

src/builtins/%.o: src/builtins/%.c
	$(CC) $(CFLAGS) -c $< -o $@

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
