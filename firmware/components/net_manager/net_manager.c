/*
 * STUB — rede (Core 0). Por agora apenas consome a fila de telemetria.
 *
 * TODO:
 *  - Wi-Fi STA com reconexão (backoff); APP_EVT_WIFI_UP no event group
 *  - esp_mqtt_client com Last Will "offline" em topics.status; APP_EVT_MQTT_UP
 *  - subscrever topics.config_set, converter JSON -> app_cmd_t -> command_q
 *  - serializar telemetry_msg_t em JSON e publicar em topics.telemetry (QoS 1)
 *  - APP_EVT_TX_IDLE quando a fila estiver vazia e os PUBACK recebidos
 */

#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "net_manager.h"

static const char *TAG = "net";

void net_manager_task(void *arg)
{
    const net_manager_config_t *cfg = arg;
    const app_ctx_t *ctx = cfg->ctx;

    ESP_LOGW(TAG, "stub: Wi-Fi/MQTT por implementar, telemetria só em log");

    telemetry_msg_t msg;
    for (;;) {
        if (xQueueReceive(ctx->telemetry_q, &msg, portMAX_DELAY) == pdPASS) {
            ESP_LOGI(TAG, "%s seq=%" PRIu32 " valid=0x%02" PRIx32,
                     cfg->topics.telemetry, msg.seq, msg.data.valid_mask);
        }
    }
}
