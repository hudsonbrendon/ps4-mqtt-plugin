#include "collectors.h"
#include "../log.h"

#include <stdio.h>
#include <string.h>

extern int sceSystemServiceGetAppIdOfBigApp(void);
extern int sceLncUtilGetAppTitleId(int app_id, char *out, int out_len);

static int parse_titleid_from_cwd(char *out, size_t out_len) {
    char buf[256];
    FILE *f = fopen("/proc/self/cwd", "r");
    if (f) {
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        if (n > 0) {
            buf[n] = '\0';
            char *p = strstr(buf, "/CUSA");
            if (p) {
                p++;
                int i = 0;
                while (i < (int)(out_len - 1) && p[i] && p[i] != '/' && p[i] != '_') {
                    out[i] = p[i];
                    i++;
                }
                out[i] = '\0';
                return i > 0 ? 0 : -1;
            }
        }
    }
    return -1;
}

int collect_app(app_data_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    int app_id = sceSystemServiceGetAppIdOfBigApp();
    char title_id[16] = {0};
    int lnc_rc = -1;
    if (app_id > 0) {
        lnc_rc = sceLncUtilGetAppTitleId(app_id, title_id, sizeof(title_id));
    }

    if (title_id[0] == '\0') {
        parse_titleid_from_cwd(title_id, sizeof(title_id));
    }

    snprintf(out->debug, sizeof(out->debug),
             "app_id=%d lnc_rc=%d", app_id, lnc_rc);

    if (title_id[0]) {
        out->in_game = 1;
        strncpy(out->title,    title_id, sizeof(out->title)    - 1);
        strncpy(out->title_id, title_id, sizeof(out->title_id) - 1);
    }
    return 0;
}
