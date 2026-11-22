#include "core/multiboot.h"
#include "core/kprintf.h"

static uint64_t total_memory_bytes = 0;
static uint64_t usable_ram_start = 0;
static uint64_t usable_ram_end = 0;

static const char *get_mmap_type_name(uint32_t type) {
    switch (type) {
        case MULTIBOOT_MEMORY_AVAILABLE:        return "Available RAM";
        case MULTIBOOT_MEMORY_RESERVED:         return "Reserved";
        case MULTIBOOT_MEMORY_ACPI_RECLAIMABLE: return "ACPI Reclaimable";
        case MULTIBOOT_MEMORY_NVS:              return "ACPI NVS";
        case MULTIBOOT_MEMORY_BADRAM:           return "Bad Memory";
        default:                                return "Unknown";
    }
}

void multiboot_dump_info(uint64_t magic, uint64_t mb_addr) {
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        KLOG_ERR("Invalid Multiboot magic: 0x%08x (Expected 0x2BADB002)", (uint32_t)magic);
        return;
    }

    multiboot_info_t *mbi = (multiboot_info_t *)mb_addr;
    KLOG_OK("Multiboot verified at 0x%016lx (Flags: 0x%08x)", mb_addr, mbi->flags);

    if (mbi->flags & MULTIBOOT_INFO_BOOT_LOADER) {
        const char *bootloader = (const char *)(uint64_t)mbi->boot_loader_name;
        KLOG_INFO("Bootloader: %s", bootloader);
    }

    if (mbi->flags & MULTIBOOT_INFO_CMDLINE) {
        const char *cmdline = (const char *)(uint64_t)mbi->cmdline;
        KLOG_INFO("Kernel Command Line: \"%s\"", cmdline);
    }

    if (mbi->flags & MULTIBOOT_INFO_MEMORY) {
        uint64_t lower_kb = mbi->mem_lower;
        uint64_t upper_kb = mbi->mem_upper;
        KLOG_INFO("Basic Memory: Lower: %u KiB, Upper: %u KiB (~%u MiB)",
                  (uint32_t)lower_kb, (uint32_t)upper_kb, (uint32_t)((upper_kb + 1024) / 1024));
    }

    if (mbi->flags & MULTIBOOT_INFO_MEM_MAP) {
        KLOG_INFO("Hardware Memory Map (from BIOS/QEMU):");
        uint64_t mmap_addr = (uint64_t)mbi->mmap_addr;
        uint64_t mmap_end = mmap_addr + mbi->mmap_length;

        total_memory_bytes = 0;

        while (mmap_addr < mmap_end) {
            multiboot_memory_map_t *entry = (multiboot_memory_map_t *)mmap_addr;

            kprintf("  Region [0x%016lx - 0x%016lx] (%4u MiB) : %s\n",
                    entry->addr, entry->addr + entry->len,
                    (uint32_t)(entry->len / (1024 * 1024)),
                    get_mmap_type_name(entry->type));

            if (entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
                total_memory_bytes += entry->len;
                if (entry->addr >= 0x100000) { // High RAM above 1MB
                    if (usable_ram_start == 0) {
                        usable_ram_start = entry->addr;
                    }
                    if (entry->addr + entry->len > usable_ram_end) {
                        usable_ram_end = entry->addr + entry->len;
                    }
                }
            }

            mmap_addr += entry->size + sizeof(entry->size);
        }

        KLOG_OK("Total Usable Physical RAM: %u MiB (%u Bytes)",
                (uint32_t)(total_memory_bytes / (1024 * 1024)),
                (uint32_t)total_memory_bytes);
    }
}

uint64_t multiboot_get_total_memory(void) {
    return total_memory_bytes;
}

uint64_t multiboot_get_usable_ram_start(void) {
    return usable_ram_start;
}

uint64_t multiboot_get_usable_ram_end(void) {
    return usable_ram_end;
}
