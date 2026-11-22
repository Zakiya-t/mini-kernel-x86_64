#include "memory/vmm.h"
#include "memory/pmm.h"
#include "arch/x86_64/cpu.h"
#include "core/kprintf.h"
#include "string.h"

static uint64_t *kernel_pml4 = NULL;

uint64_t *vmm_get_kernel_pml4(void) {
    return kernel_pml4;
}

int vmm_map_page(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t flags) {
    if (!pml4) return -1;

    size_t pml4_i = PML4_INDEX(virt);
    size_t pdpt_i = PDPT_INDEX(virt);
    size_t pd_i   = PD_INDEX(virt);
    size_t pt_i   = PT_INDEX(virt);

    /* 1. Walk or allocate PDPT */
    uint64_t *pdpt;
    if (!(pml4[pml4_i] & PAGE_PRESENT)) {
        uint64_t new_frame = pmm_alloc_frame();
        if (!new_frame) return -2;
        memset((void *)new_frame, 0, PAGE_SIZE);
        pml4[pml4_i] = new_frame | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
        pdpt = (uint64_t *)new_frame;
    } else {
        pdpt = (uint64_t *)(pml4[pml4_i] & PAGE_ADDR_MASK);
        if (flags & PAGE_USER) {
            pml4[pml4_i] |= PAGE_USER;
        }
    }

    /* 2. Walk or allocate PD */
    uint64_t *pd;
    if (!(pdpt[pdpt_i] & PAGE_PRESENT)) {
        uint64_t new_frame = pmm_alloc_frame();
        if (!new_frame) return -2;
        memset((void *)new_frame, 0, PAGE_SIZE);
        pdpt[pdpt_i] = new_frame | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
        pd = (uint64_t *)new_frame;
    } else {
        pd = (uint64_t *)(pdpt[pdpt_i] & PAGE_ADDR_MASK);
        if (flags & PAGE_USER) {
            pdpt[pdpt_i] |= PAGE_USER;
        }
    }

    /* 3. Walk or allocate PT */
    uint64_t *pt;
    if (!(pd[pd_i] & PAGE_PRESENT)) {
        uint64_t new_frame = pmm_alloc_frame();
        if (!new_frame) return -2;
        memset((void *)new_frame, 0, PAGE_SIZE);
        pd[pd_i] = new_frame | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
        pt = (uint64_t *)new_frame;
    } else if (pd[pd_i] & PAGE_HUGE) {
        /* Split 2 MiB huge page into 512 standard 4 KiB pages */
        uint64_t new_frame = pmm_alloc_frame();
        if (!new_frame) return -2;
        pt = (uint64_t *)new_frame;
        uint64_t huge_phys_base = pd[pd_i] & 0x000FFFFFFFE00000ULL;
        uint64_t orig_flags = pd[pd_i] & (PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER | PAGE_WRITETHROUGH | PAGE_CACHE_DISABLE);
        for (size_t j = 0; j < 512; j++) {
            pt[j] = (huge_phys_base + j * PAGE_SIZE) | orig_flags;
        }
        pd[pd_i] = new_frame | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
        write_cr3(read_cr3());
    } else {
        pt = (uint64_t *)(pd[pd_i] & PAGE_ADDR_MASK);
        if (flags & PAGE_USER) {
            pd[pd_i] |= PAGE_USER;
        }
    }

    /* 4. Set the leaf Page Table Entry */
    pt[pt_i] = (phys & PAGE_ADDR_MASK) | flags | PAGE_PRESENT;

    /* Invalidate TLB for this virtual address */
    invlpg(virt);

    return 0;
}

int vmm_unmap_page(uint64_t *pml4, uint64_t virt) {
    if (!pml4) return -1;

    size_t pml4_i = PML4_INDEX(virt);
    size_t pdpt_i = PDPT_INDEX(virt);
    size_t pd_i   = PD_INDEX(virt);
    size_t pt_i   = PT_INDEX(virt);

    if (!(pml4[pml4_i] & PAGE_PRESENT)) return -1;
    uint64_t *pdpt = (uint64_t *)(pml4[pml4_i] & PAGE_ADDR_MASK);

    if (!(pdpt[pdpt_i] & PAGE_PRESENT)) return -1;
    uint64_t *pd = (uint64_t *)(pdpt[pdpt_i] & PAGE_ADDR_MASK);

    if (!(pd[pd_i] & PAGE_PRESENT)) return -1;
    uint64_t *pt = (uint64_t *)(pd[pd_i] & PAGE_ADDR_MASK);

    pt[pt_i] = 0;
    invlpg(virt);

    return 0;
}

uint64_t vmm_virt_to_phys(uint64_t *pml4, uint64_t virt) {
    if (!pml4) return 0;

    size_t pml4_i = PML4_INDEX(virt);
    size_t pdpt_i = PDPT_INDEX(virt);
    size_t pd_i   = PD_INDEX(virt);
    size_t pt_i   = PT_INDEX(virt);

    if (!(pml4[pml4_i] & PAGE_PRESENT)) return 0;
    uint64_t *pdpt = (uint64_t *)(pml4[pml4_i] & PAGE_ADDR_MASK);

    if (!(pdpt[pdpt_i] & PAGE_PRESENT)) return 0;
    uint64_t *pd = (uint64_t *)(pdpt[pdpt_i] & PAGE_ADDR_MASK);

    if (!(pd[pd_i] & PAGE_PRESENT)) return 0;

    /* Check if 2 MiB huge page */
    if (pd[pd_i] & PAGE_HUGE) {
        return (pd[pd_i] & 0x000FFFFFFFE00000ULL) | (virt & 0x1FFFFFULL);
    }

    uint64_t *pt = (uint64_t *)(pd[pd_i] & PAGE_ADDR_MASK);
    if (!(pt[pt_i] & PAGE_PRESENT)) return 0;

    return (pt[pt_i] & PAGE_ADDR_MASK) | (virt & 0xFFFULL);
}

void vmm_switch_address_space(uint64_t *pml4) {
    if (pml4) {
        write_cr3((uint64_t)pml4);
    }
}

uint64_t *vmm_create_address_space(void) {
    uint64_t pml4_phys = pmm_alloc_frame();
    if (!pml4_phys) return NULL;

    uint64_t *new_pml4 = (uint64_t *)pml4_phys;
    memset(new_pml4, 0, PAGE_SIZE);

    /* Mirror the kernel's mapping from kernel_pml4 */
    /* Lower 1 GiB is mapped in entry 0 */
    new_pml4[0] = kernel_pml4[0];

    /* Upper half (entries 256..511) reserved for higher-half kernel mapping if used */
    for (size_t i = 256; i < 512; i++) {
        new_pml4[i] = kernel_pml4[i];
    }

    return new_pml4;
}

void vmm_destroy_address_space(uint64_t *pml4) {
    if (!pml4 || pml4 == kernel_pml4) return;

    /* Free user-space PML4 frame (user tables will be cleaned up on process exit) */
    pmm_free_frame((uint64_t)pml4);
}

void vmm_init(void) {
    /* Allocate dedicated kernel PML4 table from PMM */
    uint64_t pml4_frame = pmm_alloc_frame();
    kernel_pml4 = (uint64_t *)pml4_frame;
    memset(kernel_pml4, 0, PAGE_SIZE);

    /*
     * Identity-map the first 1 GiB of physical memory using 2 MiB huge pages
     * to preserve seamless access to all physical RAM, kernel code,
     * serial ports, VGA memory (0xB8000), and devices.
     */
    uint64_t pdpt_frame = pmm_alloc_frame();
    uint64_t *pdpt = (uint64_t *)pdpt_frame;
    memset(pdpt, 0, PAGE_SIZE);
    kernel_pml4[0] = pdpt_frame | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;

    uint64_t pd_frame = pmm_alloc_frame();
    uint64_t *pd = (uint64_t *)pd_frame;
    memset(pd, 0, PAGE_SIZE);
    pdpt[0] = pd_frame | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;

    /* 512 entries of 2 MiB huge pages = 1 GiB (User accessible for Ring 3 execution) */
    for (size_t i = 0; i < 512; i++) {
        pd[i] = (i * 0x200000ULL) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_HUGE | PAGE_USER;
    }

    /* Switch CR3 to our new permanent kernel PML4 */
    vmm_switch_address_space(kernel_pml4);

    KLOG_OK("VMM initialized: 4-Level Paging active (Kernel PML4 @ 0x%016lx, 1 GiB Identity Map)",
            (uint64_t)kernel_pml4);
}

