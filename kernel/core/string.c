#include "string.h"

void *memset(void *dest, int val, size_t count) {
    uint8_t *d = (uint8_t *)dest;
    uint8_t  v = (uint8_t)val;
    for (size_t i = 0; i < count; i++) {
        d[i] = v;
    }
    return dest;
}

void *memcpy(void *dest, const void *src, size_t count) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < count; i++) {
        d[i] = s[i];
    }
    return dest;
}

void *memmove(void *dest, const void *src, size_t count) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) {
        for (size_t i = 0; i < count; i++) {
            d[i] = s[i];
        }
    } else if (d > s) {
        for (size_t i = count; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }
    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] - p2[i];
        }
    }
    return 0;
}

size_t strlen(const char *str) {
    if (!str) return 0;
    size_t len = 0;
    while (str[len] != '\0') {
        len++;
    }
    return len;
}

int strcmp(const char *s1, const char *s2) {
    if (!s1 || !s2) return s1 ? 1 : (s2 ? -1 : 0);
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    if (!s1 || !s2 || n == 0) return 0;
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i] || s1[i] == '\0') {
            return (unsigned char)s1[i] - (unsigned char)s2[i];
        }
    }
    return 0;
}

char *strcpy(char *dest, const char *src) {
    if (!dest || !src) return dest;
    char *ret = dest;
    while ((*dest++ = *src++) != '\0');
    return ret;
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

char *strchr(const char *s, int c) {
    if (!s) return NULL;
    while (*s != '\0') {
        if (*s == (char)c) {
            return (char *)s;
        }
        s++;
    }
    if ((char)c == '\0') return (char *)s;
    return NULL;
}

char *strrchr(const char *s, int c) {
    if (!s) return NULL;
    const char *last = NULL;
    while (*s != '\0') {
        if (*s == (char)c) {
            last = s;
        }
        s++;
    }
    if ((char)c == '\0') return (char *)s;
    return (char *)last;
}

char *strcat(char *dest, const char *src) {
    if (!dest || !src) return dest;
    char *d = dest;
    while (*d != '\0') d++;
    while ((*d++ = *src++) != '\0');
    return dest;
}

char *strncat(char *dest, const char *src, size_t n) {
    if (!dest || !src || n == 0) return dest;
    char *d = dest;
    while (*d != '\0') d++;
    size_t i = 0;
    while (i < n && *src != '\0') {
        *d++ = *src++;
        i++;
    }
    *d = '\0';
    return dest;
}
