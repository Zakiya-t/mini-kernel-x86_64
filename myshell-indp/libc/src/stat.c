#include "sys/stat.h"
#include "syscall.h"
#include "string.h"
#include "errno.h"

int stat(const char *pathname, struct stat *statbuf) {
    if (!pathname || !statbuf) return -1;

    memset(statbuf, 0, sizeof(struct stat));

    if (strcmp(pathname, "/") == 0 || strcmp(pathname, ".") == 0 || strcmp(pathname, "..") == 0) {
        statbuf->st_mode = S_IFDIR | 0755;
        statbuf->st_size = 4096;
        return 0;
    }

    if (strcmp(pathname, "/proc") == 0) {
        statbuf->st_mode = S_IFDIR | 0555;
        statbuf->st_size = 0;
        return 0;
    }

    user_fs_stat_t raw_st;
    memset(&raw_st, 0, sizeof(raw_st));
    int64_t ret = sys_call(SYS_FS_STAT, (uint64_t)pathname, (uint64_t)(uintptr_t)&raw_st, 0, 0);
    if (ret < 0) {
        errno = ENOENT;
        return -1;
    }

    statbuf->st_ino = raw_st.inode_id;
    statbuf->st_size = raw_st.size;
    if (raw_st.type == USER_FS_TYPE_DIR) {
        statbuf->st_mode = S_IFDIR | 0755;
    } else {
        statbuf->st_mode = S_IFREG | 0644;
    }
    return 0;
}

int mkdir(const char *pathname, mode_t mode) {
    (void)mode;
    if (!pathname) return -1;
    int64_t ret = sys_call(SYS_MKDIR, (uint64_t)pathname, 0, 0, 0);
    if (ret < 0) {
        errno = EEXIST;
        return -1;
    }
    return 0;
}

