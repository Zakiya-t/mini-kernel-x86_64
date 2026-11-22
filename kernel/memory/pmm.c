#include "memory/pmm.h"
#include "core/kprintf.h"
#include "string.h"

static uint64_t *pmm_bitmap = NULL;
static size_t    pmm_total_frames = 0;
static size_t    pmm_used_frames = 0;
static uint64_t  pmm_max_phys_addr = 0;

static inline void bitmap_set(size_t frame) {
    if (frame < pmm_total_frames) {
        if (!(pmm_bitmap[frame / 64] & (1ULL << (frame % 64)))) {
            pmm_bitmap[frame / 64] |= (1ULL << (frame % 64));
            pmm_used_frames++;
        }
    }
}

static inline void bitmap_clear(size_t frame) {
    if (frame < pmm_total_frames) {
        if (pmm_bitmap[frame / 64] & (1ULL << (frame % 64))) {
            pmm_bitmap[frame / 64] &= ~(1ULL << (frame % 64));
            pmm_used_frames--;
        }
    }
}

static inline bool bitmap_test(size_t frame) {
    if (frame >= pmm_total_frames) return true; // Treat out-of-bounds as used
    return (pmm_bitmap[frame / 64] & (1ULL << (frame % 64))) != 0;
}

static void pmm_mark_region_free(uint64_t base, uint64_t length) {
    size_t start_frame = (size_t)(PAGE_ALIGN_UP(base) / PAGE_SIZE);
    size_t end_frame   = (size_t)(PAGE_ALIGN_DOWN(base + length) / PAGE_SIZE);

    for (size_t frame = start_frame; frame < end_frame && frame < pmm_total_frames; frame++) {
        bitmap_clear(frame);
    }
}

static void pmm_mark_region_used(uint64_t base, uint64_t length) {
    size_t start_frame = (size_t)(PAGE_ALIGN_DOWN(base) / PAGE_SIZE);
    size_t end_frame   = (size_t)(PAGE_ALIGN_UP(base + length) / PAGE_SIZE);

    for (size_t frame = start_frame; frame < end_frame && frame < pmm_total_frames; frame++) {
        bitmap_set(frame);
    }
}

void pmm_init(uint64_t mb_addr, uint64_t kernel_end_addr) {
    multiboot_info_t *mbi = (multiboot_info_t *)mb_addr;

    /* 1. Determine maximum physical RAM address from Multiboot memory map */
    if (mbi->flags & MULTIBOOT_INFO_MEM_MAP) {
        uint64_t mmap_addr = (uint64_t)mbi->mmap_addr;
        uint64_t mmap_end  = mmap_addr + mbi->mmap_length;

        while (mmap_addr < mmap_end) {
            multiboot_memory_map_t *entry = (multiboot_memory_map_t *)mmap_addr;
            if (entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
                uint64_t top = entry->addr + entry->len;
                if (top > pmm_max_phys_addr) {
                    pmm_max_phys_addr = top;
                }
            }
            mmap_addr += entry->size + sizeof(entry->size);
        }
    }

    if (pmm_max_phys_addr == 0) {
        /* Fallback: assume 512 MiB if memory map was unavailable */
        pmm_max_phys_addr = 512 * 1024 * 1024;
    }

    pmm_total_frames = (size_t)(pmm_max_phys_addr / PAGE_SIZE);
    size_t bitmap_size_bytes = (pmm_total_frames + 7) / 8;
    bitmap_size_bytes = PAGE_ALIGN_UP(bitmap_size_bytes);

    /* 2. Place the bitmap immediately after kernel_end (page-aligned) */
    pmm_bitmap = (uint64_t *)PAGE_ALIGN_UP(kernel_end_addr);
    uint64_t bitmap_end = (uint64_t)pmm_bitmap + bitmap_size_bytes;

    /* 3. Initially mark ALL frames as used/reserved */
    memset(pmm_bitmap, 0xFF, bitmap_size_bytes);
    pmm_used_frames = pmm_total_frames;

    /* 4. Mark all RAM regions designated as AVAILABLE by the BIOS as free */
    if (mbi->flags & MULTIBOOT_INFO_MEM_MAP) {
        uint64_t mmap_addr = (uint64_t)mbi->mmap_addr;
        uint64_t mmap_end  = mmap_addr + mbi->mmap_length;

        while (mmap_addr < mmap_end) {
            multiboot_memory_map_t *entry = (multiboot_memory_map_t *)mmap_addr;
            if (entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
                pmm_mark_region_free(entry->addr, entry->len);
            }
            mmap_addr += entry->size + sizeof(entry->size);
        }
    }

    /* 5. Protect critical system memory by re-marking as used:
     *    a. Low 1 MiB (0x0 to 0x100000): IVT, BDA, EBDA, Video RAM 0xB8000, BIOS ROM
     *    b. Kernel image: 0x100000 to kernel_end
     *    c. PMM Bitmap itself: kernel_end to bitmap_end
     *    d. Multiboot information structure
     */
    pmm_mark_region_used(0x00000000, 0x00100000);
    pmm_mark_region_used(0x00100000, kernel_end_addr - 0x00100000);
    pmm_mark_region_used((uint64_t)pmm_bitmap, bitmap_size_bytes);
    pmm_mark_region_used(mb_addr, sizeof(multiboot_info_t));

    KLOG_OK("PMM initialized (Bitmap at 0x%016lx - 0x%016lx, Size: %u KiB)",
            (uint64_t)pmm_bitmap, bitmap_end, (uint32_t)(bitmap_size_bytes / 1024));
    pmm_dump_stats();
}

uint64_t pmm_alloc_frame(void) {
    size_t words = (pmm_total_frames + 63) / 64;

    for (size_t i = 0; i < words; i++) {
        if (pmm_bitmap[i] != 0xFFFFFFFFFFFFFFFFULL) {
            /* At least one bit is 0 (free frame) */
            int bit = __builtin_ctzll(~pmm_bitmap[i]);
            size_t frame = i * 64 + bit;

            if (frame < pmm_total_frames) {
                bitmap_set(frame);
                return (uint64_t)frame * PAGE_SIZE;
            }
        }
    }

    KLOG_ERR("PMM: Physical memory exhausted (out of page frames)!");
    return 0; // Out of memory
}

void pmm_free_frame(uint64_t frame_phys_addr) {
    if (frame_phys_addr & (PAGE_SIZE - 1)) {
        KLOG_WARN("PMM: Freeing unaligned frame address 0x%016lx", frame_phys_addr);
        return;
    }

    size_t frame = (size_t)(frame_phys_addr / PAGE_SIZE);
    if (frame >= pmm_total_frames) {
        KLOG_WARN("PMM: Freeing out-of-bounds frame 0x%016lx", frame_phys_addr);
        return;
    }

    if (!bitmap_test(frame)) {
        KLOG_WARN("PMM: Double-free detected on physical frame 0x%016lx!", frame_phys_addr);
        return;
    }

    bitmap_clear(frame);
}

uint64_t pmm_alloc_frames(size_t count) {
    if (count == 0) return 0;
    if (count == 1) return pmm_alloc_frame();

    size_t consecutive = 0;
    size_t start_frame = 0;

    for (size_t frame = 0; frame < pmm_total_frames; frame++) {
        if (!bitmap_test(frame)) {
            if (consecutive == 0) {
                start_frame = frame;
            }
            consecutive++;
            if (consecutive == count) {
                for (size_t j = start_frame; j < start_frame + count; j++) {
                    bitmap_set(j);
                }
                return (uint64_t)start_frame * PAGE_SIZE;
            }
        } else {
            consecutive = 0;
        }
    }

    KLOG_ERR("PMM: Failed to allocate %u contiguous physical frames!", (uint32_t)count);
    return 0;
}

void pmm_free_frames(uint64_t frame_phys_addr, size_t count) {
    if (frame_phys_addr & (PAGE_SIZE - 1)) {
        KLOG_WARN("PMM: Freeing unaligned frames address 0x%016lx", frame_phys_addr);
        return;
    }

    size_t start_frame = (size_t)(frame_phys_addr / PAGE_SIZE);
    for (size_t i = 0; i < count; i++) {
        pmm_free_frame((uint64_t)(start_frame + i) * PAGE_SIZE);
    }
}

size_t   pmm_get_total_frames(void)  { return pmm_total_frames; }
size_t   pmm_get_used_frames(void)   { return pmm_used_frames; }
size_t   pmm_get_free_frames(void)   { return pmm_total_frames - pmm_used_frames; }
uint64_t pmm_get_total_memory(void)  { return (uint64_t)pmm_total_frames * PAGE_SIZE; }
uint64_t pmm_get_free_memory(void)   { return (uint64_t)(pmm_total_frames - pmm_used_frames) * PAGE_SIZE; }
uint64_t pmm_get_used_memory(void)   { return (uint64_t)pmm_used_frames * PAGE_SIZE; }

void pmm_dump_stats(void) {
    size_t total = pmm_get_total_frames();
    size_t used  = pmm_get_used_frames();
    size_t free  = pmm_get_free_frames();

    kprintf("  [PMM Stats] Total: %u frames (%u MiB) | Used: %u frames (%u MiB) | Free: %u frames (%u MiB)\n",
            (uint32_t)total, (uint32_t)(pmm_get_total_memory() / (1024 * 1024)),
            (uint32_t)used,  (uint32_t)(pmm_get_used_memory() / (1024 * 1024)),
            (uint32_t)free,  (uint32_t)(pmm_get_free_memory() / (1024 * 1024)));
}
