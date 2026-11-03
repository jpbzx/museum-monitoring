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
    uint32_t           wifi_backoff_min_ms;
    uint32_t           wifi_backoff_max_ms;
    const char        *client_id;           /* identificador MQTT (único por board) */
    uint16_t           mqtt_keepalive_s;
    uint8_t            telemetry_qos;       /* 0 ou 1 */
    net_mqtt_topics_t  topics;
} net_manager_config_t;

/**
 * Tarefa do Core 0. arg: const net_manager_config_t* com duração estática; as strings
 * de credenciais também têm de viver enquanto a tarefa existir (não são copiadas).
 */
void net_manager_task(void *arg);

#ifdef __cplusplus
}
#endif
