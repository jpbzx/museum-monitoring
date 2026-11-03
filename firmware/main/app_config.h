#pragma once

/*
 * Configuração estática da aplicação (placeholders).
 *
 * Este ficheiro só é incluído por main.c: os componentes recebem estes valores
 * através das suas structs de configuração, nunca incluem app_config.h.
 *
 * TODO: mover credenciais (Wi-Fi/MQTT) para Kconfig.projbuild ou NVS
 *       (provisioning) para não ficarem no repositório.
 */

#include "driver/gpio.h"
#include "app_types.h"

/* ---------------------------------------------------------------------------
 * Identificação do nó (um nó por divisão do museu)
 * ------------------------------------------------------------------------- */
#define APP_SITE_ID                 "isep-dee"
#define APP_ROOM_ID                 "sala-01"
#define APP_DEVICE_ID               "node-01"   /* TODO: derivar do MAC (esp_read_mac) */

/* ---------------------------------------------------------------------------
 * Access point de configuração (primeiro boot / após reset de 6 s)
 * TODO: password fixa e igual em todas as boards é fraca; derivar do MAC ou
 *       imprimir numa etiqueta (decisão do passo 9).
 * ------------------------------------------------------------------------- */
#define APP_PROV_AP_SSID_PREFIX     "museum-node"
#define APP_PROV_AP_PASSWORD        "museum-setup"      /* 8-63 caracteres */
#define APP_PROV_AP_CHANNEL         1

/* ---------------------------------------------------------------------------
 * Wi-Fi (STA)
 * ------------------------------------------------------------------------- */
/* SSID/password vêm da NVS (provisioning). Reconexão: tenta para sempre, com backoff exponencial. */
#define APP_WIFI_BACKOFF_MIN_MS     1000
#define APP_WIFI_BACKOFF_MAX_MS     30000

/* ---------------------------------------------------------------------------
 * MQTT
 *
 * Esquema de tópicos: museum/<site>/<room>/<device>/<canal>
 *   telemetry     -> publicação periódica das medições (QoS 1)
 *   status        -> "online"/"offline" retained; "offline" é o Last Will
 *   config/set    <- subscrito: dashboard altera cadência, modo de energia,
 *                    ativa/desativa sensores
 *   config/state  -> configuração efetiva reportada (retained)
 * ------------------------------------------------------------------------- */
/* URI, username e password do broker vêm da NVS (provisioning). */
#define APP_MQTT_KEEPALIVE_S        60
#define APP_MQTT_QOS_TELEMETRY      1

#define APP_MQTT_TOPIC_BASE         "museum/" APP_SITE_ID "/" APP_ROOM_ID "/" APP_DEVICE_ID
#define APP_MQTT_TOPIC_TELEMETRY    APP_MQTT_TOPIC_BASE "/telemetry"
#define APP_MQTT_TOPIC_STATUS       APP_MQTT_TOPIC_BASE "/status"
#define APP_MQTT_TOPIC_CONFIG_SET   APP_MQTT_TOPIC_BASE "/config/set"
#define APP_MQTT_TOPIC_CONFIG_STATE APP_MQTT_TOPIC_BASE "/config/state"

/* ---------------------------------------------------------------------------
 * Mapeamento de pinos (ESP32 DevKit — ajustar ao PCB final)
 * ------------------------------------------------------------------------- */
/* Barramento I2C partilhado: Temp/HR, iluminância e módulos de expansão (VOC, CO2 I2C) */
#define APP_PIN_I2C_SDA             GPIO_NUM_21
#define APP_PIN_I2C_SCL             GPIO_NUM_22
#define APP_I2C_FREQ_HZ             100000

/* Interruptor high-side da alimentação dos sensores (corta o consumo em repouso).
 * Usar GPIO_NUM_NC se a linha de sensores estiver sempre alimentada. */
#define APP_PIN_SENSOR_PWR_EN       GPIO_NUM_25

/* UART para sensor CO2 NDIR opcional (ex.: MH-Z19 / SenseAir S8) */
#define APP_PIN_CO2_UART_TX         GPIO_NUM_17
#define APP_PIN_CO2_UART_RX         GPIO_NUM_16

/* Divisor resistivo da bateria — tem de ser ADC1 (o ADC2 não funciona com Wi-Fi ativo) */
#define APP_PIN_BATTERY_ADC         GPIO_NUM_34     /* ADC1_CH6 */

/* Interface local */
#define APP_PIN_STATUS_LED          GPIO_NUM_2
#define APP_PIN_USER_BUTTON         GPIO_NUM_27     /* RTC GPIO: pode acordar do deep sleep (ext0) */
#define APP_BUTTON_ACTIVE_LOW       true            /* pull-up interno + botão à massa */
#define APP_BUTTON_LONG_PRESS_MS    6000            /* pressão longa: apagar credenciais */

/* ---------------------------------------------------------------------------
 * Amostragem e energia (valores por omissão; o dashboard pode alterá-los)
 * Os limites min/max fazem parte do contrato e estão em app_types.h.
 * ------------------------------------------------------------------------- */
#define APP_SAMPLE_PERIOD_DEFAULT_S 60
#define APP_POWER_MODE_DEFAULT      POWER_MODE_BALANCED

/* ---------------------------------------------------------------------------
 * FreeRTOS: afinidade, prioridades, stacks e filas
 * Nota: no ESP-IDF o stack de xTaskCreatePinnedToCore é em BYTES, não em words.
 * ------------------------------------------------------------------------- */
#define APP_CORE_NET                0       /* PRO_CPU: Wi-Fi, lwIP, MQTT */
#define APP_CORE_APP                1       /* APP_CPU: amostragem + lógica de negócio */

#define APP_TASK_NET_NAME           "net"
#define APP_TASK_NET_STACK          6144
#define APP_TASK_NET_PRIO           5

#define APP_TASK_APP_NAME           "app"
#define APP_TASK_APP_STACK          4096
#define APP_TASK_APP_PRIO           4

/* Buffer de telemetria enquanto a rede está em baixo (16 amostras = 16 min a 60 s) */
#define APP_TELEMETRY_QUEUE_LEN     16
#define APP_COMMAND_QUEUE_LEN       4
