#include "collectors.h"

int collect_thermal(thermal_data_t *out) {
    if (!out) return -1;
    out->cpu_temp_c = 62.5;
    out->soc_temp_c = 58.0;
    out->fan_rpm    = 1800;
    return 0;
}
