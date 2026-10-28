#pragma once

#include <stdint.h>
#include "app_types.h"
#include "sensor_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const app_ctx_t         *ctx;
    sensor_manager_config_t  sensors;
    uint32_t                 sample_period_s;
    power_mode_t             power_mode;
} app_logic_config_t;

/** Tarefa do Core 1. arg: const app_logic_config_t* com duração estática. */
void app_logic_task(void *arg);

#ifdef __cplusplus
}
#endif
