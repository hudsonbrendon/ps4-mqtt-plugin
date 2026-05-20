#include "collectors.h"

#include <stdio.h>
#include <string.h>

extern int sceSystemServiceGetAppIdOfBigApp(void);
extern int sceLncUtilGetAppTitleId(int app_id, char *out, int out_len);

void app_set_argv(int argc, const char *argv[]) {
    (void)argc; (void)argv;
}

int collect_app(app_data_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    int app_id = sceSystemServiceGetAppIdOfBigApp();
    if (app_id <= 0) return 0;

    char title_id[16] = {0};
    if (sceLncUtilGetAppTitleId(app_id, title_id, sizeof(title_id)) != 0) {
        return 0;
    }
    out->in_game = 1;
    strncpy(out->title,    title_id, sizeof(out->title)    - 1);
    strncpy(out->title_id, title_id, sizeof(out->title_id) - 1);
    return 0;
}
