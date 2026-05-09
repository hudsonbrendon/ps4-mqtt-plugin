#include "collectors.h"
#include "../log.h"

#include <string.h>
#include <sys/mount.h>

int collect_storage(storage_data_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    struct statfs sfs;
    if (statfs("/user", &sfs) != 0) {
        LOG_WARN("storage: statfs(/user) failed");
        return -1;
    }
    uint64_t total_bytes = (uint64_t)sfs.f_blocks * (uint64_t)sfs.f_bsize;
    uint64_t free_bytes  = (uint64_t)sfs.f_bavail * (uint64_t)sfs.f_bsize;
    uint64_t used_bytes  = total_bytes - free_bytes;
    out->total_gb = total_bytes >> 30;
    out->used_gb  = used_bytes  >> 30;
    return 0;
}
