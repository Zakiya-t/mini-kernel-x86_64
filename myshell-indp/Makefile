CC = gcc
NASM = nasm

CFLAGS = -Wall -Wextra -std=gnu11 -m64 -ffreestanding -fno-builtin -fno-stack-protector \
         -mno-red-zone -no-pie -O2 -Iinclude -Ilibc/include -nostdinc

NASMFLAGS = -f elf64
LDFLAGS = -nostdlib -no-pie -Wl,-T,linker.ld -Wl,--build-id=none

BUILD_DIR = build
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

CRT0_SRC = libc/src/crt0.asm

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

CRT0_OBJ = $(BUILD_DIR)/$(CRT0_SRC:.asm=.o)
LIBC_OBJS = $(patsubst %.c, $(BUILD_DIR)/%.o, $(LIBC_C_SRCS))
SHELL_OBJS = $(patsubst %.c, $(BUILD_DIR)/%.o, $(SHELL_SRCS))
OBJS = $(CRT0_OBJ) $(LIBC_OBJS) $(SHELL_OBJS)

all: $(TARGET)

$(TARGET): $(OBJS) linker.ld
	$(CC) $(LDFLAGS) -o $(TARGET) $(OBJS)
	@echo "=========================================================="
	@echo " [SUCCESS] Independent MyShell ELF binary built: $@"
	@readelf -h $(TARGET) | grep -E "Class|Machine|Entry"
	@echo "=========================================================="

$(BUILD_DIR)/%.o: %.asm
	@mkdir -p $(dir $@)
	$(NASM) $(NASMFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) libc/src/*.o src/*.o src/builtins/*.o $(TARGET)

.PHONY: all clean
