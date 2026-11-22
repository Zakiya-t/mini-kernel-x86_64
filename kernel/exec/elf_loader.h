#ifndef _KERNEL_EXEC_ELF_LOADER_H
#define _KERNEL_EXEC_ELF_LOADER_H

#include "types.h"
#include "elf.h"
#include "process/process.h"

/* ── ELF Loader Return Codes ─────────────────────────────────────────────── */
#define ELF_OK              0
#define ELF_ERR_NOT_FOUND  -1   /* File not on MiniFS disk                  */
#define ELF_ERR_TOO_LARGE  -2   /* ELF file too big to load                 */
#define ELF_ERR_BAD_MAGIC  -3   /* Not a valid ELF file (bad magic bytes)   */
#define ELF_ERR_NOT_64BIT  -4   /* Not a 64-bit ELF                         */
#define ELF_ERR_NOT_EXEC   -5   /* Not an executable (is .o or .so)         */
#define ELF_ERR_NOT_X86    -6   /* Wrong architecture (not x86-64)          */
#define ELF_ERR_NO_LOAD    -7   /* No PT_LOAD segments found                */
#define ELF_ERR_NO_MEM     -8   /* Physical memory allocation failed        */
#define ELF_ERR_BAD_ADDR   -9   /* Segment virtual address is invalid       */
#define ELF_ERR_READ       -10  /* Disk read error                          */

/* Maximum ELF file size we'll attempt to load (4 MiB) */
#define ELF_MAX_FILE_SIZE   (4 * 1024 * 1024)

/* Virtual base for user ELF programs (above kernel binary, 16 MiB mark) */
#define ELF_USER_LOAD_BASE  0x0000000001000000ULL

/*
 * elf_load() — Load an ELF64 executable from MiniFS into the calling process.
 *
 * Reads the ELF from disk path, validates its headers, allocates physical RAM
 * frames for every PT_LOAD segment, maps them into the process's address space
 * via the VMM, copies segment bytes from disk, and updates the interrupt frame
 * so that when the syscall returns, execution continues at the ELF entry point
 * in Ring 3 (user mode).
 *
 * Parameters:
 *   path  — MiniFS path to ELF binary (e.g. "/hello", "/programs/init")
 *   proc  — PCB of the process that will execute the ELF (usually current)
 *   frame — Saved interrupt frame (modified in place: RIP, RSP, CS, SS, RFLAGS)
 *
 * Returns:
 *   ELF_OK (0) on success, negative ELF_ERR_* code on failure.
 */
int elf_load(const char *path, process_t *proc, interrupt_frame_t *frame, int argc, const char *const argv[]);

/*
 * elf_error_string() — Human-readable string for an ELF_ERR_* code.
 */
const char *elf_error_string(int err);

#endif /* _KERNEL_EXEC_ELF_LOADER_H */

