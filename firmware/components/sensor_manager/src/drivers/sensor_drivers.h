#pragma once

/* Header privado: drivers incluídos neste componente. */

#include "sensor_driver.h"

extern const sensor_driver_t sensor_drv_sht4x;     /* Temperatura + humidade relativa */
extern const sensor_driver_t sensor_drv_bh1750;    /* Iluminância */
