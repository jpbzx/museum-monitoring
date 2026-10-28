#pragma once

/*
 * sensor_manager — dono do barramento I2C, da alimentação dos sensores e do
 * registo de drivers. Os sensores são detetados no arranque (probe), por isso
 * adicionar/remover um módulo de hardware não exige alterar a lógica.
 *
 * Não é thread-safe: todas as funções devem ser chamadas pela mesma tarefa
 * (app_logic, Core 1). Chamar init nessa tarefa garante que a ISR do I2C
 * fica instalada no Core 1, longe da stack de rede.
 */

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SENSOR_MANAGER_MAX_SENSORS 8

typedef struct {
    gpio_num_t i2c_sda;
    gpio_num_t i2c_scl;
    uint32_t   i2c_freq_hz;
    gpio_num_t power_en;    /* GPIO_NUM_NC se a alimentação não for comutada */
} sensor_manager_config_t;

/** Configura o barramento e deteta os sensores presentes. */
esp_err_t sensor_manager_init(const sensor_manager_config_t *cfg);

/** Liberta o barramento e desliga a alimentação dos sensores. */
esp_err_t sensor_manager_deinit(void);

/**
 * Liga a alimentação, espera pelo aquecimento, lê todos os sensores ativos e
 * volta a desligar. Grandezas sem leitura ficam com o bit a 0 em valid_mask.
 *
 * @return ESP_OK se pelo menos um sensor foi lido; ESP_FAIL se nenhum.
 */
esp_err_t sensor_manager_sample_all(meas_set_t *out);

/** Ativa/desativa um sensor detetado, pelo nome do driver (ex.: "sht4x"). */
esp_err_t sensor_manager_set_enabled(const char *name, bool enabled);

#ifdef __cplusplus
}
#endif
