#ifndef POPPY_TIME_H
#define POPPY_TIME_H

#include <stdint.h>
#include <time.h>

static inline void addTime(struct timespec* to, const struct timespec* amount) {
    to->tv_sec += amount->tv_sec;
    to->tv_nsec += amount->tv_nsec;
    to->tv_sec += to->tv_nsec / 1000000000;
    to->tv_nsec %= 1000000000;
}
static inline void subTime(struct timespec* from, const struct timespec* amount) {
    from->tv_sec -= amount->tv_sec;
    from->tv_nsec -= amount->tv_nsec;
    from->tv_sec += from->tv_nsec / 1000000000;
    from->tv_nsec %= 1000000000;
    if (from->tv_nsec < 0) {
        from->tv_sec -= 1;
        from->tv_nsec += 1000000000;
    }
}

static inline void getTime(struct timespec* out) {
    clock_gettime(CLOCK_MONOTONIC, out);
}

static inline void waitFor(struct timespec* amount) {
    nanosleep(amount, NULL);
}
static void waitUntil(struct timespec* target) {
    struct timespec curtime;
    getTime(&curtime);
    struct timespec amount = *target;
    if (curtime.tv_sec > amount.tv_sec) return;
    if (curtime.tv_nsec == amount.tv_nsec && curtime.tv_nsec >= amount.tv_nsec) return;
    subTime(&amount, &curtime);
    waitFor(&amount);
}

#endif
