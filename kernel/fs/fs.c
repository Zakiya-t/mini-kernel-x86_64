#include "fs/fs.h"
#include "drivers/ata.h"
#include "core/kprintf.h"
#include "memory/heap.h"
#include "string.h"
#include "process/process.h"

static fs_superblock_t active_sb;
static file_descriptor_t fd_table[MAX_OPEN_FILES];
static bool fs_mounted = false;
static fs_list_telemetry_t last_list_telemetry = {0};

void fs_get_last_list_telemetry(fs_list_telemetry_t *out) {
    if (out) {
        *out = last_list_telemetry;
    }
}

/* Read 1024-byte filesystem block from disk (2 ATA sectors) */
static int fs_read_block(uint32_t block_idx, void *buf) {
    uint32_t lba = FS_START_LBA + (block_idx * FS_SECTORS_PER_BLOCK);
    return ata_read_sectors(lba, FS_SECTORS_PER_BLOCK, buf);
}

/* Write 1024-byte filesystem block to disk (2 ATA sectors) */
static int fs_write_block(uint32_t block_idx, const void *buf) {
    uint32_t lba = FS_START_LBA + (block_idx * FS_SECTORS_PER_BLOCK);
    return ata_write_sectors(lba, FS_SECTORS_PER_BLOCK, buf);
}

/* Flush active superblock to disk (Block 0) */
static int fs_flush_superblock(void) {
    return fs_write_block(0, &active_sb);
}

/* Inode reading from on-disk Inode Table */
static int fs_read_inode(uint32_t inode_id, fs_inode_t *inode) {
    if (inode_id == 0 || inode_id > active_sb.total_inodes || !inode) {
        return -1;
    }

    uint32_t index = inode_id - 1;
    uint32_t block = active_sb.inode_table_block + (index / FS_INODES_PER_BLOCK);
    uint32_t offset = (index % FS_INODES_PER_BLOCK) * sizeof(fs_inode_t);

    uint8_t buf[FS_BLOCK_SIZE];
    if (fs_read_block(block, buf) != 0) {
        return -2;
    }

    memcpy(inode, buf + offset, sizeof(fs_inode_t));
    return 0;
}

/* Inode writing to on-disk Inode Table */
static int fs_write_inode(uint32_t inode_id, const fs_inode_t *inode) {
    if (inode_id == 0 || inode_id > active_sb.total_inodes || !inode) {
        return -1;
    }

    uint32_t index = inode_id - 1;
    uint32_t block = active_sb.inode_table_block + (index / FS_INODES_PER_BLOCK);
    uint32_t offset = (index % FS_INODES_PER_BLOCK) * sizeof(fs_inode_t);

    uint8_t buf[FS_BLOCK_SIZE];
    if (fs_read_block(block, buf) != 0) {
        return -2;
    }

    memcpy(buf + offset, inode, sizeof(fs_inode_t));
    return fs_write_block(block, buf);
}

/* Allocate free inode from Inode Bitmap */
static int fs_alloc_inode(void) {
    uint8_t bitmap[FS_BLOCK_SIZE];
    if (fs_read_block(active_sb.inode_bitmap_block, bitmap) != 0) {
        return -1;
    }

    for (uint32_t i = 0; i < active_sb.total_inodes; i++) {
        uint32_t byte_idx = i / 8;
        uint8_t  bit_mask = 1 << (i % 8);

        if (!(bitmap[byte_idx] & bit_mask)) {
            /* Mark allocated */
            bitmap[byte_idx] |= bit_mask;
            fs_write_block(active_sb.inode_bitmap_block, bitmap);

            active_sb.free_inodes--;
            fs_flush_superblock();

            return (int)(i + 1); /* Inode IDs are 1-indexed */
        }
    }

    return -1; /* Out of inodes */
}

/* Free inode back to Inode Bitmap */
static int fs_free_inode(uint32_t inode_id) {
    if (inode_id == 0 || inode_id > active_sb.total_inodes) {
        return -1;
    }

    uint8_t bitmap[FS_BLOCK_SIZE];
    if (fs_read_block(active_sb.inode_bitmap_block, bitmap) != 0) {
        return -1;
    }

    uint32_t i = inode_id - 1;
    uint32_t byte_idx = i / 8;
    uint8_t  bit_mask = 1 << (i % 8);

    bitmap[byte_idx] &= ~bit_mask;
    fs_write_block(active_sb.inode_bitmap_block, bitmap);

    active_sb.free_inodes++;
    fs_flush_superblock();
    return 0;
}

/* Allocate free data block from Block Bitmap */
static int fs_alloc_data_block(void) {
    uint8_t bitmap[FS_BLOCK_SIZE];
    if (fs_read_block(active_sb.block_bitmap_block, bitmap) != 0) {
        return -1;
    }

    uint32_t total_data_blocks = active_sb.total_blocks - active_sb.data_blocks_start;
    for (uint32_t i = 0; i < total_data_blocks; i++) {
        uint32_t byte_idx = i / 8;
        uint8_t  bit_mask = 1 << (i % 8);

        if (!(bitmap[byte_idx] & bit_mask)) {
            bitmap[byte_idx] |= bit_mask;
            fs_write_block(active_sb.block_bitmap_block, bitmap);

            active_sb.free_blocks--;
            fs_flush_superblock();

            uint32_t block_idx = active_sb.data_blocks_start + i;

            /* Zero the newly allocated data block */
            uint8_t zero[FS_BLOCK_SIZE];
            memset(zero, 0, sizeof(zero));
            fs_write_block(block_idx, zero);

            return (int)block_idx;
        }
    }

    return -1; /* Out of disk blocks */
}

/* Free data block back to Block Bitmap */
static int fs_free_data_block(uint32_t block_idx) {
    if (block_idx < active_sb.data_blocks_start || block_idx >= active_sb.total_blocks) {
        return -1;
    }

    uint8_t bitmap[FS_BLOCK_SIZE];
    if (fs_read_block(active_sb.block_bitmap_block, bitmap) != 0) {
        return -1;
    }

    uint32_t i = block_idx - active_sb.data_blocks_start;
    uint32_t byte_idx = i / 8;
    uint8_t  bit_mask = 1 << (i % 8);

    bitmap[byte_idx] &= ~bit_mask;
    fs_write_block(active_sb.block_bitmap_block, bitmap);

    active_sb.free_blocks++;
    fs_flush_superblock();
    return 0;
}

/* Format disk volume with fresh MiniFS file system */
int fs_format(void) {
    KLOG_INFO("MiniFS: Formatting persistent volume on ATA drive...");

    /* 1. Initialize Superblock */
    memset(&active_sb, 0, sizeof(active_sb));
    active_sb.magic               = FS_MAGIC;
    active_sb.block_size          = FS_BLOCK_SIZE;
    active_sb.total_blocks        = FS_TOTAL_BLOCKS;
    active_sb.total_inodes        = FS_TOTAL_INODES;
    active_sb.free_blocks         = FS_TOTAL_BLOCKS - FS_DATA_BLOCKS_START;
    active_sb.free_inodes         = FS_TOTAL_INODES;
    active_sb.inode_bitmap_block  = FS_INODE_BITMAP_BLOCK;
    active_sb.block_bitmap_block  = FS_BLOCK_BITMAP_BLOCK;
    active_sb.inode_table_block   = FS_INODE_TABLE_START;
    active_sb.inode_table_blocks  = FS_INODE_TABLE_BLOCKS;
    active_sb.data_blocks_start   = FS_DATA_BLOCKS_START;
    active_sb.root_inode          = FS_ROOT_INODE;
    strncpy(active_sb.volume_name, "MINIFS_SYSTEM", sizeof(active_sb.volume_name) - 1);

    /* 2. Zero Inode and Block Bitmaps */
    uint8_t zero_block[FS_BLOCK_SIZE];
    memset(zero_block, 0, sizeof(zero_block));
    fs_write_block(active_sb.inode_bitmap_block, zero_block);
    fs_write_block(active_sb.block_bitmap_block, zero_block);

    /* 3. Zero Inode Table Blocks */
    for (uint32_t b = 0; b < active_sb.inode_table_blocks; b++) {
        fs_write_block(active_sb.inode_table_block + b, zero_block);
    }

    /* 4. Allocate Root Directory (Inode 1) */
    int root_inode_id = fs_alloc_inode();
    if (root_inode_id != 1) {
        KLOG_ERR("fs_format: Failed to allocate Root Inode (got %d)", root_inode_id);
        return -1;
    }

    /* Allocate initial data block for root directory */
    int root_block = fs_alloc_data_block();
    if (root_block < 0) {
        KLOG_ERR("fs_format: Failed to allocate root data block");
        return -2;
    }

    fs_inode_t root_inode;
    memset(&root_inode, 0, sizeof(root_inode));
    root_inode.id          = 1;
    root_inode.type        = FS_TYPE_DIR;
    root_inode.permissions = 0755;
    root_inode.size        = 2 * sizeof(fs_dirent_t);
    root_inode.block_count = 1;
    root_inode.direct[0]   = (uint32_t)root_block;

    /* Write '.' and '..' entries */
    fs_dirent_t root_entries[2];
    memset(root_entries, 0, sizeof(root_entries));
    root_entries[0].inode_id = 1;
    root_entries[0].type     = FS_TYPE_DIR;
    strcpy(root_entries[0].name, ".");

    root_entries[1].inode_id = 1;
    root_entries[1].type     = FS_TYPE_DIR;
    strcpy(root_entries[1].name, "..");

    uint8_t dir_buf[FS_BLOCK_SIZE];
    memset(dir_buf, 0, sizeof(dir_buf));
    memcpy(dir_buf, root_entries, sizeof(root_entries));
    fs_write_block((uint32_t)root_block, dir_buf);

    fs_write_inode(1, &root_inode);

    /* 5. Write Superblock */
    fs_flush_superblock();

    fs_mounted = true;
    KLOG_OK("MiniFS: Volume formatted successfully (16 MiB, %u inodes, %u blocks)",
            active_sb.total_inodes, active_sb.total_blocks);
    return 0;
}

int fs_init(void) {
    for (size_t i = 0; i < MAX_OPEN_FILES; i++) {
        fd_table[i].used = false;
    }

    /* 1. Initialize underlying ATA hard disk driver */
    if (ata_init() != 0) {
        KLOG_WARN("MiniFS: ATA initialization failed, filesystem offline");
        return -1;
    }

    /* 2. Read Superblock from Block 0 */
    if (fs_read_block(0, &active_sb) != 0) {
        KLOG_ERR("MiniFS: Failed to read superblock block from disk");
        return -2;
    }

    /* 3. Validate Superblock signature */
    if (active_sb.magic == FS_MAGIC && active_sb.block_size == FS_BLOCK_SIZE) {
        fs_mounted = true;
        KLOG_OK("MiniFS: Mounted volume \"%s\" (Capacity: 16 MiB, Free: %u blocks, %u inodes)",
                active_sb.volume_name, active_sb.free_blocks, active_sb.free_inodes);
        return 0;
    }

    /* Magic does not match -> First boot or unformatted disk */
    KLOG_INFO("MiniFS: No existing filesystem found (Magic: 0x%08x). Formatting...", active_sb.magic);
    return fs_format();
}

/* Extract next path component (skipping slashes).
 * Returns pointer to remainder of path, or NULL when exhausted.
 */
static const char *path_next_component(const char *path, char *comp_buf, size_t max_len) {
    if (!path) return NULL;
    while (*path == '/') path++;
    if (*path == '\0') return NULL;

    size_t i = 0;
    while (*path && *path != '/') {
        if (i + 1 < max_len) {
            comp_buf[i++] = *path;
        }
        path++;
    }
    comp_buf[i] = '\0';
    while (*path == '/') path++;
    return path;
}

/* Search for an entry in any directory inode */
static int find_in_dir(uint32_t dir_inode_id, const char *name, fs_dirent_t *out_entry, uint32_t *out_block_idx, uint32_t *out_entry_idx) {
    fs_inode_t dir;
    if (fs_read_inode(dir_inode_id, &dir) != 0) return -1;
    if (dir.type != FS_TYPE_DIR) return -20; /* ENOTDIR */

    for (size_t b = 0; b < FS_DIRECT_BLOCKS; b++) {
        uint32_t block = dir.direct[b];
        if (block == 0) continue;

        uint8_t buf[FS_BLOCK_SIZE];
        if (fs_read_block(block, buf) != 0) continue;

        fs_dirent_t *entries = (fs_dirent_t *)buf;
        size_t entries_per_block = FS_BLOCK_SIZE / sizeof(fs_dirent_t);

        for (size_t i = 0; i < entries_per_block; i++) {
            if (entries[i].inode_id != 0 && strcmp(entries[i].name, name) == 0) {
                if (out_entry) *out_entry = entries[i];
                if (out_block_idx) *out_block_idx = block;
                if (out_entry_idx) *out_entry_idx = (uint32_t)i;
                return 0;
            }
        }
    }

    return -2; /* ENOENT */
}

/* Add directory entry to any directory inode */
static int add_to_dir(uint32_t dir_inode_id, uint32_t inode_id, uint8_t type, const char *name) {
    fs_inode_t dir;
    if (fs_read_inode(dir_inode_id, &dir) != 0) return -1;
    if (dir.type != FS_TYPE_DIR) return -20; /* ENOTDIR */

    size_t entries_per_block = FS_BLOCK_SIZE / sizeof(fs_dirent_t);

    for (size_t b = 0; b < FS_DIRECT_BLOCKS; b++) {
        uint32_t block = dir.direct[b];
        if (block == 0) {
            /* Allocate new direct block for directory */
            int new_block = fs_alloc_data_block();
            if (new_block < 0) return -28; /* ENOSPC */

            block = (uint32_t)new_block;
            dir.direct[b] = block;
            dir.block_count++;
        }

        uint8_t buf[FS_BLOCK_SIZE];
        if (fs_read_block(block, buf) != 0) return -3;

        fs_dirent_t *entries = (fs_dirent_t *)buf;
        for (size_t i = 0; i < entries_per_block; i++) {
            if (entries[i].inode_id == 0) {
                entries[i].inode_id = inode_id;
                entries[i].type     = type;
                strncpy(entries[i].name, name, sizeof(entries[i].name) - 1);
                entries[i].name[sizeof(entries[i].name) - 1] = '\0';

                fs_write_block(block, buf);

                dir.size += sizeof(fs_dirent_t);
                fs_write_inode(dir_inode_id, &dir);
                return 0;
            }
        }
    }

    return -4; /* Directory block table full */
}

/* Remove directory entry from a directory inode */
static int remove_from_dir(uint32_t dir_inode_id, const char *name) {
    fs_inode_t dir;
    if (fs_read_inode(dir_inode_id, &dir) != 0) return -1;

    size_t entries_per_block = FS_BLOCK_SIZE / sizeof(fs_dirent_t);

    for (size_t b = 0; b < FS_DIRECT_BLOCKS; b++) {
        uint32_t block = dir.direct[b];
        if (block == 0) continue;

        uint8_t buf[FS_BLOCK_SIZE];
        if (fs_read_block(block, buf) != 0) continue;

        fs_dirent_t *entries = (fs_dirent_t *)buf;
        for (size_t i = 0; i < entries_per_block; i++) {
            if (entries[i].inode_id != 0 && strcmp(entries[i].name, name) == 0) {
                entries[i].inode_id = 0;
                entries[i].name[0]  = '\0';
                fs_write_block(block, buf);

                if (dir.size >= sizeof(fs_dirent_t)) {
                    dir.size -= sizeof(fs_dirent_t);
                }
                fs_write_inode(dir_inode_id, &dir);
                return 0;
            }
        }
    }

    return -2; /* Not found */
}

/* Resolve any path (absolute or relative to process CWD) to its target inode */
int fs_resolve_path(const char *path, uint32_t *out_inode, uint16_t *out_type) {
    process_t *curr = process_get_current();
    uint32_t cwd = (curr && curr->cwd_inode) ? curr->cwd_inode : FS_ROOT_INODE;

    if (!path || *path == '\0') {
        if (out_inode) *out_inode = cwd;
        if (out_type) {
            fs_inode_t ino;
            if (fs_read_inode(cwd, &ino) == 0) *out_type = ino.type;
            else *out_type = FS_TYPE_DIR;
        }
        return 0;
    }

    uint32_t curr_inode = FS_ROOT_INODE;
    const char *p = path;
    if (*p == '/') {
        curr_inode = FS_ROOT_INODE;
        while (*p == '/') p++;
        if (*p == '\0') {
            if (out_inode) *out_inode = FS_ROOT_INODE;
            if (out_type) *out_type = FS_TYPE_DIR;
            return 0;
        }
    } else {
        curr_inode = cwd;
    }

    char comp[FS_MAX_NAME_LEN];
    uint16_t curr_type = FS_TYPE_DIR;

    while ((p = path_next_component(p, comp, sizeof(comp))) != NULL) {
        if (strcmp(comp, ".") == 0) {
            continue; /* Current directory reference */
        }

        if (curr_type != FS_TYPE_DIR) {
            return -20; /* ENOTDIR */
        }

        fs_dirent_t ent;
        int res = find_in_dir(curr_inode, comp, &ent, NULL, NULL);
        if (res != 0) {
            return -2; /* ENOENT */
        }

        curr_inode = ent.inode_id;
        curr_type  = ent.type;
    }

    if (out_inode) *out_inode = curr_inode;
    if (out_type)  *out_type  = curr_type;
    return 0;
}

/* Resolve path into parent directory inode and final basename */
static int fs_resolve_parent(const char *path, uint32_t *out_parent_inode, char *out_basename, size_t max_base_len) {
    if (!path || *path == '\0' || !out_parent_inode || !out_basename || max_base_len == 0) {
        return -22; /* EINVAL */
    }

    process_t *curr = process_get_current();
    uint32_t cwd = (curr && curr->cwd_inode) ? curr->cwd_inode : FS_ROOT_INODE;

    char temp[128];
    strncpy(temp, path, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    size_t len = strlen(temp);
    while (len > 1 && temp[len - 1] == '/') {
        temp[len - 1] = '\0';
        len--;
    }

    if (strcmp(temp, "/") == 0) {
        return -22; /* Root directory cannot be created or deleted */
    }

    char *last_slash = strrchr(temp, '/');
    if (!last_slash) {
        /* Relative name in CWD: e.g. "proj" */
        *out_parent_inode = cwd;
        strncpy(out_basename, temp, max_base_len - 1);
        out_basename[max_base_len - 1] = '\0';
    } else if (last_slash == temp) {
        /* Leading slash only: e.g. "/proj" */
        *out_parent_inode = FS_ROOT_INODE;
        strncpy(out_basename, last_slash + 1, max_base_len - 1);
        out_basename[max_base_len - 1] = '\0';
    } else {
        /* Hierarchical path: e.g. "foo/bar" or "/foo/bar" */
        *last_slash = '\0';
        const char *parent_path = temp;
        const char *base = last_slash + 1;

        uint32_t p_ino = 0;
        uint16_t p_type = 0;
        int res = fs_resolve_path(parent_path, &p_ino, &p_type);
        if (res != 0) return res;
        if (p_type != FS_TYPE_DIR) return -20; /* ENOTDIR */

        *out_parent_inode = p_ino;
        strncpy(out_basename, base, max_base_len - 1);
        out_basename[max_base_len - 1] = '\0';
    }

    if (strcmp(out_basename, ".") == 0 || strcmp(out_basename, "..") == 0 || out_basename[0] == '\0') {
        return -22; /* Invalid basename */
    }

    return 0;
}

/* Canonicalize relative or absolute path into normalized absolute path (e.g. /proj/sub) */
static void fs_canonicalize_path(const char *base, const char *rel, char *out, size_t max_len) {
    if (!out || max_len == 0) return;

    char full[256];
    full[0] = '\0';

    if (!rel || *rel == '\0') {
        if (base && *base) {
            strncpy(out, base, max_len - 1);
            out[max_len - 1] = '\0';
        } else {
            strncpy(out, "/", max_len - 1);
            out[max_len - 1] = '\0';
        }
        return;
    }

    if (rel[0] == '/') {
        strncpy(full, rel, sizeof(full) - 1);
        full[sizeof(full) - 1] = '\0';
    } else {
        if (!base || base[0] == '\0' || strcmp(base, "/") == 0) {
            full[0] = '/';
            full[1] = '\0';
            strncat(full, rel, sizeof(full) - 2);
        } else {
            strncpy(full, base, sizeof(full) - 1);
            full[sizeof(full) - 1] = '\0';
            strncat(full, "/", sizeof(full) - strlen(full) - 1);
            strncat(full, rel, sizeof(full) - strlen(full) - 1);
        }
    }

    char comps[16][32];
    int depth = 0;

    const char *p = full;
    while (*p) {
        while (*p == '/') p++;
        if (*p == '\0') break;

        char comp[32];
        size_t c_idx = 0;
        while (*p && *p != '/') {
            if (c_idx + 1 < sizeof(comp)) {
                comp[c_idx++] = *p;
            }
            p++;
        }
        comp[c_idx] = '\0';

        if (strcmp(comp, ".") == 0) {
            continue;
        } else if (strcmp(comp, "..") == 0) {
            if (depth > 0) {
                depth--;
            }
        } else {
            if (depth < 16) {
                strncpy(comps[depth], comp, sizeof(comps[depth]) - 1);
                comps[depth][sizeof(comps[depth]) - 1] = '\0';
                depth++;
            }
        }
    }

    if (depth == 0) {
        strncpy(out, "/", max_len - 1);
        out[max_len - 1] = '\0';
        return;
    }

    out[0] = '\0';
    for (int i = 0; i < depth; i++) {
        strncat(out, "/", max_len - strlen(out) - 1);
        strncat(out, comps[i], max_len - strlen(out) - 1);
    }
}

/* Create a new directory on disk with '.' and '..' */
int fs_mkdir(const char *path) {
    if (!fs_mounted) return -1;
    if (!path || *path == '\0') return -22;

    uint32_t parent_inode = 0;
    char basename[FS_MAX_NAME_LEN];
    int res = fs_resolve_parent(path, &parent_inode, basename, sizeof(basename));
    if (res != 0) return res;

    /* Check if entry already exists in parent directory */
    if (find_in_dir(parent_inode, basename, NULL, NULL, NULL) == 0) {
        return -17; /* EEXIST */
    }

    /* Allocate free inode */
    int new_inode_id = fs_alloc_inode();
    if (new_inode_id < 0) return -28; /* ENOSPC */

    /* Allocate initial data block for directory */
    int dir_block = fs_alloc_data_block();
    if (dir_block < 0) {
        fs_free_inode((uint32_t)new_inode_id);
        return -28; /* ENOSPC */
    }

    /* Initialize '.' and '..' entries */
    fs_dirent_t dot_entries[2];
    memset(dot_entries, 0, sizeof(dot_entries));
    dot_entries[0].inode_id = (uint32_t)new_inode_id;
    dot_entries[0].type     = FS_TYPE_DIR;
    strcpy(dot_entries[0].name, ".");

    dot_entries[1].inode_id = parent_inode;
    dot_entries[1].type     = FS_TYPE_DIR;
    strcpy(dot_entries[1].name, "..");

    uint8_t dir_buf[FS_BLOCK_SIZE];
    memset(dir_buf, 0, sizeof(dir_buf));
    memcpy(dir_buf, dot_entries, sizeof(dot_entries));
    fs_write_block((uint32_t)dir_block, dir_buf);

    /* Initialize directory inode */
    fs_inode_t inode;
    memset(&inode, 0, sizeof(inode));
    inode.id          = (uint32_t)new_inode_id;
    inode.type        = FS_TYPE_DIR;
    inode.permissions = 0755;
    inode.size        = 2 * sizeof(fs_dirent_t);
    inode.block_count = 1;
    inode.direct[0]   = (uint32_t)dir_block;

    if (fs_write_inode((uint32_t)new_inode_id, &inode) != 0) {
        fs_free_data_block((uint32_t)dir_block);
        fs_free_inode((uint32_t)new_inode_id);
        return -5;
    }

    /* Add entry to parent directory */
    res = add_to_dir(parent_inode, (uint32_t)new_inode_id, FS_TYPE_DIR, basename);
    if (res != 0) {
        fs_free_data_block((uint32_t)dir_block);
        fs_free_inode((uint32_t)new_inode_id);
        return res;
    }

    KLOG_INFO("MiniFS: Created directory \"%s\" (Inode %u in parent %u)",
              basename, new_inode_id, parent_inode);
    return 0;
}

/* Change process current working directory */
int fs_chdir(const char *path) {
    if (!fs_mounted) return -1;
    process_t *curr = process_get_current();
    if (!curr) return -1;

    if (!path || *path == '\0' || strcmp(path, "~") == 0) {
        curr->cwd_inode = FS_ROOT_INODE;
        strcpy(curr->cwd_path, "/");
        return 0;
    }

    uint32_t target_inode = 0;
    uint16_t target_type = 0;
    int res = fs_resolve_path(path, &target_inode, &target_type);
    if (res != 0) return res; /* -2 ENOENT */
    if (target_type != FS_TYPE_DIR) return -20; /* -20 ENOTDIR */

    char canonical[128];
    fs_canonicalize_path(curr->cwd_path, path, canonical, sizeof(canonical));

    curr->cwd_inode = target_inode;
    strncpy(curr->cwd_path, canonical, sizeof(curr->cwd_path) - 1);
    curr->cwd_path[sizeof(curr->cwd_path) - 1] = '\0';

    return 0;
}

/* Retrieve process current working directory path string */
int fs_getcwd(char *buf, size_t max_len) {
    if (!buf || max_len == 0) return -1;
    process_t *curr = process_get_current();
    if (!curr || curr->cwd_path[0] == '\0') {
        strncpy(buf, "/", max_len - 1);
        buf[max_len - 1] = '\0';
        return 0;
    }
    strncpy(buf, curr->cwd_path, max_len - 1);
    buf[max_len - 1] = '\0';
    return 0;
}

int fs_create(const char *path, uint16_t type) {
    if (!fs_mounted) return -1;
    if (!path || *path == '\0') return -22;

    uint32_t parent_inode = 0;
    char basename[FS_MAX_NAME_LEN];
    int res = fs_resolve_parent(path, &parent_inode, basename, sizeof(basename));
    if (res != 0) return res;

    /* Check if already exists in parent directory */
    if (find_in_dir(parent_inode, basename, NULL, NULL, NULL) == 0) {
        return -3; /* Already exists */
    }

    /* Allocate free inode */
    int inode_id = fs_alloc_inode();
    if (inode_id < 0) return -4; /* Out of inodes */

    /* Initialize inode */
    fs_inode_t inode;
    memset(&inode, 0, sizeof(inode));
    inode.id          = (uint32_t)inode_id;
    inode.type        = type;
    inode.permissions = (type == FS_TYPE_DIR) ? 0755 : 0644;
    inode.size        = 0;
    inode.block_count = 0;

    if (fs_write_inode((uint32_t)inode_id, &inode) != 0) {
        fs_free_inode((uint32_t)inode_id);
        return -5;
    }

    /* Add entry to parent directory */
    if (add_to_dir(parent_inode, (uint32_t)inode_id, (uint8_t)type, basename) != 0) {
        fs_free_inode((uint32_t)inode_id);
        return -6;
    }

    KLOG_INFO("MiniFS: Created %s \"%s\" (Inode %u in parent %u)",
              type == FS_TYPE_DIR ? "directory" : "file", basename, inode_id, parent_inode);
    return 0;
}

/* Free all data and index blocks associated with an inode */
static void fs_free_inode_blocks(fs_inode_t *inode) {
    if (!inode) return;

    /* 1. Free direct blocks */
    for (size_t b = 0; b < FS_DIRECT_BLOCKS; b++) {
        if (inode->direct[b] != 0) {
            fs_free_data_block(inode->direct[b]);
            inode->direct[b] = 0;
        }
    }

    /* 2. Free single indirect block and its data blocks */
    if (inode->indirect != 0) {
        uint32_t ind_buf[FS_PTRS_PER_BLOCK];
        if (fs_read_block(inode->indirect, ind_buf) == 0) {
            for (size_t i = 0; i < FS_PTRS_PER_BLOCK; i++) {
                if (ind_buf[i] != 0) {
                    fs_free_data_block(ind_buf[i]);
                }
            }
        }
        fs_free_data_block(inode->indirect);
        inode->indirect = 0;
    }

    /* 3. Free double indirect block and all indirect and data blocks */
    if (inode->double_indirect != 0) {
        uint32_t dind_buf[FS_PTRS_PER_BLOCK];
        if (fs_read_block(inode->double_indirect, dind_buf) == 0) {
            for (size_t i = 0; i < FS_PTRS_PER_BLOCK; i++) {
                if (dind_buf[i] != 0) {
                    uint32_t ind2_buf[FS_PTRS_PER_BLOCK];
                    if (fs_read_block(dind_buf[i], ind2_buf) == 0) {
                        for (size_t j = 0; j < FS_PTRS_PER_BLOCK; j++) {
                            if (ind2_buf[j] != 0) {
                                fs_free_data_block(ind2_buf[j]);
                            }
                        }
                    }
                    fs_free_data_block(dind_buf[i]);
                }
            }
        }
        fs_free_data_block(inode->double_indirect);
        inode->double_indirect = 0;
    }

    inode->size = 0;
    inode->block_count = 0;
}

/*
 * fs_bmap: Maps a logical block index within a file (0, 1, 2, ...) to a physical disk block.
 * If allocate is true, missing intermediate index blocks and data blocks are allocated and saved.
 * Returns the physical block number, or 0 if unallocated / error.
 */
static uint32_t fs_bmap(fs_inode_t *inode, uint32_t logical_block, bool allocate) {
    if (!inode) return 0;

    /* 1. Direct blocks (0 .. 7) */
    if (logical_block < FS_DIRECT_BLOCKS) {
        if (inode->direct[logical_block] != 0) {
            return inode->direct[logical_block];
        }
        if (!allocate) return 0;

        int new_b = fs_alloc_data_block();
        if (new_b < 0) return 0;
        inode->direct[logical_block] = (uint32_t)new_b;
        inode->block_count++;
        return (uint32_t)new_b;
    }

    logical_block -= FS_DIRECT_BLOCKS;

    /* 2. Single indirect blocks (8 .. 263 -> logical_block 0 .. 255) */
    if (logical_block < FS_SINGLE_INDIRECT_BLOCKS) {
        if (inode->indirect == 0) {
            if (!allocate) return 0;
            int new_ind = fs_alloc_data_block();
            if (new_ind < 0) return 0;
            inode->indirect = (uint32_t)new_ind;
            inode->block_count++;
        }

        uint32_t ind_buf[FS_PTRS_PER_BLOCK];
        if (fs_read_block(inode->indirect, ind_buf) != 0) {
            return 0;
        }

        if (ind_buf[logical_block] != 0) {
            return ind_buf[logical_block];
        }

        if (!allocate) return 0;

        int new_data = fs_alloc_data_block();
        if (new_data < 0) return 0;
        ind_buf[logical_block] = (uint32_t)new_data;
        inode->block_count++;
        fs_write_block(inode->indirect, ind_buf);
        return (uint32_t)new_data;
    }

    logical_block -= FS_SINGLE_INDIRECT_BLOCKS;

    /* 3. Double indirect blocks (264 .. 65799 -> logical_block 0 .. 65535) */
    if (logical_block < FS_DOUBLE_INDIRECT_BLOCKS) {
        uint32_t idx1 = logical_block / FS_PTRS_PER_BLOCK;
        uint32_t idx2 = logical_block % FS_PTRS_PER_BLOCK;

        if (inode->double_indirect == 0) {
            if (!allocate) return 0;
            int new_dind = fs_alloc_data_block();
            if (new_dind < 0) return 0;
            inode->double_indirect = (uint32_t)new_dind;
            inode->block_count++;
        }

        uint32_t dind_buf[FS_PTRS_PER_BLOCK];
        if (fs_read_block(inode->double_indirect, dind_buf) != 0) {
            return 0;
        }

        if (dind_buf[idx1] == 0) {
            if (!allocate) return 0;
            int new_ind2 = fs_alloc_data_block();
            if (new_ind2 < 0) return 0;
            dind_buf[idx1] = (uint32_t)new_ind2;
            inode->block_count++;
            fs_write_block(inode->double_indirect, dind_buf);
        }

        uint32_t ind2_buf[FS_PTRS_PER_BLOCK];
        if (fs_read_block(dind_buf[idx1], ind2_buf) != 0) {
            return 0;
        }

        if (ind2_buf[idx2] != 0) {
            return ind2_buf[idx2];
        }

        if (!allocate) return 0;

        int new_data = fs_alloc_data_block();
        if (new_data < 0) return 0;
        ind2_buf[idx2] = (uint32_t)new_data;
        inode->block_count++;
        fs_write_block(dind_buf[idx1], ind2_buf);
        return (uint32_t)new_data;
    }

    return 0;
}

int fs_open(const char *path, int flags) {
    if (!fs_mounted || !path || *path == '\0') return -1;

    uint32_t inode_id = 0;
    uint16_t type = 0;
    int res = fs_resolve_path(path, &inode_id, &type);

    if (res != 0) {
        if (flags & O_CREAT) {
            int create_res = fs_create(path, FS_TYPE_FILE);
            if (create_res != 0) return -3;
            res = fs_resolve_path(path, &inode_id, &type);
            if (res != 0) return -4;
        } else {
            return -2; /* File not found */
        }
    }

    /* Find free slot in file descriptor table (fd 0,1,2 reserved for stdio) */
    int fd = -1;
    for (int i = 3; i < MAX_OPEN_FILES; i++) {
        if (!fd_table[i].used) {
            fd = i;
            break;
        }
    }

    if (fd < 0) {
        return -5; /* Too many open files */
    }

    fs_inode_t inode;
    fs_read_inode(inode_id, &inode);

    if (flags & O_TRUNC) {
        /* Truncate file data blocks */
        fs_free_inode_blocks(&inode);
        fs_write_inode(inode_id, &inode);
    }

    fd_table[fd].used     = true;
    fd_table[fd].inode_id = inode_id;
    fd_table[fd].offset   = (flags & O_APPEND) ? inode.size : 0;
    fd_table[fd].flags    = flags;

    return fd;
}

int fs_close(int fd) {
    if (fd < 3 || fd >= MAX_OPEN_FILES || !fd_table[fd].used) {
        return -1;
    }
    fd_table[fd].used = false;
    return 0;
}

int fs_read(int fd, void *buf, size_t count) {
    if (fd < 3 || fd >= MAX_OPEN_FILES || !fd_table[fd].used || !buf) {
        return -1;
    }

    file_descriptor_t *desc = &fd_table[fd];
    fs_inode_t inode;
    if (fs_read_inode(desc->inode_id, &inode) != 0) {
        return -2;
    }

    if (desc->offset >= inode.size) {
        return 0; /* EOF */
    }

    if (desc->offset + count > inode.size) {
        count = inode.size - desc->offset;
    }

    uint8_t *dest = (uint8_t *)buf;
    size_t bytes_read = 0;

    while (bytes_read < count) {
        uint32_t block_idx = (desc->offset + bytes_read) / FS_BLOCK_SIZE;
        uint32_t block_off = (desc->offset + bytes_read) % FS_BLOCK_SIZE;

        if (block_idx >= FS_MAX_FILE_BLOCKS) {
            break; /* Exceeds maximum file capacity */
        }

        uint32_t phys_block = fs_bmap(&inode, block_idx, false);
        size_t chunk = FS_BLOCK_SIZE - block_off;
        if (chunk > count - bytes_read) {
            chunk = count - bytes_read;
        }

        if (phys_block == 0) {
            /* Sparse hole - fill with zeroes */
            memset(dest + bytes_read, 0, chunk);
        } else {
            uint8_t block_buf[FS_BLOCK_SIZE];
            if (fs_read_block(phys_block, block_buf) != 0) {
                break;
            }
            memcpy(dest + bytes_read, block_buf + block_off, chunk);
        }

        bytes_read += chunk;
    }

    desc->offset += (uint32_t)bytes_read;
    return (int)bytes_read;
}

int fs_write(int fd, const void *buf, size_t count) {
    if (fd < 3 || fd >= MAX_OPEN_FILES || !fd_table[fd].used || !buf) {
        return -1;
    }

    file_descriptor_t *desc = &fd_table[fd];
    fs_inode_t inode;
    if (fs_read_inode(desc->inode_id, &inode) != 0) {
        return -2;
    }

    const uint8_t *src = (const uint8_t *)buf;
    size_t bytes_written = 0;

    while (bytes_written < count) {
        uint32_t block_idx = (desc->offset + bytes_written) / FS_BLOCK_SIZE;
        uint32_t block_off = (desc->offset + bytes_written) % FS_BLOCK_SIZE;

        if (block_idx >= FS_MAX_FILE_BLOCKS) {
            break; /* Exceeds maximum file capacity */
        }

        uint32_t phys_block = fs_bmap(&inode, block_idx, true);
        if (phys_block == 0) {
            break; /* Disk full or error */
        }

        uint8_t block_buf[FS_BLOCK_SIZE];
        if (block_off != 0 || (count - bytes_written) < FS_BLOCK_SIZE) {
            fs_read_block(phys_block, block_buf);
        }

        size_t chunk = FS_BLOCK_SIZE - block_off;
        if (chunk > count - bytes_written) {
            chunk = count - bytes_written;
        }

        memcpy(block_buf + block_off, src + bytes_written, chunk);
        if (fs_write_block(phys_block, block_buf) != 0) {
            break;
        }

        bytes_written += chunk;
    }

    desc->offset += (uint32_t)bytes_written;
    if (desc->offset > inode.size) {
        inode.size = desc->offset;
    }

    fs_write_inode(desc->inode_id, &inode);
    return (int)bytes_written;
}

int fs_seek(int fd, int32_t offset, int whence) {
    if (fd < 3 || fd >= MAX_OPEN_FILES || !fd_table[fd].used) {
        return -1;
    }

    file_descriptor_t *desc = &fd_table[fd];
    fs_inode_t inode;
    if (fs_read_inode(desc->inode_id, &inode) != 0) {
        return -2;
    }

    int32_t new_offset = 0;
    if (whence == SEEK_SET) {
        new_offset = offset;
    } else if (whence == SEEK_CUR) {
        new_offset = (int32_t)desc->offset + offset;
    } else if (whence == SEEK_END) {
        new_offset = (int32_t)inode.size + offset;
    } else {
        return -22; /* EINVAL */
    }

    if (new_offset < 0) {
        return -22; /* EINVAL */
    }

    desc->offset = (uint32_t)new_offset;
    return (int)desc->offset;
}

int fs_unlink(const char *path) {
    if (!fs_mounted || !path || *path == '\0') return -1;

    uint32_t parent_inode = 0;
    char basename[FS_MAX_NAME_LEN];
    int res = fs_resolve_parent(path, &parent_inode, basename, sizeof(basename));
    if (res != 0) return res;

    fs_dirent_t entry;
    uint32_t dir_block = 0;
    uint32_t entry_idx = 0;

    if (find_in_dir(parent_inode, basename, &entry, &dir_block, &entry_idx) != 0) {
        return -3; /* File not found */
    }

    /* If it is a directory, verify it contains only '.' and '..' */
    if (entry.type == FS_TYPE_DIR) {
        fs_inode_t dir_ino;
        if (fs_read_inode(entry.inode_id, &dir_ino) == 0) {
            size_t entries_per_block = FS_BLOCK_SIZE / sizeof(fs_dirent_t);
            for (size_t b = 0; b < FS_DIRECT_BLOCKS; b++) {
                if (dir_ino.direct[b] == 0) continue;
                uint8_t buf[FS_BLOCK_SIZE];
                if (fs_read_block(dir_ino.direct[b], buf) != 0) continue;
                fs_dirent_t *ents = (fs_dirent_t *)buf;
                for (size_t i = 0; i < entries_per_block; i++) {
                    if (ents[i].inode_id != 0) {
                        if (strcmp(ents[i].name, ".") != 0 && strcmp(ents[i].name, "..") != 0) {
                            return -39; /* ENOTEMPTY */
                        }
                    }
                }
            }
        }
    }

    /* 1. Free data blocks associated with inode */
    fs_inode_t inode;
    if (fs_read_inode(entry.inode_id, &inode) == 0) {
        fs_free_inode_blocks(&inode);
    }

    /* 2. Free inode */
    fs_free_inode(entry.inode_id);

    /* 3. Remove entry from parent directory */
    remove_from_dir(parent_inode, basename);

    KLOG_INFO("MiniFS: Unlinked \"%s\" from directory %u (Inode %u reclaimed)",
              basename, parent_inode, entry.inode_id);
    return 0;
}

int fs_stat(const char *path, fs_stat_t *st) {
    if (!fs_mounted || !st || !path || *path == '\0') return -1;

    uint32_t inode_id = 0;
    uint16_t type = 0;
    int res = fs_resolve_path(path, &inode_id, &type);
    if (res != 0) return res;

    fs_inode_t inode;
    if (fs_read_inode(inode_id, &inode) != 0) {
        return -4;
    }

    st->inode_id    = inode.id;
    st->type        = inode.type;
    st->size        = inode.size;
    st->block_count = inode.block_count;
    return 0;
}

int fs_list(const char *path, fs_user_dirent_t *list, size_t max_items) {
    memset(&last_list_telemetry, 0, sizeof(last_list_telemetry));
    strncpy(last_list_telemetry.requested_path, (path && *path) ? path : ".", sizeof(last_list_telemetry.requested_path) - 1);

    if (!fs_mounted) {
        last_list_telemetry.error_step = 1;
        last_list_telemetry.error_msg = "Filesystem is not mounted";
        return -1;
    }
    if (!list || max_items == 0) {
        last_list_telemetry.error_step = 5;
        last_list_telemetry.error_msg = "Invalid destination buffer or max_items = 0";
        return 0;
    }

    uint32_t dir_inode_id = FS_ROOT_INODE;
    uint16_t type = FS_TYPE_DIR;

    if (!path || *path == '\0' || strcmp(path, ".") == 0) {
        process_t *curr = process_get_current();
        dir_inode_id = (curr && curr->cwd_inode) ? curr->cwd_inode : FS_ROOT_INODE;
        last_list_telemetry.resolved_type = FS_TYPE_DIR;
    } else {
        int res = fs_resolve_path(path, &dir_inode_id, &type);
        if (res != 0) {
            last_list_telemetry.error_step = 2;
            last_list_telemetry.error_msg = (res == -2) ? "Directory path does not exist (-ENOENT)" : "VFS path resolution failed";
            return res;
        }
        if (type != FS_TYPE_DIR) {
            last_list_telemetry.error_step = 3;
            last_list_telemetry.error_msg = "Target path is a regular file, not a directory (-ENOTDIR)";
            return -20; /* ENOTDIR */
        }
        last_list_telemetry.resolved_type = type;
    }

    last_list_telemetry.resolved_dir_inode = dir_inode_id;

    /* Inode Table location */
    uint32_t inode_index = dir_inode_id - 1;
    last_list_telemetry.inode_block = active_sb.inode_table_block + (inode_index / FS_INODES_PER_BLOCK);
    last_list_telemetry.inode_lba = FS_START_LBA + (last_list_telemetry.inode_block * FS_SECTORS_PER_BLOCK);

    fs_inode_t dir;
    if (fs_read_inode(dir_inode_id, &dir) != 0) {
        last_list_telemetry.error_step = 4;
        last_list_telemetry.error_msg = "Failed reading directory inode from disk media";
        return -4;
    }

    last_list_telemetry.dir_size = dir.size;
    last_list_telemetry.direct_blocks_used = dir.block_count;
    last_list_telemetry.dir_data_block = dir.direct[0];
    last_list_telemetry.dir_data_lba = FS_START_LBA + (dir.direct[0] * FS_SECTORS_PER_BLOCK);

    size_t count = 0;
    size_t entries_per_block = FS_BLOCK_SIZE / sizeof(fs_dirent_t);

    for (size_t b = 0; b < FS_DIRECT_BLOCKS && count < max_items; b++) {
        uint32_t block = dir.direct[b];
        if (block == 0) continue;

        uint8_t buf[FS_BLOCK_SIZE];
        if (fs_read_block(block, buf) != 0) continue;

        fs_dirent_t *entries = (fs_dirent_t *)buf;
        for (size_t i = 0; i < entries_per_block && count < max_items; i++) {
            last_list_telemetry.entries_scanned++;
            if (entries[i].inode_id != 0) {
                last_list_telemetry.entries_valid++;
                list[count].inode_id = entries[i].inode_id;
                list[count].type     = entries[i].type;
                strncpy(list[count].name, entries[i].name, sizeof(list[count].name) - 1);
                list[count].name[sizeof(list[count].name) - 1] = '\0';

                /* Fetch size from inode */
                fs_inode_t file_inode;
                if (fs_read_inode(entries[i].inode_id, &file_inode) == 0) {
                    list[count].size = file_inode.size;
                } else {
                    list[count].size = 0;
                }
                count++;
            } else {
                last_list_telemetry.entries_free++;
            }
        }
    }

    last_list_telemetry.records_marshalled = (uint32_t)count;
    last_list_telemetry.bytes_marshalled = (uint32_t)(count * sizeof(fs_user_dirent_t));
    return (int)count;
}

int fs_read_file(const char *path, void *buf, size_t max_len) {
    int fd = fs_open(path, O_RDONLY);
    if (fd < 0) return fd;

    int bytes = fs_read(fd, buf, max_len);
    fs_close(fd);

    if (bytes >= 0 && (size_t)bytes < max_len) {
        ((char *)buf)[bytes] = '\0';
    }
    return bytes;
}
