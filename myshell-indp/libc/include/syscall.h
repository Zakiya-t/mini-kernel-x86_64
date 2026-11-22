#ifndef _LIBC_SYSCALL_H
#define _LIBC_SYSCALL_H

#include "stdint.h"
#include "stddef.h"

#define SYS_EXIT         1
#define SYS_WRITE        2
#define SYS_READ         3
#define SYS_GETPID       4
#define SYS_YIELD        5
#define SYS_MEM_ALLOC    6
#define SYS_MEM_FREE     7
#define SYS_MEM_STATS    8
#define SYS_SCHED_GET    9
#define SYS_SCHED_SET    10
#define SYS_PROC_LIST    11
#define SYS_OBSERVE      12
#define SYS_TEST_MEM     13
#define SYS_SPAWN        14
#define SYS_KILL         15
#define SYS_REAP         16
#define SYS_FORK         17
#define SYS_EXEC         18
#define SYS_OPEN         19
#define SYS_CLOSE        20
#define SYS_FS_CREATE    21
#define SYS_FS_UNLINK    22
#define SYS_FS_LIST      23
#define SYS_FS_STAT      24
#define SYS_FS_FORMAT    25
#define SYS_DISK_INFO    26
#define SYS_GETCWD       27
#define SYS_CHDIR        28
#define SYS_MKDIR        29
#define SYS_SEEK         30
#define SYS_SHUTDOWN     31

#define USER_FS_TYPE_UNUSED   0
#define USER_FS_TYPE_FILE     1
#define USER_FS_TYPE_DIR      2

#define USER_O_RDONLY         0x0001
#define USER_O_WRONLY         0x0002
#define USER_O_RDWR           0x0003
#define USER_O_CREAT          0x0040
#define USER_O_TRUNC          0x0200
#define USER_O_APPEND         0x0400

#define USER_SEEK_SET         0
#define USER_SEEK_CUR         1
#define USER_SEEK_END         2

typedef struct {
    uint64_t total_phys_ram;
    uint64_t free_phys_ram;
    uint64_t total_frames;
    uint64_t free_frames;
    uint64_t heap_total;
    uint64_t heap_used;
    uint64_t heap_free;
    uint32_t alloc_count;
    uint32_t free_count;
    uint32_t fail_count;
} user_mem_stats_t;

typedef struct {
    uint32_t pid;
    uint32_t ppid;
    char     name[32];
    uint32_t state;
    uint32_t priority;
    uint64_t cpu_ticks;
    uint32_t ring;
} user_proc_info_t;

typedef struct {
    uint32_t inode_id;
    uint16_t type;
    uint32_t size;
    uint32_t block_count;
} user_fs_stat_t;

typedef struct {
    uint32_t inode_id;
    uint8_t  type;
    uint32_t size;
    char     name[28];
} user_fs_dirent_t;

typedef struct {
    bool     present;
    char     model[41];
    char     serial[21];
    uint32_t total_sectors;
    uint32_t size_mb;
} user_ata_device_info_t;

int64_t sys_call(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4);

#endif

