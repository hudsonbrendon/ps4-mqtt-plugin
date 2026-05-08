#ifndef PS4MQTT_MQTT_CLIENT_H
#define PS4MQTT_MQTT_CLIENT_H

typedef struct mqtt_client mqtt_client_t;

mqtt_client_t *mqtt_client_new(const char *host, int port,
                               const char *user, const char *pass,
                               const char *client_id,
                               int keepalive_sec,
                               const char *will_topic,
                               const char *will_payload);

int  mqtt_client_connect(mqtt_client_t *c);
int  mqtt_client_publish(mqtt_client_t *c, const char *topic,
                         const char *payload, int retain);
int  mqtt_client_ping(mqtt_client_t *c);
int  mqtt_client_disconnect(mqtt_client_t *c);
int  mqtt_client_is_connected(const mqtt_client_t *c);
void mqtt_client_free(mqtt_client_t *c);

#endif
