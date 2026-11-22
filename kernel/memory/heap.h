#ifndef _MEMORY_HEAP_H
#define _MEMORY_HEAP_H

#include "types.h"

void   heap_init(size_t initial_size_bytes);
void  *kmalloc(size_t size);
void   kfree(void *ptr);
void  *kcalloc(size_t num, size_t size);
void  *krealloc(void *ptr, size_t new_size);

/* Memory usage statistics */
size_t heap_total_bytes(void);
size_t heap_used_bytes(void);
size_t heap_free_bytes(void);
size_t heap_alloc_count(void);
size_t heap_free_count(void);
size_t heap_fail_count(void);

void   heap_dump_stats(void);

#endif /* _MEMORY_HEAP_H */
