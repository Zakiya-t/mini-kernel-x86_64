#ifndef _KERNEL_STRING_H
#define _KERNEL_STRING_H

#include "types.h"

void  *memset(void *dest, int val, size_t count);
void  *memcpy(void *dest, const void *src, size_t count);
void  *memmove(void *dest, const void *src, size_t count);
int    memcmp(const void *s1, const void *s2, size_t n);

size_t strlen(const char *str);
int    strcmp(const char *s1, const char *s2);
int    strncmp(const char *s1, const char *s2, size_t n);
char  *strcpy(char *dest, const char *src);
char  *strncpy(char *dest, const char *src, size_t n);
char  *strchr(const char *s, int c);
char  *strrchr(const char *s, int c);
char  *strcat(char *dest, const char *src);
char  *strncat(char *dest, const char *src, size_t n);

#endif /* _KERNEL_STRING_H */
