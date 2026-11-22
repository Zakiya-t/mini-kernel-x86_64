#ifndef _TIME_H
#define _TIME_H

#include "stdint.h"
#include "stddef.h"

typedef int64_t time_t;

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

time_t     time(time_t *tloc);
struct tm *localtime(const time_t *timep);
size_t     strftime(char *s, size_t max, const char *format, const struct tm *tm);

#endif

