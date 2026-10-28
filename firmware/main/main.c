/*
 * Ponto de composição do firmware.
 *
 * Arquitetura:
 *   Core 0 (APP_CORE_NET) : net_manager_task  — Wi-Fi + MQTT
 *   Core 1 (APP_CORE_APP) : app_logic_task    — amostragem (sensor_manager) + lógica
 *
 *   app_logic --[telemetry_q: telemetry_msg_t]--> net_manager
 *   app_logic <--[command_q:  app_cmd_t      ]--  net_manager
 *   events (EventGroup): estado da rede, usado pela lógica para decidir quando dormir
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "app_config.h"
#include "app_types.h"
#include "app_logic.h"
#include "net_manager.h"

static const char *TAG = "main";

/* Duração estática: as tarefas guardam ponteiros para estas estruturas. */
static app_ctx_t s_ctx;

static const app_logic_config_t s_logic_cfg = {
    .ctx = &s_ctx,
    .sensors = {
        .i2c_sda     = APP_PIN_I2C_SDA,
        .i2c_scl     = APP_PIN_I2C_SCL,
        .i2c_freq_hz = APP_I2C_FREQ_HZ,
        .power_en    = APP_PIN_SENSOR_PWR_EN,
    },
    .sample_period_s = APP_SAMPLE_PERIOD_DEFAULT_S,
    .power_mode      = APP_POWER_MODE_DEFAULT,
};

static const net_manager_config_t s_net_cfg = {
    .ctx           = &s_ctx,
    .wifi_ssid     = APP_WIFI_SSID,
    .wifi_password = APP_WIFI_PASSWORD,
    .broker_uri    = APP_MQTT_BROKER_URI,
    .mqtt_username = APP_MQTT_USERNAME,
    .mqtt_password = APP_MQTT_PASSWORD,
    .topics = {
        .telemetry    = APP_MQTT_TOPIC_TELEMETRY,
        .status       = APP_MQTT_TOPIC_STATUS,
        .config_set   = APP_MQTT_TOPIC_CONFIG_SET,
        .config_state = APP_MQTT_TOPIC_CONFIG_STATE,
    },
};

static esp_err_t nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS inválida (%s), a apagar partição", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

static esp_err_t ipc_init(app_ctx_t *ctx)
{
    ctx->telemetry_q = xQueueCreate(APP_TELEMETRY_QUEUE_LEN, sizeof(telemetry_msg_t));
    ctx->command_q   = xQueueCreate(APP_COMMAND_QUEUE_LEN, sizeof(app_cmd_t));
    ctx->events      = xEventGroupCreate();

    if (ctx->telemetry_q == NULL || ctx->command_q == NULL || ctx->events == NULL) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void start_task(TaskFunction_t fn, const char *name, uint32_t stack,
                       const void *arg, UBaseType_t prio, BaseType_t core)
{
    BaseType_t ok = xTaskCreatePinnedToCore(fn, name, stack, (void *)arg, prio, NULL, core);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "falha a criar tarefa '%s' (heap livre: %u B)",
                 name, (unsigned)xPortGetFreeHeapSize());
        abort();
    }
    ESP_LOGI(TAG, "tarefa '%s' criada no core %d", name, (int)core);
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_init());

    /* Singletons de sistema necessários ao Wi-Fi; criados aqui uma única vez. */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(ipc_init(&s_ctx));

    start_task(net_manager_task, APP_TASK_NET_NAME, APP_TASK_NET_STACK,
               &s_net_cfg, APP_TASK_NET_PRIO, APP_CORE_NET);
    start_task(app_logic_task, APP_TASK_APP_NAME, APP_TASK_APP_STACK,
               &s_logic_cfg, APP_TASK_APP_PRIO, APP_CORE_APP);

    /* app_main retorna: a main task é removida e os recursos ficam com as tarefas. */
}
