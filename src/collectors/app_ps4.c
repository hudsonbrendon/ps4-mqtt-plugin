#include "collectors.h"

#include <stdio.h>
#include <string.h>

extern int sceSystemServiceGetAppIdOfBigApp(void);
extern int sceLncUtilGetAppTitleId(int app_id, char *out, int out_len);
extern int sceLncUtilGetAppId(int *out);
extern int sceLncUtilGetApp0DirPath(int app_id, char *buf, int size);
extern int sceAppInstUtilAppGetInsertedDiscTitleId(char *out, int size);

void app_set_argv(int argc, const char *argv[]) {
    (void)argc; (void)argv;
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

int collect_app(app_data_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    int own_app_id = -999;
    int rc_own = sceLncUtilGetAppId(&own_app_id);

    char dir_path[256] = {0};
    int rc_dir = -1;
    int dir_app_id = (rc_own == 0 && own_app_id > 0)
                     ? own_app_id : sceSystemServiceGetAppIdOfBigApp();
    if (dir_app_id > 0) {
        rc_dir = sceLncUtilGetApp0DirPath(dir_app_id, dir_path, sizeof(dir_path));
    }
    char from_dir[16] = {0};
    if (rc_dir == 0) extract_cusa(dir_path, from_dir, sizeof(from_dir));

    char disc_title[16] = {0};
    int rc_disc = sceAppInstUtilAppGetInsertedDiscTitleId(disc_title,
                                                         sizeof(disc_title));
    char from_disc[16] = {0};
    if (rc_disc == 0) extract_cusa(disc_title, from_disc, sizeof(from_disc));

    int big_app = sceSystemServiceGetAppIdOfBigApp();
    char lnc_title[16] = {0};
    int rc_lnc = -1;
    if (big_app > 0) {
        rc_lnc = sceLncUtilGetAppTitleId(big_app, lnc_title,
                                         sizeof(lnc_title));
    }

    snprintf(out->debug, sizeof(out->debug),
             "own=%d(%d) dir=%s(%d) disc=%s(%d) lnc=%s big=%d",
             own_app_id, rc_own,
             from_dir[0]  ? from_dir  : "-", rc_dir,
             from_disc[0] ? from_disc : "-", rc_disc,
             lnc_title[0] ? lnc_title : "-",
             big_app);

    const char *winner = NULL;
    if (from_dir[0])       winner = from_dir;
    else if (from_disc[0]) winner = from_disc;
    else if (lnc_title[0]) winner = lnc_title;

    if (winner) {
        out->in_game = 1;
        strncpy(out->title,    winner, sizeof(out->title)    - 1);
        strncpy(out->title_id, winner, sizeof(out->title_id) - 1);
    }
    return 0;
}
