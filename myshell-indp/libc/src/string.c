#include "string.h"
#include "stdlib.h"
#include "errno.h"

size_t strlen(const char *s) {
    if (!s) return 0;
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

char *strcpy(char *dest, const char *src) {
    if (!dest || !src) return dest;
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n) {
    if (!dest || !src) return dest;
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

char *strcat(char *dest, const char *src) {
    if (!dest || !src) return dest;
    char *d = dest + strlen(dest);
    while ((*d++ = *src++));
    return dest;
}

char *strncat(char *dest, const char *src, size_t n) {
    if (!dest || !src) return dest;
    size_t dest_len = strlen(dest);
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[dest_len + i] = src[i];
    }
    dest[dest_len + i] = '\0';
    return dest;
}

int strcmp(const char *s1, const char *s2) {
    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    if (n == 0) return 0;
    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;
    while (n > 0 && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

char *strchr(const char *s, int c) {
    if (!s) return NULL;
    char ch = (char)c;
    while (*s) {
        if (*s == ch) return (char *)s;
        s++;
    }
    return (ch == '\0') ? (char *)s : NULL;
}

char *strrchr(const char *s, int c) {
    if (!s) return NULL;
    char ch = (char)c;
    const char *last = NULL;
    while (*s) {
        if (*s == ch) last = s;
        s++;
    }
    if (ch == '\0') return (char *)s;
    return (char *)last;
}

char *strstr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    if (*needle == '\0') return (char *)haystack;
    size_t nlen = strlen(needle);
    while (*haystack) {
        if (strncmp(haystack, needle, nlen) == 0) {
            return (char *)haystack;
        }
        haystack++;
    }
    return NULL;
}

size_t strcspn(const char *s, const char *reject) {
    if (!s || !reject) return 0;
    size_t count = 0;
    while (*s) {
        if (strchr(reject, *s)) break;
        count++;
        s++;
    }
    return count;
}

size_t strspn(const char *s, const char *accept) {
    if (!s || !accept) return 0;
    size_t count = 0;
    while (*s) {
        if (!strchr(accept, *s)) break;
        count++;
        s++;
    }
    return count;
}

char *strtok_r(char *str, const char *delim, char **saveptr) {
    char *s = str ? str : *saveptr;
    if (!s) return NULL;

    s += strspn(s, delim);
    if (*s == '\0') {
        *saveptr = NULL;
        return NULL;
    }

    char *token = s;
    s += strcspn(s, delim);
    if (*s == '\0') {
        *saveptr = NULL;
    } else {
        *s = '\0';
        *saveptr = s + 1;
    }
    return token;
}

char *strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *copy = (char *)malloc(len + 1);
    if (copy) {
        memcpy(copy, s, len + 1);
    }
    return copy;
}

char *strerror(int errnum) {
    switch (errnum) {
        case 0:           return "Success";
        case EPERM:       return "Operation not permitted";
        case ENOENT:      return "No such file or directory";
        case ESRCH:       return "No such process";
        case EINTR:       return "Interrupted system call";
        case EIO:         return "Input/output error";
        case ENOMEM:      return "Cannot allocate memory";
        case EACCES:      return "Permission denied";
        case EEXIST:      return "File exists";
        case ENODEV:      return "No such device";
        case ENOTDIR:     return "Not a directory";
        case EISDIR:      return "Is a directory";
        case EINVAL:      return "Invalid argument";
        case ENOSPC:      return "No space left on device";
        case ESPIPE:      return "Illegal seek";
        default:          return "Unknown error";
    }
}

void *memset(void *s, int c, size_t n) {
    unsigned char *p = (unsigned char *)s;
    unsigned char val = (unsigned char)c;
    while (n--) *p++ = val;
    return s;
}

void *memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dest;
}

void *memmove(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else if (d > s) {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;
    while (n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++;
        p2++;
    }
    return 0;
}

void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = (const unsigned char *)s;
    unsigned char ch = (unsigned char)c;
    while (n--) {
        if (*p == ch) return (void *)p;
        p++;
    }
    return NULL;
}

