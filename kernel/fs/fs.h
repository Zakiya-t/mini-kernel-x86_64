#ifndef _KERNEL_FS_FS_H
#define _KERNEL_FS_FS_H

#include "types.h"

#define FS_MAGIC               0x4D494E32  /* "MIN2" - MiniFS v2 with Indirect Blocks */
#define FS_BLOCK_SIZE          1024
#define FS_SECTORS_PER_BLOCK   2
#define FS_START_LBA           2048        /* 1 MiB offset on disk */

#define FS_TOTAL_BLOCKS        16384       /* 16 MiB filesystem */
#define FS_TOTAL_INODES        256         /* 256 inodes */
#define FS_INODES_PER_BLOCK    16          /* 1024 / 64 bytes = 16 */
#define FS_INODE_TABLE_BLOCKS  16          /* 16 * 16 = 256 inodes */

#define FS_INODE_BITMAP_BLOCK  1
#define FS_BLOCK_BITMAP_BLOCK  2
#define FS_INODE_TABLE_START   4
#define FS_DATA_BLOCKS_START   20
#define FS_ROOT_INODE          1

#define FS_DIRECT_BLOCKS       8
#define FS_PTRS_PER_BLOCK      (FS_BLOCK_SIZE / sizeof(uint32_t)) /* 256 */
#define FS_SINGLE_INDIRECT_BLOCKS (FS_PTRS_PER_BLOCK)             /* 256 */
#define FS_DOUBLE_INDIRECT_BLOCKS (FS_PTRS_PER_BLOCK * FS_PTRS_PER_BLOCK) /* 65536 */
#define FS_MAX_FILE_BLOCKS     (FS_DIRECT_BLOCKS + FS_SINGLE_INDIRECT_BLOCKS + FS_DOUBLE_INDIRECT_BLOCKS)
#define FS_MAX_FILE_SIZE       ((uint64_t)FS_MAX_FILE_BLOCKS * FS_BLOCK_SIZE) /* ~64.25 MiB */
#define FS_MAX_NAME_LEN        27

/* Seek origins */
#define SEEK_SET               0
#define SEEK_CUR               1
#define SEEK_END               2

/* File types */
#define FS_TYPE_UNUSED         0
#define FS_TYPE_FILE           1
#define FS_TYPE_DIR            2

/* Open flags */
#define O_RDONLY               0x0001
#define O_WRONLY               0x0002
#define O_RDWR                 0x0003
#define O_CREAT                0x0040
#define O_TRUNC                0x0200
#define O_APPEND               0x0400

#define MAX_OPEN_FILES         32

/* On-disk Superblock (1024 bytes) */
typedef struct {
    uint32_t magic;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t total_inodes;
    uint32_t free_blocks;
    uint32_t free_inodes;
    uint32_t inode_bitmap_block;
    uint32_t block_bitmap_block;
    uint32_t inode_table_block;
    uint32_t inode_table_blocks;
    uint32_t data_blocks_start;
    uint32_t root_inode;
    char     volume_name[32];
    uint8_t  padding[964];
} __attribute__((packed)) fs_superblock_t;

/* On-disk Inode (64 bytes) */
typedef struct {
    uint32_t id;
    uint16_t type;
    uint16_t permissions;
    uint32_t size;
    uint32_t block_count;
    uint32_t direct[FS_DIRECT_BLOCKS];
    uint32_t indirect;
    uint32_t double_indirect;
    uint32_t created_time;
    uint32_t modified_time;
} __attribute__((packed)) fs_inode_t;

/* On-disk Directory Entry (32 bytes) */
typedef struct {
    uint32_t inode_id;
    uint8_t  type;
    char     name[FS_MAX_NAME_LEN];
} __attribute__((packed)) fs_dirent_t;

/* User-facing file statistics */
typedef struct {
    uint32_t inode_id;
    uint16_t type;
    uint32_t size;
    uint32_t block_count;
} fs_stat_t;

/* User-facing directory listing item */
typedef struct {
    uint32_t inode_id;
    uint8_t  type;
    uint32_t size;
    char     name[28];
} fs_user_dirent_t;

/* Filesystem List Operation Detailed Telemetry */
typedef struct {
    char     requested_path[64];
    uint32_t resolved_dir_inode;
    uint16_t resolved_type;
    uint32_t inode_block;
    uint32_t inode_lba;
    uint32_t dir_data_block;
    uint32_t dir_data_lba;
    uint32_t dir_size;
    uint32_t direct_blocks_used;
    uint32_t entries_scanned;
    uint32_t entries_valid;
    uint32_t entries_free;
    uint32_t records_marshalled;
    uint32_t bytes_marshalled;
    int      error_step; /* 0=OK, 1=not mounted, 2=path resolution, 3=not directory, 4=inode read fail */
    const char *error_msg;
} fs_list_telemetry_t;

void fs_get_last_list_telemetry(fs_list_telemetry_t *out);

/* Kernel In-Memory File Descriptor */
typedef struct {
    bool     used;
    uint32_t inode_id;
    uint32_t offset;
    int      flags;
} file_descriptor_t;

/* Filesystem Core API */
int  fs_init(void);
int  fs_format(void);
int  fs_create(const char *path, uint16_t type);
int  fs_open(const char *path, int flags);
int  fs_close(int fd);
int  fs_read(int fd, void *buf, size_t count);
int  fs_write(int fd, const void *buf, size_t count);
int  fs_seek(int fd, int32_t offset, int whence);
int  fs_unlink(const char *path);
int  fs_stat(const char *path, fs_stat_t *st);
int  fs_list(const char *path, fs_user_dirent_t *list, size_t max_items);
int  fs_read_file(const char *path, void *buf, size_t max_len);
int  fs_mkdir(const char *path);
int  fs_chdir(const char *path);
int  fs_getcwd(char *buf, size_t max_len);
int  fs_resolve_path(const char *path, uint32_t *out_inode, uint16_t *out_type);

#endif /* _KERNEL_FS_FS_H */
