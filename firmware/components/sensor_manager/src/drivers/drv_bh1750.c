/* ROHM BH1750 — iluminância (I2C). */

#include "esp_log.h"
#include "esp_check.h"
#include "sensor_drivers.h"

#define BH1750_ADDR             0x23    /* pino ADDR a GND; 0x5C se a VCC */
#define BH1750_PROBE_TIMEOUT_MS 50

static const char *TAG = "bh1750";

static esp_err_t bh1750_probe(const sensor_bus_t *bus, void **ctx)
{
    if (i2c_master_probe(bus->i2c, BH1750_ADDR, BH1750_PROBE_TIMEOUT_MS) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = BH1750_ADDR,
        .scl_speed_hz    = bus->i2c_freq_hz,
    };
    i2c_master_dev_handle_t dev;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus->i2c, &dev_cfg, &dev), TAG, "add_device");

    *ctx = dev;
    return ESP_OK;
}

static esp_err_t bh1750_read(void *ctx, meas_set_t *out)
{
    i2c_master_dev_handle_t dev = ctx;

    /* TODO:
     *  1. i2c_master_transmit(dev, {0x01}) — power on (necessário após corte de alimentação)
     *  2. i2c_master_transmit(dev, {0x20}) — one-time H-resolution (volta a power down sozinho)
     *  3. vTaskDelay(pdMS_TO_TICKS(180))   — tempo máx. de conversão
     *  4. i2c_master_receive(dev, buf, 2)
     *  5. lux = ((buf[0] << 8) | buf[1]) / 1.2f
     *  6. meas_set_put(out, MEAS_ILLUMINANCE, lux);
     */
    (void)dev;
    (void)out;
    return ESP_ERR_NOT_SUPPORTED;
}

static void bh1750_remove(void *ctx)
{
    i2c_master_bus_rm_device(ctx);
}

const sensor_driver_t sensor_drv_bh1750 = {
    .name      = "bh1750",
    .provides  = MEAS_BIT(MEAS_ILLUMINANCE),
    .warmup_ms = 0,
    .always_on = false,
    .probe     = bh1750_probe,
    .read      = bh1750_read,
    .remove    = bh1750_remove,
};
