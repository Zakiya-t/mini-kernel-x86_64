#ifndef _STDLIB_H
#define _STDLIB_H

#include "stdint.h"
#include "stddef.h"

void *malloc(size_t size);
void  free(void *ptr);
void *realloc(void *ptr, size_t size);
void *calloc(size_t nmemb, size_t size);

void  exit(int status);
void  abort(void);
int   atexit(void (*function)(void));

int   atoi(const char *nptr);
long  atol(const char *nptr);
long  strtol(const char *nptr, char **endptr, int base);
long long strtoll(const char *nptr, char **endptr, int base);
unsigned long long strtoull(const char *nptr, char **endptr, int base);

char *getenv(const char *name);
int   setenv(const char *name, const char *value, int overwrite);

#endif
