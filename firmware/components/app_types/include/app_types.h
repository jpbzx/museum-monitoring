#pragma once

/*
 * Contrato de dados entre módulos. As mensagens são copiadas por valor para
 * as filas FreeRTOS, por isso têm tamanho fixo e não contêm ponteiros.
 */

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Grandezas medidas ---------------------------------------------------- */

typedef enum {
    MEAS_TEMPERATURE = 0,   /* °C */
    MEAS_HUMIDITY,          /* %HR */
    MEAS_ILLUMINANCE,       /* lux */
    MEAS_CO2,               /* ppm      — módulo NDIR opcional */
    MEAS_VOC_INDEX,         /* 1..500   — módulo VOC opcional */
    MEAS_BATTERY,           /* mV */
    MEAS_COUNT
} meas_type_t;

#define MEAS_BIT(type) (1UL << (type))

typedef struct {
    float    value[MEAS_COUNT];
    uint32_t valid_mask;    /* bit n a 1 => value[n] é válido */
} meas_set_t;

static inline void meas_set_put(meas_set_t *set, meas_type_t type, float value)
{
    set->value[type] = value;
    set->valid_mask |= MEAS_BIT(type);
}

/* ---- Core 1 -> Core 0 ----------------------------------------------------- */

typedef struct {
    uint32_t   seq;
    int64_t    timestamp_us;    /* esp_timer; substituir por epoch após SNTP */
    meas_set_t data;
} telemetry_msg_t;

/* ---- Core 0 -> Core 1 (comandos vindos de config/set) --------------------- */

typedef enum {
    POWER_MODE_PERFORMANCE = 0, /* sempre acordado, Wi-Fi ligado */
    POWER_MODE_BALANCED,        /* light sleep automático + modem sleep */
    POWER_MODE_ULTRA_LOW,       /* deep sleep entre ciclos (bateria) */
} power_mode_t;

#define APP_SAMPLE_PERIOD_MIN_S 5
#define APP_SAMPLE_PERIOD_MAX_S 3600
#define APP_SENSOR_NAME_LEN     16

typedef enum {
    CMD_SET_SAMPLE_PERIOD = 0,
    CMD_SET_POWER_MODE,
    CMD_SET_SENSOR_ENABLED,
    CMD_REPORT_CONFIG,
} app_cmd_type_t;

typedef struct {
    app_cmd_type_t type;
    union {
        uint32_t     sample_period_s;
        power_mode_t power_mode;
        struct {
            char name[APP_SENSOR_NAME_LEN];
            bool enabled;
        } sensor;
    };
} app_cmd_t;

/* ---- Estado partilhado ---------------------------------------------------- */

#define APP_EVT_WIFI_UP     BIT0
#define APP_EVT_MQTT_UP     BIT1
#define APP_EVT_TX_IDLE     BIT2    /* fila de telemetria escoada: seguro para dormir */

typedef struct {
    QueueHandle_t      telemetry_q;
    QueueHandle_t      command_q;
    EventGroupHandle_t events;
} app_ctx_t;

#ifdef __cplusplus
}
#endif
