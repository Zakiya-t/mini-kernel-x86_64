#ifndef _CORE_MULTIBOOT_H
#define _CORE_MULTIBOOT_H

#include "types.h"

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

#define MULTIBOOT_INFO_MEMORY      0x00000001
#define MULTIBOOT_INFO_BOOTDEV     0x00000002
#define MULTIBOOT_INFO_CMDLINE     0x00000004
#define MULTIBOOT_INFO_MODS        0x00000008
#define MULTIBOOT_INFO_MEM_MAP     0x00000040
#define MULTIBOOT_INFO_DRIVE_INFO  0x00000080
#define MULTIBOOT_INFO_CONFIG_TBL  0x00000100
#define MULTIBOOT_INFO_BOOT_LOADER 0x00000200
#define MULTIBOOT_INFO_APM_TABLE   0x00000400
#define MULTIBOOT_INFO_VBE_INFO    0x00000800
#define MULTIBOOT_INFO_FRAMEBUFFER 0x00001000

#define MULTIBOOT_MEMORY_AVAILABLE        1
#define MULTIBOOT_MEMORY_RESERVED         2
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE 3
#define MULTIBOOT_MEMORY_NVS              4
#define MULTIBOOT_MEMORY_BADRAM           5

typedef struct __attribute__((packed)) {
    uint32_t size;
    uint64_t addr;
    uint64_t len;
    uint32_t type;
} multiboot_memory_map_t;

typedef struct __attribute__((packed)) {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
} multiboot_info_t;

void multiboot_dump_info(uint64_t magic, uint64_t mb_addr);
uint64_t multiboot_get_total_memory(void);
uint64_t multiboot_get_usable_ram_start(void);
uint64_t multiboot_get_usable_ram_end(void);

#endif /* _CORE_MULTIBOOT_H */
