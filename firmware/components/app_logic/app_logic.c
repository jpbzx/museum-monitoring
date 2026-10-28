/*
 * STUB — lógica de negócio (Core 1): cadência de amostragem e aplicação de
 * comandos. Falta: modos de energia, persistência da configuração em NVS,
 * reporte de config/state.
 */

#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "app_logic.h"

static const char *TAG = "app_logic";

typedef struct {
    uint32_t     sample_period_s;
    power_mode_t power_mode;
} logic_state_t;

static void handle_command(const app_cmd_t *cmd, logic_state_t *st)
{
    switch (cmd->type) {
    case CMD_SET_SAMPLE_PERIOD:
        if (cmd->sample_period_s < APP_SAMPLE_PERIOD_MIN_S ||
            cmd->sample_period_s > APP_SAMPLE_PERIOD_MAX_S) {
            ESP_LOGW(TAG, "período %" PRIu32 " s fora de [%d, %d], ignorado",
                     cmd->sample_period_s, APP_SAMPLE_PERIOD_MIN_S, APP_SAMPLE_PERIOD_MAX_S);
            break;
        }
        st->sample_period_s = cmd->sample_period_s;
        ESP_LOGI(TAG, "período de amostragem = %" PRIu32 " s", st->sample_period_s);
        break;
    case CMD_SET_POWER_MODE:
        st->power_mode = cmd->power_mode;
        /* TODO: esp_pm_configure() / deep sleep conforme o modo. */
        break;
    case CMD_SET_SENSOR_ENABLED:
        if (sensor_manager_set_enabled(cmd->sensor.name, cmd->sensor.enabled) != ESP_OK) {
            ESP_LOGW(TAG, "sensor '%.*s' desconhecido",
                     APP_SENSOR_NAME_LEN, cmd->sensor.name);
        }
        break;
    case CMD_REPORT_CONFIG:
        /* TODO: enviar configuração efetiva para o Core 0 (config/state). */
        break;
    }
}

/* Espera pelo próximo ciclo sem drift, acordando para aplicar comandos do Core 0. */
static void wait_next_cycle(const app_ctx_t *ctx, TickType_t *last_wake, logic_state_t *st)
{
    for (;;) {
        const TickType_t period  = pdMS_TO_TICKS(st->sample_period_s * 1000U);
        const TickType_t elapsed = xTaskGetTickCount() - *last_wake;
        if (elapsed >= period) {
            *last_wake += period;
            return;
        }
        app_cmd_t cmd;
        if (xQueueReceive(ctx->command_q, &cmd, period - elapsed) == pdPASS) {
            handle_command(&cmd, st);
        }
    }
}

void app_logic_task(void *arg)
{
    const app_logic_config_t *cfg = arg;
    const app_ctx_t *ctx = cfg->ctx;
    logic_state_t st = {
        .sample_period_s = cfg->sample_period_s,
        .power_mode      = cfg->power_mode,
    };

    /* Inicializado aqui (e não em app_main) para a ISR do I2C ficar no Core 1. */
    esp_err_t err = sensor_manager_init(&cfg->sensors);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sensor_manager_init: %s", esp_err_to_name(err));
    }

    uint32_t seq = 0;
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        telemetry_msg_t msg = { .seq = seq++, .timestamp_us = esp_timer_get_time() };

        if (sensor_manager_sample_all(&msg.data) == ESP_OK) {
            if (xQueueSend(ctx->telemetry_q, &msg, 0) != pdPASS) {
                ESP_LOGW(TAG, "fila de telemetria cheia, amostra %" PRIu32 " descartada", msg.seq);
            }
        }

        wait_next_cycle(ctx, &last_wake, &st);
    }
}
