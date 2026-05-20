#include "collectors.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern int sceSystemServiceGetAppIdOfBigApp(void);
extern int sceLncUtilGetAppTitleId(int app_id, char *out, int out_len);

static int  g_argc = 0;
static const char *g_argv[8];

void app_set_argv(int argc, const char *argv[]) {
    g_argc = argc < 8 ? argc : 8;
    for (int i = 0; i < g_argc; ++i) g_argv[i] = argv ? argv[i] : NULL;
}

static int extract_cusa(const char *src, char *out, size_t out_len) {
    if (!src || !out || out_len == 0) return -1;
    const char *p = strstr(src, "CUSA");
    if (!p) return -1;
    size_t i = 0;
    while (i < out_len - 1 && p[i] && p[i] != '/' && p[i] != '_'
           && p[i] != ' ') {
        out[i] = p[i];
        i++;
    }
    out[i] = '\0';
    return (i >= 5) ? 0 : -1;
}

static int read_cmdline(char *buf, size_t len) {
    int fd = open("/proc/self/cmdline", 0 /*O_RDONLY*/);
    if (fd < 0) return -1;
    int n = (int)read(fd, buf, len - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';
    for (int i = 0; i < n; i++) if (buf[i] == '\0') buf[i] = ' ';
    return n;
}

int collect_app(app_data_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    char from_argv[16] = {0};
    for (int i = 0; i < g_argc; ++i) {
        if (g_argv[i] && extract_cusa(g_argv[i], from_argv,
                                      sizeof(from_argv)) == 0) break;
    }

    char from_env[16] = {0};
    static const char *keys[] = {
        "SCE_TITLEID", "SCE_BREADCRUMB_DUMP_ROOT",
        "HOME", "PWD", NULL
    };
    for (int i = 0; keys[i]; ++i) {
        const char *v = getenv(keys[i]);
        if (v && extract_cusa(v, from_env, sizeof(from_env)) == 0) break;
    }

    char cmdline[512] = {0};
    char from_cmd[16] = {0};
    int rc_cmd = read_cmdline(cmdline, sizeof(cmdline));
    if (rc_cmd > 0) extract_cusa(cmdline, from_cmd, sizeof(from_cmd));

    int app_id = sceSystemServiceGetAppIdOfBigApp();
    char lnc_title[16] = {0};
    int rc_lnc = -1;
    if (app_id > 0) {
        rc_lnc = sceLncUtilGetAppTitleId(app_id, lnc_title,
                                         sizeof(lnc_title));
    }

    snprintf(out->debug, sizeof(out->debug),
             "argv=%s env=%s cmd=%s(%d) lnc=%s app=%d",
             from_argv[0]    ? from_argv : "-",
             from_env[0]     ? from_env  : "-",
             from_cmd[0]     ? from_cmd  : "-",
             rc_cmd,
             lnc_title[0]    ? lnc_title : "-",
             app_id);

    const char *winner = NULL;
    if (from_argv[0])     winner = from_argv;
    else if (from_env[0]) winner = from_env;
    else if (from_cmd[0]) winner = from_cmd;
    else if (lnc_title[0]) winner = lnc_title;

    if (winner) {
        out->in_game = 1;
        strncpy(out->title,    winner, sizeof(out->title)    - 1);
        strncpy(out->title_id, winner, sizeof(out->title_id) - 1);
    }
    return 0;
}
