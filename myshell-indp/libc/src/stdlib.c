#include "stdlib.h"
#include "string.h"
#include "syscall.h"

typedef struct {
    size_t   size;
    uint32_t magic;
} mem_hdr_t;

#define MEM_MAGIC 0xFEED55AA

void *malloc(size_t size) {
    if (size == 0) size = 1;
    size_t total = size + sizeof(mem_hdr_t);
    mem_hdr_t *hdr = (mem_hdr_t *)(uintptr_t)sys_call(SYS_MEM_ALLOC, total, 0, 0, 0);
    if (!hdr) return NULL;
    hdr->size = size;
    hdr->magic = MEM_MAGIC;
    return (void *)(hdr + 1);
}

void free(void *ptr) {
    if (!ptr) return;
    mem_hdr_t *hdr = ((mem_hdr_t *)ptr) - 1;
    if (hdr->magic == MEM_MAGIC) {
        hdr->magic = 0;
        sys_call(SYS_MEM_FREE, (uint64_t)(uintptr_t)hdr, 0, 0, 0);
    }
}

void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    mem_hdr_t *hdr = ((mem_hdr_t *)ptr) - 1;
    if (hdr->magic != MEM_MAGIC) return NULL;

    void *new_ptr = malloc(size);
    if (!new_ptr) return NULL;

    size_t copy_len = (hdr->size < size) ? hdr->size : size;
    memcpy(new_ptr, ptr, copy_len);
    free(ptr);
    return new_ptr;
}

void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

/* ── atexit Support ──────────────────────────────────────────────────────── */
#define MAX_ATEXIT 16
static void (*atexit_funcs[MAX_ATEXIT])(void);
static int atexit_count = 0;

int atexit(void (*function)(void)) {
    if (!function || atexit_count >= MAX_ATEXIT) return -1;
    atexit_funcs[atexit_count++] = function;
    return 0;
}

void exit(int status) {
    while (atexit_count > 0) {
        atexit_count--;
        if (atexit_funcs[atexit_count]) {
            atexit_funcs[atexit_count]();
        }
    }
    sys_call(SYS_EXIT, (uint64_t)status, 0, 0, 0);
    while (1) {}
}

void abort(void) {
    exit(134);
}

/* ── Number Parsing ──────────────────────────────────────────────────────── */
int atoi(const char *nptr) {
    return (int)atol(nptr);
}

long atol(const char *nptr) {
    return strtol(nptr, NULL, 10);
}

long strtol(const char *nptr, char **endptr, int base) {
    if (!nptr) return 0;
    while (*nptr == ' ' || *nptr == '\t' || *nptr == '\n' || *nptr == '\r') nptr++;

    int sign = 1;
    if (*nptr == '-') {
        sign = -1;
        nptr++;
    } else if (*nptr == '+') {
        nptr++;
    }

    if (base == 0) {
        if (*nptr == '0' && (nptr[1] == 'x' || nptr[1] == 'X')) {
            base = 16;
            nptr += 2;
        } else if (*nptr == '0') {
            base = 8;
            nptr++;
        } else {
            base = 10;
        }
    } else if (base == 16 && *nptr == '0' && (nptr[1] == 'x' || nptr[1] == 'X')) {
        nptr += 2;
    }

    long result = 0;
    while (*nptr) {
        int val = -1;
        if (*nptr >= '0' && *nptr <= '9') val = *nptr - '0';
        else if (*nptr >= 'a' && *nptr <= 'f') val = *nptr - 'a' + 10;
        else if (*nptr >= 'A' && *nptr <= 'F') val = *nptr - 'A' + 10;

        if (val < 0 || val >= base) break;
        result = result * base + val;
        nptr++;
    }

    if (endptr) *endptr = (char *)nptr;
    return sign * result;
}

long long strtoll(const char *nptr, char **endptr, int base) {
    return (long long)strtol(nptr, endptr, base);
}

unsigned long long strtoull(const char *nptr, char **endptr, int base) {
    return (unsigned long long)strtol(nptr, endptr, base);
}

/* ── Environment Variables ───────────────────────────────────────────────── */
typedef struct {
    char key[32];
    char value[256];
} env_var_t;

#define MAX_ENV_VARS 32
static env_var_t env_table[MAX_ENV_VARS] = {
    {"USER", "root"},
    {"HOME", "/"},
    {"HOSTNAME", "minikernel"},
    {"PWD", "/"},
    {"OLDPWD", "/"},
    {"SHELL", "/bin/myshell"},
    {"TERM", "xterm-256color"},
    {"PATH", "/bin:/"},
    {"", ""}
};
static int env_count = 8;

char *getenv(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < env_count; i++) {
        if (strcmp(env_table[i].key, name) == 0) {
            return env_table[i].value;
        }
    }
    return NULL;
}

int setenv(const char *name, const char *value, int overwrite) {
    if (!name || name[0] == '\0') return -1;
    for (int i = 0; i < env_count; i++) {
        if (strcmp(env_table[i].key, name) == 0) {
            if (overwrite) {
                strncpy(env_table[i].value, value ? value : "", sizeof(env_table[i].value) - 1);
                env_table[i].value[sizeof(env_table[i].value) - 1] = '\0';
            }
            return 0;
        }
    }
    if (env_count < MAX_ENV_VARS) {
        strncpy(env_table[env_count].key, name, sizeof(env_table[env_count].key) - 1);
        env_table[env_count].key[sizeof(env_table[env_count].key) - 1] = '\0';
        strncpy(env_table[env_count].value, value ? value : "", sizeof(env_table[env_count].value) - 1);
        env_table[env_count].value[sizeof(env_table[env_count].value) - 1] = '\0';
        env_count++;
        return 0;
    }
    return -1;
}
