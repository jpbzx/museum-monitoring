/* Sensirion SHT4x — temperatura e humidade relativa (I2C). */

#include "esp_log.h"
#include "esp_check.h"
#include "sensor_drivers.h"

#define SHT4X_ADDR              0x44
#define SHT4X_PROBE_TIMEOUT_MS  50

static const char *TAG = "sht4x";

static esp_err_t sht4x_probe(const sensor_bus_t *bus, void **ctx)
{
    if (i2c_master_probe(bus->i2c, SHT4X_ADDR, SHT4X_PROBE_TIMEOUT_MS) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = SHT4X_ADDR,
        .scl_speed_hz    = bus->i2c_freq_hz,
    };
    i2c_master_dev_handle_t dev;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus->i2c, &dev_cfg, &dev), TAG, "add_device");

    *ctx = dev;
    return ESP_OK;
}

static esp_err_t sht4x_read(void *ctx, meas_set_t *out)
{
    i2c_master_dev_handle_t dev = ctx;

    /* TODO:
     *  1. i2c_master_transmit(dev, {0xFD}) — medição de alta precisão
     *  2. vTaskDelay(pdMS_TO_TICKS(10))     — máx. 8.3 ms
     *  3. i2c_master_receive(dev, buf, 6)   — [T_msb T_lsb CRC RH_msb RH_lsb CRC]
     *  4. validar CRC-8 (polinómio 0x31, init 0xFF) de cada palavra
     *  5. T  = -45 + 175 * raw_t  / 65535
     *     RH =  -6 + 125 * raw_rh / 65535  (limitar a 0..100)
     *  6. meas_set_put(out, MEAS_TEMPERATURE, t); meas_set_put(out, MEAS_HUMIDITY, rh);
     */
    (void)dev;
    (void)out;
    return ESP_ERR_NOT_SUPPORTED;
}

static void sht4x_remove(void *ctx)
{
    i2c_master_bus_rm_device(ctx);
}

const sensor_driver_t sensor_drv_sht4x = {
    .name      = "sht4x",
    .provides  = MEAS_BIT(MEAS_TEMPERATURE) | MEAS_BIT(MEAS_HUMIDITY),
    .warmup_ms = 1,
    .always_on = false,
    .probe     = sht4x_probe,
    .read      = sht4x_read,
    .remove    = sht4x_remove,
};
