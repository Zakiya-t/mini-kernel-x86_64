#include "memory/heap.h"
#include "memory/pmm.h"
#include "core/kprintf.h"
#include "string.h"

#define HEAP_MAGIC_ALLOC   0xDEADBEEF
#define HEAP_MAGIC_FREE    0xFEEDFACE
#define HEAP_FOOTER_MAGIC  0xCAFEBABE

typedef struct heap_block {
    uint32_t magic;
    uint32_t is_free;
    size_t   size; // Usable payload size
    struct heap_block *prev;
    struct heap_block *next;
} __attribute__((aligned(16))) heap_block_t;

typedef struct heap_footer {
    uint32_t magic;
    uint32_t reserved;
    size_t   size;
} __attribute__((aligned(16))) heap_footer_t;

#define BLOCK_OVERHEAD (sizeof(heap_block_t) + sizeof(heap_footer_t))

static uintptr_t heap_start_addr = 0;
static uintptr_t heap_end_addr = 0;
static heap_block_t *heap_first_block = NULL;

static size_t stat_heap_total = 0;
static size_t stat_heap_used  = 0;
static size_t stat_alloc_count = 0;
static size_t stat_free_count  = 0;
static size_t stat_fail_count  = 0;

static inline heap_footer_t *get_footer(heap_block_t *block) {
    return (heap_footer_t *)((uint8_t *)block + sizeof(heap_block_t) + block->size);
}

static inline heap_block_t *get_block_from_ptr(void *ptr) {
    return (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));
}

void heap_init(size_t initial_size_bytes) {
    if (initial_size_bytes == 0) {
        initial_size_bytes = 16 * 1024 * 1024; // 16 MiB default initial heap
    }

    size_t pages_needed = (initial_size_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t phys_frames = pmm_alloc_frames(pages_needed);

    if (!phys_frames) {
        KLOG_ERR("HEAP: Failed to allocate physical memory for kernel heap!");
        return;
    }

    heap_start_addr = (uintptr_t)phys_frames;
    heap_end_addr   = heap_start_addr + pages_needed * PAGE_SIZE;
    stat_heap_total = pages_needed * PAGE_SIZE;
    stat_heap_used  = 0;

    /* Initialize single initial free block covering the whole heap */
    heap_first_block = (heap_block_t *)heap_start_addr;
    heap_first_block->magic   = HEAP_MAGIC_FREE;
    heap_first_block->is_free = 1;
    heap_first_block->size    = stat_heap_total - BLOCK_OVERHEAD;
    heap_first_block->prev    = NULL;
    heap_first_block->next    = NULL;

    heap_footer_t *footer = get_footer(heap_first_block);
    footer->magic = HEAP_FOOTER_MAGIC;
    footer->size  = heap_first_block->size;

    KLOG_OK("Kernel Heap initialized: [0x%016lx - 0x%016lx] (%u MiB capacity)",
            heap_start_addr, heap_end_addr, (uint32_t)(stat_heap_total / (1024 * 1024)));
}

void *kmalloc(size_t size) {
    if (size == 0) return NULL;

    /* Align requested size up to 16 bytes for System V AMD64 alignment */
    size = (size + 15) & ~15ULL;
    if (size < 16) size = 16;

    heap_block_t *curr = heap_first_block;

    /* First-fit search */
    while (curr) {
        if (curr->is_free && curr->size >= size) {
            /* Check if block can be split */
            if (curr->size >= size + BLOCK_OVERHEAD + 16) {
                size_t remainder_size = curr->size - size - BLOCK_OVERHEAD;

                curr->size = size;
                heap_footer_t *curr_footer = get_footer(curr);
                curr_footer->magic = HEAP_FOOTER_MAGIC;
                curr_footer->size  = size;

                /* Create remainder free block */
                heap_block_t *remainder = (heap_block_t *)((uint8_t *)curr_footer + sizeof(heap_footer_t));
                remainder->magic   = HEAP_MAGIC_FREE;
                remainder->is_free = 1;
                remainder->size    = remainder_size;
                remainder->prev    = curr;
                remainder->next    = curr->next;

                if (curr->next) {
                    curr->next->prev = remainder;
                }
                curr->next = remainder;

                heap_footer_t *rem_footer = get_footer(remainder);
                rem_footer->magic = HEAP_FOOTER_MAGIC;
                rem_footer->size  = remainder_size;
            }

            curr->is_free = 0;
            curr->magic   = HEAP_MAGIC_ALLOC;

            stat_heap_used += curr->size + BLOCK_OVERHEAD;
            stat_alloc_count++;

            void *payload = (void *)((uint8_t *)curr + sizeof(heap_block_t));
            return payload;
        }
        curr = curr->next;
    }

    stat_fail_count++;
    KLOG_ERR("kmalloc: Heap allocation failed for %u bytes (Heap exhausted)", (uint32_t)size);
    return NULL;
}

void kfree(void *ptr) {
    if (!ptr) return;

    /* 1. Alignment check */
    if ((uintptr_t)ptr & 0x0F) {
        KLOG_WARN("kfree: Rejected invalid free of unaligned pointer %p", ptr);
        return;
    }

    /* 2. Heap boundary check */
    if ((uintptr_t)ptr < heap_start_addr + sizeof(heap_block_t) || (uintptr_t)ptr >= heap_end_addr) {
        KLOG_WARN("kfree: Rejected invalid free of out-of-bounds pointer %p", ptr);
        return;
    }

    heap_block_t *block = get_block_from_ptr(ptr);

    /* 3. Magic and Double-free detection */
    if (block->magic == HEAP_MAGIC_FREE || block->is_free == 1) {
        KLOG_WARN("kfree: DOUBLE FREE detected on pointer %p! Operation aborted.", ptr);
        return;
    }

    if (block->magic != HEAP_MAGIC_ALLOC) {
        KLOG_WARN("kfree: CORRUPTED BLOCK header (magic: 0x%08x) on pointer %p!", block->magic, ptr);
        return;
    }

    heap_footer_t *footer = get_footer(block);
    if (footer->magic != HEAP_FOOTER_MAGIC) {
        KLOG_WARN("kfree: CORRUPTED FOOTER (Buffer Overflow detected!) on pointer %p!", ptr);
        return;
    }

    /* 4. Mark block free */
    block->magic   = HEAP_MAGIC_FREE;
    block->is_free = 1;
    stat_heap_used -= (block->size + BLOCK_OVERHEAD);
    stat_free_count++;

    /* 5. Coalesce right with next block if free */
    if (block->next && block->next->is_free) {
        heap_block_t *next_block = block->next;
        block->size += next_block->size + BLOCK_OVERHEAD;
        block->next  = next_block->next;
        if (next_block->next) {
            next_block->next->prev = block;
        }

        heap_footer_t *new_footer = get_footer(block);
        new_footer->magic = HEAP_FOOTER_MAGIC;
        new_footer->size  = block->size;
    }

    /* 6. Coalesce left with prev block if free */
    if (block->prev && block->prev->is_free) {
        heap_block_t *prev_block = block->prev;
        prev_block->size += block->size + BLOCK_OVERHEAD;
        prev_block->next  = block->next;
        if (block->next) {
            block->next->prev = prev_block;
        }

        heap_footer_t *new_footer = get_footer(prev_block);
        new_footer->magic = HEAP_FOOTER_MAGIC;
        new_footer->size  = prev_block->size;
    }
}

void *kcalloc(size_t num, size_t size) {
    size_t total = num * size;
    void *ptr = kmalloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *krealloc(void *ptr, size_t new_size) {
    if (!ptr) return kmalloc(new_size);
    if (new_size == 0) {
        kfree(ptr);
        return NULL;
    }

    heap_block_t *block = get_block_from_ptr(ptr);
    if (block->magic != HEAP_MAGIC_ALLOC) return NULL;

    if (block->size >= new_size) {
        return ptr; // Current block is already big enough
    }

    void *new_ptr = kmalloc(new_size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, block->size);
        kfree(ptr);
    }
    return new_ptr;
}

size_t heap_total_bytes(void)  { return stat_heap_total; }
size_t heap_used_bytes(void)   { return stat_heap_used; }
size_t heap_free_bytes(void)   { return stat_heap_total > stat_heap_used ? stat_heap_total - stat_heap_used : 0; }
size_t heap_alloc_count(void)  { return stat_alloc_count; }
size_t heap_free_count(void)   { return stat_free_count; }
size_t heap_fail_count(void)   { return stat_fail_count; }

void heap_dump_stats(void) {
    kprintf("  [Heap Stats] Total: %u KiB | Used: %u KiB | Free: %u KiB | Allocs: %u | Frees: %u | Fails: %u\n",
            (uint32_t)(heap_total_bytes() / 1024),
            (uint32_t)(heap_used_bytes() / 1024),
            (uint32_t)(heap_free_bytes() / 1024),
            (uint32_t)heap_alloc_count(),
            (uint32_t)heap_free_count(),
            (uint32_t)heap_fail_count());
}
