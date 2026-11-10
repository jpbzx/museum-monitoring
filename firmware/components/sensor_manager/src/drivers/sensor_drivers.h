#pragma once

/* Header privado: drivers incluídos neste componente. */

#include "sensor_driver.h"

extern const sensor_driver_t sensor_drv_sht4x;     /* Temperatura + humidade relativa */
extern const sensor_driver_t sensor_drv_bh1750;    /* Iluminância */

/* Sensores simulados por botões (bancada): sim_btn1..3 mapeados a CO2, VOC e iluminância. */
#define SENSOR_SIM_BUTTONS 3
extern const sensor_driver_t sensor_drv_sim_btn0;
extern const sensor_driver_t sensor_drv_sim_btn1;
extern const sensor_driver_t sensor_drv_sim_btn2;
/** Pinos dos botões (GPIO_NUM_NC = canal desligado); chamar antes do probe. */
void sensor_drv_sim_buttons_set_pins(const gpio_num_t pins[SENSOR_SIM_BUTTONS]);
