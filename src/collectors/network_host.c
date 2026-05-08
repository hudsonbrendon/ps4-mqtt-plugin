#include "collectors.h"

#include <string.h>

int collect_network(network_data_t *out) {
    if (!out) return -1;
    strncpy(out->ip,   "192.168.1.50", sizeof(out->ip)   - 1);
    strncpy(out->ssid, "MinhaRede",    sizeof(out->ssid) - 1);
    out->ip[sizeof(out->ip)   - 1] = '\0';
    out->ssid[sizeof(out->ssid) - 1] = '\0';
    out->rssi_dbm = -55;
    return 0;
}
