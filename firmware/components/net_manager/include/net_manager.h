#pragma once

#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *telemetry;
    const char *status;
    const char *config_set;
    const char *config_state;
} net_mqtt_topics_t;

typedef struct {
    const app_ctx_t   *ctx;
    const char        *wifi_ssid;
    const char        *wifi_password;
    const char        *broker_uri;
    const char        *mqtt_username;
    const char        *mqtt_password;
    net_mqtt_topics_t  topics;
} net_manager_config_t;

/** Tarefa do Core 0. arg: const net_manager_config_t* com duração estática. */
void net_manager_task(void *arg);

#ifdef __cplusplus
}
#endif
