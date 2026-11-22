#ifndef _MEMORY_PMM_H
#define _MEMORY_PMM_H

#include "types.h"
#include "core/multiboot.h"

#define PAGE_SIZE       4096ULL
#define PAGE_SHIFT      12ULL
#define PAGE_ALIGN_MASK (~(PAGE_SIZE - 1))

#define PAGE_ALIGN_DOWN(addr) ((addr) & PAGE_ALIGN_MASK)
#define PAGE_ALIGN_UP(addr)   (((addr) + PAGE_SIZE - 1) & PAGE_ALIGN_MASK)

void     pmm_init(uint64_t mb_addr, uint64_t kernel_end_addr);
uint64_t pmm_alloc_frame(void);
void     pmm_free_frame(uint64_t frame_phys_addr);
uint64_t pmm_alloc_frames(size_t count);
void     pmm_free_frames(uint64_t frame_phys_addr, size_t count);

size_t   pmm_get_total_frames(void);
size_t   pmm_get_used_frames(void);
size_t   pmm_get_free_frames(void);
uint64_t pmm_get_total_memory(void);
uint64_t pmm_get_free_memory(void);
uint64_t pmm_get_used_memory(void);

void     pmm_dump_stats(void);

#endif /* _MEMORY_PMM_H */
