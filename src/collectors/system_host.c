#include "collectors.h"

#include <string.h>

int collect_system(system_data_t *out) {
    if (!out) return -1;
    out->uptime_sec   = 12345;
    out->mem_used_mb  = 4096;
    out->mem_total_mb = 8192;
    strncpy(out->firmware, "11.00", sizeof(out->firmware) - 1);
    out->firmware[sizeof(out->firmware) - 1] = '\0';
    return 0;
}
