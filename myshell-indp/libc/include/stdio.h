#ifndef _STDIO_H
#define _STDIO_H

#include "stdint.h"
#include "stddef.h"
#include "stdarg.h"
#include "stdbool.h"

#define EOF (-1)

typedef struct FILE FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int   printf(const char *format, ...);
int   fprintf(FILE *stream, const char *format, ...);
int   sprintf(char *str, const char *format, ...);
int   snprintf(char *str, size_t size, const char *format, ...);
int   vprintf(const char *format, va_list ap);
int   vfprintf(FILE *stream, const char *format, va_list ap);
int   vsprintf(char *str, const char *format, va_list ap);
int   vsnprintf(char *str, size_t size, const char *format, va_list ap);

int   sscanf(const char *str, const char *format, ...);
int   vsscanf(const char *str, const char *format, va_list ap);

int   putchar(int c);
int   getchar(void);
int   puts(const char *s);
int   fputs(const char *s, FILE *stream);
char *fgets(char *s, int size, FILE *stream);

FILE *fopen(const char *pathname, const char *mode);
int   fclose(FILE *stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int   fseek(FILE *stream, long offset, int whence);
long  ftell(FILE *stream);
void  rewind(FILE *stream);
int   fflush(FILE *stream);

void  perror(const char *s);
int   remove(const char *pathname);

#endif
