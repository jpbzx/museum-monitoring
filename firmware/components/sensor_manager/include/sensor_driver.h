#pragma once

/*
 * Interface que cada driver de sensor implementa. É pública para que um
 * sensor novo possa viver no seu próprio componente (ex.: components/scd4x).
 *
 * Para adicionar um sensor: implementar um sensor_driver_t e acrescentá-lo
 * a k_builtin_drivers[] em sensor_manager.c.
 */

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    i2c_master_bus_handle_t i2c;
    uint32_t                i2c_freq_hz;
} sensor_bus_t;

typedef struct {
    const char *name;
    uint32_t    provides;   /* máscara de MEAS_BIT() que o driver preenche */
    uint32_t    warmup_ms;  /* tempo após ligar a alimentação até leitura válida */
    bool        always_on;  /* true se o sensor não tolera corte de alimentação
                               (ex.: NDIR com aquecimento de minutos, algoritmo VOC) */

    /** Deteta o sensor. ESP_ERR_NOT_FOUND se ausente; *ctx fica com o estado do driver. */
    esp_err_t (*probe)(const sensor_bus_t *bus, void **ctx);
    /** Medição one-shot; tem de funcionar logo após a alimentação ser ligada. */
    esp_err_t (*read)(void *ctx, meas_set_t *out);
    void      (*remove)(void *ctx);
} sensor_driver_t;

#ifdef __cplusplus
}
#endif
