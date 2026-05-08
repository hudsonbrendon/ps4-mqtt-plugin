#ifndef PS4MQTT_MQTT_SOCKET_H
#define PS4MQTT_MQTT_SOCKET_H

#include <stddef.h>

/* Returns a non-negative socket fd on success, -1 on failure. */
int mqtt_socket_connect(const char *host, int port);

/* Returns bytes sent (== len) on success; -1 on failure. */
int mqtt_socket_send(int fd, const void *buf, size_t len);

/* Receives up to len bytes, blocking up to timeout_ms (-1 = block forever).
 * Returns bytes read (>0), 0 on timeout, -1 on error/disconnect. */
int mqtt_socket_recv(int fd, void *buf, size_t len, int timeout_ms);

void mqtt_socket_close(int fd);

#endif
