/*
 * Sensores simulados por botões (bancada, sem hardware de sensores).
 *
 * Cada botão é um sensor: uma pressão curta alterna o canal entre o valor
 * "normal" e o valor "alarme"; o valor fica memorizado e é lido no ciclo de
 * amostragem normal. Para passar a sensores reais, apagar este ficheiro e
 * as entradas sim_btn* em k_builtin_drivers[].
 */

#include <stdatomic.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_check.h"
#include "user_button.h"
#include "sensor_drivers.h"

static const char *TAG = "sim_btn";

typedef struct {
    meas_type_t channel;
    float       normal;
    float       alarm;
} sim_channel_t;

static const sim_channel_t k_channels[SENSOR_SIM_BUTTONS] = {
    { MEAS_CO2,         600.0f, 1500.0f },
    { MEAS_VOC_INDEX,   100.0f,  300.0f },
    { MEAS_ILLUMINANCE,  50.0f,  500.0f },
};

typedef struct {
    const sim_channel_t *ch;
    atomic_bool          alarm;     /* escrito pela tarefa do botão, lido pelo Core 1 */
} sim_state_t;

static gpio_num_t s_pins[SENSOR_SIM_BUTTONS] = { GPIO_NUM_NC, GPIO_NUM_NC, GPIO_NUM_NC };

void sensor_drv_sim_buttons_set_pins(const gpio_num_t pins[SENSOR_SIM_BUTTONS])
{
    for (int i = 0; i < SENSOR_SIM_BUTTONS; i++) {
        s_pins[i] = pins[i];
    }
}

static void on_short_press(void *ctx)
{
    sim_state_t *st = ctx;
    bool now = !atomic_load(&st->alarm);
    atomic_store(&st->alarm, now);
    ESP_LOGI(TAG, "canal %d -> %s", (int)st->ch->channel, now ? "alarme" : "normal");
}

static esp_err_t sim_probe(int idx, void **ctx)
{
    if (s_pins[idx] == GPIO_NUM_NC) {
        return ESP_ERR_NOT_FOUND;
    }

    sim_state_t *st = calloc(1, sizeof(*st));
    ESP_RETURN_ON_FALSE(st != NULL, ESP_ERR_NO_MEM, TAG, "sem memória");
    st->ch = &k_channels[idx];

    const user_button_config_t cfg = {
        .pin            = s_pins[idx],
        .active_low     = true,         /* pull-up interno + botão à massa */
        .on_short_press = on_short_press,
        .ctx            = st,
    };
    esp_err_t err = user_button_start(&cfg);
    if (err != ESP_OK) {
        free(st);
        return err;
    }
    *ctx = st;
    return ESP_OK;
}

static esp_err_t sim_read(void *ctx, meas_set_t *out)
{
    const sim_state_t *st = ctx;
    meas_set_put(out, st->ch->channel, atomic_load(&st->alarm) ? st->ch->alarm : st->ch->normal);
    return ESP_OK;
}

/* A tarefa do botão mantém o ponteiro para o estado: não se liberta. */
static void sim_remove(void *ctx)
{
    (void)ctx;
}

#define SIM_DRIVER(idx, drv_name, meas)                                                 \
    static esp_err_t sim##idx##_probe(const sensor_bus_t *bus, void **ctx)          \
    {                                                                               \
        (void)bus;                                                                  \
        return sim_probe(idx, ctx);                                                 \
    }                                                                               \
    const sensor_driver_t sensor_drv_sim_btn##idx = {                               \
        .name      = drv_name,                                                      \
        .provides  = MEAS_BIT(meas),                                                \
        .warmup_ms = 0,                                                             \
        .always_on = false,                                                         \
        .probe     = sim##idx##_probe,                                              \
        .read      = sim_read,                                                      \
        .remove    = sim_remove,                                                    \
    }

SIM_DRIVER(0, "sim_btn1", MEAS_CO2);
SIM_DRIVER(1, "sim_btn2", MEAS_VOC_INDEX);
SIM_DRIVER(2, "sim_btn3", MEAS_ILLUMINANCE);
