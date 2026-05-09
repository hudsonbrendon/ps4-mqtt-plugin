#include "mqtt_socket.h"

#include <string.h>

typedef int SceNetId;

#define SCE_NET_AF_INET     2
#define SCE_NET_SOCK_STREAM 1
#define SCE_NET_IPPROTO_TCP 6
#define SCE_NET_SOL_SOCKET  0xFFFF
#define SCE_NET_SO_RCVTIMEO 0x1006

typedef struct SceNetInAddr { unsigned int s_addr; } SceNetInAddr;

typedef struct SceNetSockaddrIn {
    unsigned char  sin_len;
    unsigned char  sin_family;
    unsigned short sin_port;
    SceNetInAddr   sin_addr;
    unsigned short sin_vport;
    char           sin_zero[6];
} SceNetSockaddrIn;

extern SceNetId sceNetSocket(const char *name, int family, int type, int protocol);
extern int      sceNetConnect(SceNetId s, const SceNetSockaddrIn *addr, int addrlen);
extern int      sceNetSend(SceNetId s, const void *buf, int len, int flags);
extern int      sceNetRecv(SceNetId s, void *buf, int len, int flags);
extern int      sceNetSocketClose(SceNetId s);
extern int      sceNetSetsockopt(SceNetId s, int level, int name,
                                 const void *val, int vallen);
extern int      sceNetInetPton(int af, const char *src, void *dst);
extern unsigned short sceNetHtons(unsigned short host);

int mqtt_socket_connect(const char *host, int port) {
    SceNetId s = sceNetSocket("ps4mqtt", SCE_NET_AF_INET,
                              SCE_NET_SOCK_STREAM, SCE_NET_IPPROTO_TCP);
    if (s < 0) return -1;

    SceNetSockaddrIn addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_len    = sizeof(addr);
    addr.sin_family = SCE_NET_AF_INET;
    addr.sin_port   = sceNetHtons((unsigned short)port);
    if (sceNetInetPton(SCE_NET_AF_INET, host, &addr.sin_addr) <= 0) {
        sceNetSocketClose(s);
        return -1;
    }

    if (sceNetConnect(s, &addr, sizeof(addr)) < 0) {
        sceNetSocketClose(s);
        return -1;
    }
    return s;
}

int mqtt_socket_send(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    size_t sent = 0;
    while (sent < len) {
        int n = sceNetSend(fd, p + sent, (int)(len - sent), 0);
        if (n <= 0) return -1;
        sent += (size_t)n;
    }
    return (int)sent;
}

int mqtt_socket_recv(int fd, void *buf, size_t len, int timeout_ms) {
    if (timeout_ms >= 0) {
        struct { unsigned int sec; unsigned int usec; } tv;
        tv.sec  = (unsigned)(timeout_ms / 1000);
        tv.usec = (unsigned)((timeout_ms % 1000) * 1000);
        sceNetSetsockopt(fd, SCE_NET_SOL_SOCKET, SCE_NET_SO_RCVTIMEO,
                         &tv, sizeof(tv));
    }
    int n = sceNetRecv(fd, buf, (int)len, 0);
    if (n < 0) return -1;
    return n;
}

void mqtt_socket_close(int fd) {
    if (fd >= 0) sceNetSocketClose(fd);
}
