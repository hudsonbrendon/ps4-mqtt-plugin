#include "config.h"

#include "cJSON.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_file(const char *path, char **out_buf) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0) { fclose(f); return -1; }
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return -1; }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
        fclose(f); free(buf); return -1;
    }
    buf[len] = '\0';
    fclose(f);
    *out_buf = buf;
    return 0;
}

static int copy_string_field(const cJSON *root, const char *key,
                             char *dest, size_t dest_len, int required) {
    const cJSON *node = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsString(node) || node->valuestring == NULL) {
        if (required) {
            LOG_ERR("config: missing required field '%s'", key);
            return -1;
        }
        return 1; /* not present, caller must apply default */
    }
    size_t n = strlen(node->valuestring);
    if (n + 1 > dest_len) {
        LOG_ERR("config: field '%s' too long (max %zu)", key, dest_len - 1);
        return -1;
    }
    memcpy(dest, node->valuestring, n + 1);
    return 0;
}

static int copy_int_field(const cJSON *root, const char *key,
                          int *dest, int required) {
    const cJSON *node = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsNumber(node)) {
        if (required) {
            LOG_ERR("config: missing required field '%s'", key);
            return -1;
        }
        return 1;
    }
    *dest = node->valueint;
    return 0;
}

int config_load(const char *path, config_t *out) {
    if (!path || !out) return -1;
    memset(out, 0, sizeof(*out));

    char *buf = NULL;
    if (read_file(path, &buf) != 0) {
        LOG_ERR("config: cannot read file '%s'", path);
        return -1;
    }

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        LOG_ERR("config: malformed JSON in '%s'", path);
        return -1;
    }

    int rc = 0;
    if (copy_string_field(root, "broker_host", out->broker_host,
                          sizeof(out->broker_host), 1) < 0) { rc = -1; goto done; }
    if (copy_string_field(root, "username", out->username,
                          sizeof(out->username), 1) < 0) { rc = -1; goto done; }
    if (copy_string_field(root, "password", out->password,
                          sizeof(out->password), 1) < 0) { rc = -1; goto done; }

    /* optional fields with defaults */
    out->broker_port = 1883;
    copy_int_field(root, "broker_port", &out->broker_port, 0);

    strncpy(out->device_name, "PS4", sizeof(out->device_name) - 1);
    copy_string_field(root, "device_name", out->device_name,
                      sizeof(out->device_name), 0);

    out->poll_interval_sec = 10;
    copy_int_field(root, "poll_interval_sec", &out->poll_interval_sec, 0);

done:
    cJSON_Delete(root);
    return rc;
}
