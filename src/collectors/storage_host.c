#include "collectors.h"

int collect_storage(storage_data_t *out) {
    if (!out) return -1;
    out->used_gb  = 320;
    out->total_gb = 500;
    return 0;
}
