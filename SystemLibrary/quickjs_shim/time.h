#ifndef QJS_SHIM_TIME_H
#define QJS_SHIM_TIME_H

#include <stddef.h>
#include <stdint.h>

typedef int64_t time_t;
typedef int64_t clockid_t;

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1
#endif

struct timespec {
    time_t tv_sec;
    long   tv_nsec;
};

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

int clock_gettime(clockid_t clk_id, struct timespec *tp);
time_t time(time_t *tloc);
struct tm *gmtime_r(const time_t *timep, struct tm *result);
struct tm *localtime_r(const time_t *timep, struct tm *result);
time_t mktime(struct tm *tm);
double difftime(time_t time1, time_t time0);

#endif
