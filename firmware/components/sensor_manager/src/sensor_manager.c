#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

#include "sensor_manager.h"
#include "sensor_drivers.h"

#define SENSOR_RAIL_SETTLE_MS  5
#define SENSOR_I2C_GLITCH_CNT  7

static const char *TAG = "sensor_mgr";

/* Ordem de deteção. Para um sensor novo basta acrescentar aqui o driver. */
static const sensor_driver_t *const k_builtin_drivers[] = {
    &sensor_drv_sht4x,
    &sensor_drv_bh1750,
};

typedef struct {
    const sensor_driver_t *drv;
    void                  *ctx;
    bool                   enabled;
} sensor_slot_t;

static struct {
    bool                    initialised;
    sensor_manager_config_t cfg;
    sensor_bus_t            bus;
    sensor_slot_t           slots[SENSOR_MANAGER_MAX_SENSORS];
    size_t                  n_slots;
} s_mgr;

static void rail_set(bool on)
{
    if (s_mgr.cfg.power_en != GPIO_NUM_NC) {
        gpio_set_level(s_mgr.cfg.power_en, on ? 1 : 0);
    }
}

static bool rail_must_stay_on(void)
{
    for (size_t i = 0; i < s_mgr.n_slots; i++) {
        if (s_mgr.slots[i].enabled && s_mgr.slots[i].drv->always_on) {
            return true;
        }
    }
    return false;
}

static uint32_t max_warmup_ms(void)
{
    uint32_t warmup = 0;
    for (size_t i = 0; i < s_mgr.n_slots; i++) {
        if (s_mgr.slots[i].enabled && s_mgr.slots[i].drv->warmup_ms > warmup) {
            warmup = s_mgr.slots[i].drv->warmup_ms;
        }
    }
    return warmup;
}

static void probe_drivers(void)
{
    for (size_t i = 0; i < sizeof(k_builtin_drivers) / sizeof(k_builtin_drivers[0]); i++) {
        const sensor_driver_t *drv = k_builtin_drivers[i];

        if (s_mgr.n_slots >= SENSOR_MANAGER_MAX_SENSORS) {
            ESP_LOGW(TAG, "limite de %d sensores atingido, '%s' ignorado",
                     SENSOR_MANAGER_MAX_SENSORS, drv->name);
            continue;
        }

        void *ctx = NULL;
        esp_err_t err = drv->probe(&s_mgr.bus, &ctx);
        if (err != ESP_OK) {
            ESP_LOGI(TAG, "'%s' não detetado (%s)", drv->name, esp_err_to_name(err));
            continue;
        }

        s_mgr.slots[s_mgr.n_slots++] = (sensor_slot_t){ .drv = drv, .ctx = ctx, .enabled = true };
        ESP_LOGI(TAG, "'%s' detetado", drv->name);
    }
}

esp_err_t sensor_manager_init(const sensor_manager_config_t *cfg)
{
    ESP_RETURN_ON_FALSE(cfg != NULL, ESP_ERR_INVALID_ARG, TAG, "cfg é NULL");
    ESP_RETURN_ON_FALSE(!s_mgr.initialised, ESP_ERR_INVALID_STATE, TAG, "já inicializado");

    memset(&s_mgr, 0, sizeof(s_mgr));
    s_mgr.cfg = *cfg;

    if (cfg->power_en != GPIO_NUM_NC) {
        const gpio_config_t io = {
            .pin_bit_mask = 1ULL << cfg->power_en,
            .mode         = GPIO_MODE_OUTPUT,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "configurar power_en");
    }

    /* Sensores alimentados durante a deteção. */
    rail_set(true);
    vTaskDelay(pdMS_TO_TICKS(SENSOR_RAIL_SETTLE_MS));

    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port          = -1,    /* seleção automática */
        .sda_io_num        = cfg->i2c_sda,
        .scl_io_num        = cfg->i2c_scl,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = SENSOR_I2C_GLITCH_CNT,
        .flags.enable_internal_pullup = false,   /* pull-ups externos no PCB */
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_mgr.bus.i2c);
    if (err != ESP_OK) {
        rail_set(false);
        ESP_LOGE(TAG, "criar barramento I2C: %s", esp_err_to_name(err));
        return err;
    }
    s_mgr.bus.i2c_freq_hz = cfg->i2c_freq_hz;

    probe_drivers();

    if (!rail_must_stay_on()) {
        rail_set(false);
    }

    s_mgr.initialised = true;
    ESP_LOGI(TAG, "%u sensor(es) ativo(s)", (unsigned)s_mgr.n_slots);
    return ESP_OK;
}

esp_err_t sensor_manager_deinit(void)
{
    ESP_RETURN_ON_FALSE(s_mgr.initialised, ESP_ERR_INVALID_STATE, TAG, "não inicializado");

    for (size_t i = 0; i < s_mgr.n_slots; i++) {
        s_mgr.slots[i].drv->remove(s_mgr.slots[i].ctx);
    }
    esp_err_t err = i2c_del_master_bus(s_mgr.bus.i2c);
    rail_set(false);
    s_mgr.initialised = false;
    return err;
}

esp_err_t sensor_manager_sample_all(meas_set_t *out)
{
    ESP_RETURN_ON_FALSE(out != NULL, ESP_ERR_INVALID_ARG, TAG, "out é NULL");
    ESP_RETURN_ON_FALSE(s_mgr.initialised, ESP_ERR_INVALID_STATE, TAG, "não inicializado");

    memset(out, 0, sizeof(*out));

    const bool switched = !rail_must_stay_on();
    if (switched) {
        rail_set(true);
        vTaskDelay(pdMS_TO_TICKS(SENSOR_RAIL_SETTLE_MS + max_warmup_ms()));
    }

    size_t n_ok = 0;
    for (size_t i = 0; i < s_mgr.n_slots; i++) {
        const sensor_slot_t *slot = &s_mgr.slots[i];
        if (!slot->enabled) {
            continue;
        }
        esp_err_t err = slot->drv->read(slot->ctx, out);
        if (err == ESP_OK) {
            n_ok++;
        } else {
            ESP_LOGW(TAG, "leitura '%s' falhou: %s", slot->drv->name, esp_err_to_name(err));
        }
    }

    if (switched) {
        rail_set(false);
    }
    return n_ok > 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t sensor_manager_set_enabled(const char *name, bool enabled)
{
    ESP_RETURN_ON_FALSE(name != NULL, ESP_ERR_INVALID_ARG, TAG, "name é NULL");
    ESP_RETURN_ON_FALSE(s_mgr.initialised, ESP_ERR_INVALID_STATE, TAG, "não inicializado");

    for (size_t i = 0; i < s_mgr.n_slots; i++) {
        if (strcmp(s_mgr.slots[i].drv->name, name) == 0) {
            s_mgr.slots[i].enabled = enabled;
            ESP_LOGI(TAG, "'%s' %s", name, enabled ? "ativado" : "desativado");
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}
