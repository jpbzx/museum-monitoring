#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*user_button_cb_t)(void *ctx);

typedef struct {
    gpio_num_t       pin;
    bool             active_low;      /* true: premido = nível 0 (pull-up externo + botão à massa) */
    uint32_t         long_press_ms;   /* tempo contínuo premido para disparar on_long_press */
    user_button_cb_t on_long_press;   /* chamado UMA vez por pressão, no contexto da tarefa do botão */
    void            *ctx;
} user_button_config_t;

/** Configura o GPIO e arranca a tarefa que monitoriza o botão. A config é copiada. */
esp_err_t user_button_start(const user_button_config_t *cfg);

#ifdef __cplusplus
}
#endif
