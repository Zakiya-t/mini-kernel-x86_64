#ifndef _MEMORY_VMM_H
#define _MEMORY_VMM_H

#include "types.h"

/* x86-64 Page Table Entry Flags */
#define PAGE_PRESENT         (1ULL << 0)
#define PAGE_WRITABLE        (1ULL << 1)
#define PAGE_USER            (1ULL << 2)
#define PAGE_WRITETHROUGH    (1ULL << 3)
#define PAGE_CACHE_DISABLE   (1ULL << 4)
#define PAGE_ACCESSED        (1ULL << 5)
#define PAGE_DIRTY           (1ULL << 6)
#define PAGE_HUGE            (1ULL << 7)
#define PAGE_GLOBAL          (1ULL << 8)
#define PAGE_NX              (1ULL << 63)

#define PAGE_ADDR_MASK       0x000FFFFFFFFFF000ULL

#define PML4_INDEX(v)        (((v) >> 39) & 0x1FF)
#define PDPT_INDEX(v)        (((v) >> 30) & 0x1FF)
#define PD_INDEX(v)          (((v) >> 21) & 0x1FF)
#define PT_INDEX(v)          (((v) >> 12) & 0x1FF)

void      vmm_init(void);
uint64_t *vmm_get_kernel_pml4(void);
int       vmm_map_page(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags);
int       vmm_unmap_page(uint64_t *pml4, uint64_t virt);
uint64_t  vmm_virt_to_phys(uint64_t *pml4, uint64_t virt);

uint64_t *vmm_create_address_space(void);
void      vmm_destroy_address_space(uint64_t *pml4);
void      vmm_switch_address_space(uint64_t *pml4);

static inline void invlpg(uint64_t virt_addr) {
    __asm__ volatile ("invlpg (%0)" : : "r"(virt_addr) : "memory");
}

#endif /* _MEMORY_VMM_H */
