#include "collectors.h"

#include <string.h>

int collect_app(app_data_t *out) {
    if (!out) return -1;
    out->in_game = 1;
    strncpy(out->title,    "Bloodborne", sizeof(out->title)    - 1);
    strncpy(out->title_id, "CUSA00900",  sizeof(out->title_id) - 1);
    out->title[sizeof(out->title)       - 1] = '\0';
    out->title_id[sizeof(out->title_id) - 1] = '\0';
    return 0;
}
