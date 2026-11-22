#include "dirent.h"
#include "stdlib.h"
#include "string.h"
#include "syscall.h"
#include "errno.h"

#define MAX_DIR_ENTRIES 64

struct DIR {
    struct dirent entries[MAX_DIR_ENTRIES];
    int           count;
    int           index;
};

DIR *opendir(const char *name) {
    if (!name) return NULL;

    DIR *dir = (DIR *)malloc(sizeof(DIR));
    if (!dir) return NULL;
    dir->count = 0;
    dir->index = 0;

    /* Handle virtual /proc */
    if (strcmp(name, "/proc") == 0) {
        user_proc_info_t procs[32];
        int64_t n = sys_call(SYS_PROC_LIST, (uint64_t)(uintptr_t)procs, 32, 0, 0);
        if (n > 0) {
            for (int i = 0; i < n && dir->count < MAX_DIR_ENTRIES; i++) {
                dir->entries[dir->count].d_ino = procs[i].pid;
                dir->entries[dir->count].d_type = DT_DIR;
                char pid_str[16];
                int p = procs[i].pid;
                int pos = 0;
                if (p == 0) {
                    pid_str[pos++] = '0';
                } else {
                    char tmp[16];
                    int tpos = 0;
                    while (p > 0) {
                        tmp[tpos++] = '0' + (p % 10);
                        p /= 10;
                    }
                    while (tpos > 0) {
                        pid_str[pos++] = tmp[--tpos];
                    }
                }
                pid_str[pos] = '\0';
                strncpy(dir->entries[dir->count].d_name, pid_str, sizeof(dir->entries[dir->count].d_name) - 1);
                dir->entries[dir->count].d_name[sizeof(dir->entries[dir->count].d_name) - 1] = '\0';
                dir->count++;
            }
        }
        return dir;
    }

    user_fs_dirent_t raw_list[MAX_DIR_ENTRIES];
    int64_t count = sys_call(SYS_FS_LIST, (uint64_t)name, (uint64_t)(uintptr_t)raw_list, MAX_DIR_ENTRIES, 0);
    if (count < 0) {
        free(dir);
        errno = ENOENT;
        return NULL;
    }

    for (int i = 0; i < count && i < MAX_DIR_ENTRIES; i++) {
        dir->entries[i].d_ino = raw_list[i].inode_id;
        dir->entries[i].d_type = (raw_list[i].type == USER_FS_TYPE_DIR) ? DT_DIR : DT_REG;
        strncpy(dir->entries[i].d_name, raw_list[i].name, sizeof(dir->entries[i].d_name) - 1);
        dir->entries[i].d_name[sizeof(dir->entries[i].d_name) - 1] = '\0';
    }
    dir->count = (int)count;
    return dir;
}

struct dirent *readdir(DIR *dirp) {
    if (!dirp || dirp->index >= dirp->count) return NULL;
    return &dirp->entries[dirp->index++];
}

int closedir(DIR *dirp) {
    if (!dirp) return -1;
    free(dirp);
    return 0;
}

