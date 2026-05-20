#include "collectors.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern int sceSystemServiceGetAppIdOfBigApp(void);
extern int sceLncUtilGetAppTitleId(int app_id, char *out, int out_len);

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

int collect_app(app_data_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    char cwd[256] = {0};
    char *rc_cwd_ptr = getcwd(cwd, sizeof(cwd));

    int app_id = sceSystemServiceGetAppIdOfBigApp();
    char lnc_title[16] = {0};
    int rc_lnc = -1;
    if (app_id > 0) {
        rc_lnc = sceLncUtilGetAppTitleId(app_id, lnc_title,
                                         sizeof(lnc_title));
    }

    char from_cwd[16] = {0};
    if (rc_cwd_ptr) extract_cusa(cwd, from_cwd, sizeof(from_cwd));

    snprintf(out->debug, sizeof(out->debug),
             "sb=none-symbol cwd=%s exe=none-symbol lnc=%s app=%d rc_lnc=%d",
             from_cwd[0]     ? from_cwd     : "-",
             lnc_title[0]    ? lnc_title    : "-",
             app_id, rc_lnc);

    const char *winner = NULL;
    if (lnc_title[0])         winner = lnc_title;
    else if (from_cwd[0])     winner = from_cwd;

    if (winner) {
        out->in_game = 1;
        strncpy(out->title,    winner, sizeof(out->title)    - 1);
        strncpy(out->title_id, winner, sizeof(out->title_id) - 1);
    }
    return 0;
}
