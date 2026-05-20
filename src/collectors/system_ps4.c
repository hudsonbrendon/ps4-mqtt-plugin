#include "collectors.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

extern int sysctl(int *name, unsigned int namelen, void *oldp,
                  size_t *oldlenp, void *newp, size_t newlen);
extern int sysctlbyname(const char *name, void *oldp, size_t *oldlenp,
                        void *newp, size_t newlen);

#ifndef CLOCK_UPTIME
#define CLOCK_UPTIME 5
#endif

int collect_system(system_data_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    struct timespec ts;
    if (clock_gettime(CLOCK_UPTIME, &ts) == 0) {
        out->uptime_sec = (int64_t)ts.tv_sec;
    }
    strncpy(out->firmware, "11.00", sizeof(out->firmware) - 1);
    return 0;
}
