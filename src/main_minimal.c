#include <stdio.h>
#include <stddef.h>
#include <string.h>

typedef struct SceNetInAddr { unsigned int s_addr; } SceNetInAddr;
typedef struct SceNetSockaddrIn {
    unsigned char sin_len;
    unsigned char sin_family;
    unsigned short sin_port;
    SceNetInAddr sin_addr;
    unsigned short sin_vport;
    char sin_zero[6];
} SceNetSockaddrIn;

extern int sceNetSocket(const char *name, int family, int type, int protocol);
extern int sceNetConnect(int s, const SceNetSockaddrIn *addr, int addrlen);
extern int sceNetSend(int s, const void *buf, int len, int flags);
extern int sceNetRecv(int s, void *buf, int len, int flags);
extern int sceNetSocketClose(int s);
extern int sceNetInetPton(int af, const char *src, void *dst);
extern unsigned short sceNetHtons(unsigned short host);
extern int sceNetSetsockopt(int s, int level, int name, const void *val, int vallen);

__asm__(
    ".intel_syntax noprefix \n"
    ".align 0x8 \n"
    ".section \".data.sce_module_param\" \n"
    "_sceProcessParam: \n"
    "    .quad 0x18 \n"
    "    .quad 0x13C13F4BF \n"
    "    .quad 0x4508101 \n"
    ".att_syntax prefix \n"
);

__asm__(
    ".intel_syntax noprefix \n"
    ".align 0x8 \n"
    ".data \n"
    "__dso_handle: \n"
    "    .quad 0 \n"
    "_sceLibc: \n"
    "    .quad 0 \n"
    ".att_syntax prefix \n"
);

__attribute__((visibility("default"))) const char *g_pluginName    = "ps4-mqtt";
__attribute__((visibility("default"))) const char *g_pluginDesc    = "PS4 MQTT minimal";
__attribute__((visibility("default"))) const char *g_pluginAuth    = "hudsonbrendon";
__attribute__((visibility("default"))) unsigned int g_pluginVersion = 0x00000100;

static int put_str(unsigned char *buf, const char *s) {
    size_t n = strlen(s);
    buf[0] = (unsigned char)(n >> 8);
    buf[1] = (unsigned char)(n & 0xFF);
    memcpy(buf + 2, s, n);
    return (int)(2 + n);
}

__attribute__((visibility("default")))
int plugin_load(int argc, const char *argv[]) {
    (void)argc; (void)argv;

    int s = sceNetSocket("mqtt", 2, 1, 6);
    if (s < 0) return 0;

    SceNetSockaddrIn addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_len = sizeof(addr);
    addr.sin_family = 2;
    addr.sin_port = sceNetHtons(1883);
    sceNetInetPton(2, "192.168.31.150", &addr.sin_addr);
    if (sceNetConnect(s, &addr, sizeof(addr)) < 0) {
        sceNetSocketClose(s);
        return 0;
    }

    unsigned char buf[256];
    int p = 0;
    p += put_str(buf + p, "MQTT");
    buf[p++] = 0x04;
    buf[p++] = 0xC2;
    buf[p++] = 0x00;
    buf[p++] = 0x3C;
    p += put_str(buf + p, "ps4-test");
    p += put_str(buf + p, "hudsonbrendon");
    p += put_str(buf + p, "@Admin996247004");

    unsigned char hdr[4];
    hdr[0] = 0x10;
    hdr[1] = (unsigned char)p;
    sceNetSend(s, hdr, 2, 0);
    sceNetSend(s, buf, p, 0);

    unsigned char connack[4] = {0};
    int got_connack = sceNetRecv(s, connack, 4, 0);

    p = 0;
    p += put_str(buf + p, "ps4/test");
    const char *payload = "hello-from-plugin";
    memcpy(buf + p, payload, strlen(payload));
    p += (int)strlen(payload);

    hdr[0] = 0x30;
    hdr[1] = (unsigned char)p;
    sceNetSend(s, hdr, 2, 0);
    sceNetSend(s, buf, p, 0);

    hdr[0] = 0xE0;
    hdr[1] = 0x00;
    sceNetSend(s, hdr, 2, 0);

    unsigned char drain[16];
    sceNetRecv(s, drain, sizeof(drain), 0);

    sceNetSocketClose(s);

    int sr = sceNetSocket("report", 2, 1, 6);
    if (sr < 0) return 0;
    addr.sin_port = sceNetHtons(9998);
    sceNetInetPton(2, "192.168.31.165", &addr.sin_addr);
    if (sceNetConnect(sr, &addr, sizeof(addr)) >= 0) {
        char rb[128];
        int n = snprintf(rb, sizeof(rb),
                         "connack_recv=%d bytes=[%02x %02x %02x %02x]\n",
                         got_connack, connack[0], connack[1], connack[2], connack[3]);
        sceNetSend(sr, rb, n, 0);
    }
    sceNetSocketClose(sr);
    return 0;
}

__attribute__((visibility("default")))
int plugin_unload(int argc, const char *argv[]) {
    (void)argc; (void)argv;
    return 0;
}

int module_start(size_t argc, const void *argv) {
    return plugin_load((int)argc, (const char **)argv);
}

int module_stop(size_t argc, const void *argv) {
    return plugin_unload((int)argc, (const char **)argv);
}

int _init(size_t argc, const void *argv) { return module_start(argc, argv); }
int _fini(size_t argc, const void *argv) { return module_stop(argc, argv); }
