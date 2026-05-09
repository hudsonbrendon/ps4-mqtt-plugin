#include "collectors.h"
#include "../log.h"

#include <string.h>

#define SCE_NET_CTL_INFO_IP_ADDRESS  14
#define SCE_NET_CTL_INFO_SSID         8
#define SCE_NET_CTL_INFO_RSSI_DBM    11

typedef union {
    char ip_address[16];
    char ssid[33];
    int  rssi_dbm;
    char raw[64];
} SceNetCtlInfo;

extern int sceNetCtlInit(void);
extern int sceNetCtlGetInfo(int code, SceNetCtlInfo *info);
extern void sceNetCtlTerm(void);

int collect_network(network_data_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    if (sceNetCtlInit() != 0) {
        LOG_WARN("network: sceNetCtlInit failed");
        return -1;
    }

    SceNetCtlInfo info;

    if (sceNetCtlGetInfo(SCE_NET_CTL_INFO_IP_ADDRESS, &info) == 0) {
        strncpy(out->ip, info.ip_address, sizeof(out->ip) - 1);
    }
    if (sceNetCtlGetInfo(SCE_NET_CTL_INFO_SSID, &info) == 0) {
        strncpy(out->ssid, info.ssid, sizeof(out->ssid) - 1);
    }
    if (sceNetCtlGetInfo(SCE_NET_CTL_INFO_RSSI_DBM, &info) == 0) {
        out->rssi_dbm = info.rssi_dbm;
    }

    sceNetCtlTerm();
    return 0;
}
