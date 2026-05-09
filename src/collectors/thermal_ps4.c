#include "collectors.h"
#include "../log.h"

#include <string.h>
#include <sys/sysctl.h>

extern int sceKernelGetCpuTemperature(int *out_celsius);
extern int sceKernelGetSocSensorTemperature(int sensor_id, int *out_celsius);

int collect_thermal(thermal_data_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    int cpu_c = 0;
    if (sceKernelGetCpuTemperature(&cpu_c) == 0) {
        out->cpu_temp_c = (double)cpu_c;
    } else {
        LOG_WARN("thermal: sceKernelGetCpuTemperature failed");
    }

    int soc_c = 0;
    if (sceKernelGetSocSensorTemperature(0, &soc_c) == 0) {
        out->soc_temp_c = (double)soc_c;
    } else {
        LOG_WARN("thermal: sceKernelGetSocSensorTemperature failed");
    }

    int fan_rpm = 0;
    size_t sz = sizeof(fan_rpm);
    if (sysctlbyname("machdep.fan_speed", &fan_rpm, &sz, NULL, 0) == 0) {
        out->fan_rpm = fan_rpm;
    }
    return 0;
}
