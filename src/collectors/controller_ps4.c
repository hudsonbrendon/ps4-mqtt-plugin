#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct OrbisPadStick {
    uint8_t x, y;
} OrbisPadStick;

typedef struct OrbisPadAnalog {
    uint8_t l2, r2;
} OrbisPadAnalog;

typedef struct OrbisPadTouch {
    uint16_t x, y;
    uint8_t finger;
    uint8_t pad[3];
} OrbisPadTouch;

typedef struct OrbisPadTouchData {
    uint8_t fingers;
    uint8_t pad1[3];
    uint32_t pad2;
    OrbisPadTouch touch[2];
} OrbisPadTouchData;

typedef struct OrbisPadData {
    uint32_t buttons;
    OrbisPadStick leftStick;
    OrbisPadStick rightStick;
    OrbisPadAnalog analogButtons;
    uint16_t padding;
    float quat[4];
    float vel[3];
    float acell[3];
    OrbisPadTouchData touch;
    uint8_t connected;
    uint64_t timestamp;
    uint8_t ext[16];
    uint8_t count;
    uint8_t unknown[15];
} OrbisPadData;

extern int scePadInit(void);
extern int scePadOpen(int user_id, int type, int index, void *param);
extern int scePadGetHandle(int user_id, int type, int index);
extern int scePadClose(int handle);
extern int scePadReadState(int handle, OrbisPadData *data);
extern int sceUserServiceInitialize(void *param);
extern int sceUserServiceTerminate(void);
extern int sceUserServiceGetInitialUser(int *user_id);

typedef struct ScePadExtControllerInformation {
    uint8_t pad_type;
    uint8_t reserved1[3];
    uint8_t battery_level;
    uint8_t connection_type;
    uint8_t count;
    uint8_t connected;
    uint8_t reserved2[8];
} ScePadExtControllerInformation;
extern int scePadGetExtControllerInformation(int handle, ScePadExtControllerInformation *info);

typedef struct ScePadDeviceClassExtendedInformation {
    uint8_t device_class;
    uint8_t reserved[3];
    uint8_t capability;
    uint8_t quantity_of_extended_data;
    uint8_t maximum_physical_pad_number;
    uint8_t reserved2;
    uint8_t class_data[8];
    uint8_t battery_level;
    uint8_t connection_type;
    uint8_t reserved3[6];
} ScePadDeviceClassExtendedInformation;
extern int scePadDeviceClassGetExtendedInformation(int handle,
        ScePadDeviceClassExtendedInformation *info);

typedef struct {
    int  connected;
    int  battery_pct;
    int  battery_charging;
    int  user_id;
    int  pad_handle;
    char debug[96];
} controller_data_t;

int collect_controller(controller_data_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    out->battery_pct = -1;

    sceUserServiceInitialize(NULL);
    int initial_user = -1;
    sceUserServiceGetInitialUser(&initial_user);
    scePadInit();

    int users[] = { initial_user, 0xFF, -1, 0, 1, 0xFE000000 };
    int handle = -1;
    int used_user = -1;
    int opened = 0;
    for (size_t i = 0; i < sizeof(users)/sizeof(users[0]); ++i) {
        int h = scePadGetHandle(users[i], 0, 0);
        if (h >= 0) { handle = h; used_user = users[i]; break; }
        h = scePadOpen(users[i], 0, 0, NULL);
        if (h >= 0) { handle = h; used_user = users[i]; opened = 1; break; }
    }

    ScePadDeviceClassExtendedInformation dext;
    memset(&dext, 0, sizeof(dext));
    int rc_dext = -1;
    OrbisPadData data;
    memset(&data, 0, sizeof(data));
    int rc_read = -1;

    if (handle >= 0) {
        rc_dext = scePadDeviceClassGetExtendedInformation(handle, &dext);
        rc_read = scePadReadState(handle, &data);
        if (rc_dext == 0) {
            out->connected = 1;
            int lvl = dext.battery_level & 0x0F;
            out->battery_charging = (dext.battery_level & 0x10) ? 1 : 0;
            if (lvl > 10) lvl = 10;
            out->battery_pct = lvl * 10;
        } else if (rc_read == 0) {
            out->connected = data.connected ? 1 : 0;
        }
        if (opened) scePadClose(handle);
    }

    uint8_t *db = (uint8_t *)&dext;
    snprintf(out->debug, sizeof(out->debug),
             "%02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x",
             db[0], db[1], db[2], db[3], db[4], db[5], db[6], db[7],
             db[8], db[9], db[10], db[11], db[12], db[13], db[14], db[15],
             db[16], db[17], db[18], db[19], db[20], db[21], db[22], db[23]);

    out->user_id = used_user;
    out->pad_handle = handle;
    return 0;
}
