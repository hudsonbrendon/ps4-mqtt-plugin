#include "collectors.h"

#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <time.h>

int collect_system(system_data_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    struct timespec ts;
    if (clock_gettime(CLOCK_UPTIME, &ts) == 0) {
        out->uptime_sec = (int64_t)ts.tv_sec;
    }

    int mib[2] = {6 /*CTL_HW*/, 5 /*HW_PHYSMEM*/};
    unsigned long physmem = 0;
    size_t len = sizeof(physmem);
    if (sysctl(mib, 2, &physmem, &len, NULL, 0) == 0) {
        out->mem_total_mb = (uint64_t)(physmem >> 20);
    }

    int v_active = 0, v_wire = 0, page_size = 4096;
    size_t sz;
    sz = sizeof(v_active);
    sysctlbyname("vm.stats.vm.v_active_count", &v_active, &sz, NULL, 0);
    sz = sizeof(v_wire);
    sysctlbyname("vm.stats.vm.v_wire_count",   &v_wire,   &sz, NULL, 0);
    sz = sizeof(page_size);
    sysctlbyname("hw.pagesize", &page_size, &sz, NULL, 0);
    out->mem_used_mb =
        (uint64_t)((unsigned long long)(v_active + v_wire) * page_size >> 20);

    sz = sizeof(out->firmware);
    if (sysctlbyname("kern.osrelease", out->firmware, &sz, NULL, 0) != 0) {
        strncpy(out->firmware, "unknown", sizeof(out->firmware) - 1);
    }
    return 0;
}
