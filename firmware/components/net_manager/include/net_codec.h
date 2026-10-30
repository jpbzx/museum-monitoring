#pragma once

/*
 * Formato das mensagens MQTT (JSON, UTF-8). É o contrato com o dashboard e com
 * os scripts de dev-tools: alterar aqui implica alterar lá.
 *
 * telemetry (board -> broker), só inclui as grandezas válidas nessa amostra:
 *   {"seq":12,"ts_us":734000000,"temperature_c":23.41,"humidity_pct":48.20,
 *    "illuminance_lux":120.5,"co2_ppm":612,"voc_index":80,"battery_mv":3912}
 *   ts_us: microssegundos desde o arranque (esp_timer) até haver SNTP.
 *
 * config/set (broker -> board), qualquer combinação destas chaves:
 *   {"sample_period_s":30}                           5..3600
 *   {"power_mode":"performance"|"balanced"|"ultra_low"}
 *   {"sensor":{"name":"sht4x","enabled":false}}
 *   {"report_config":true}
 * Chaves desconhecidas são ignoradas. Se algum valor for inválido a mensagem
 * inteira é rejeitada (nada é aplicado).
 */

#include <stddef.h>
#include "app_types.h"

#define NET_CODEC_MAX_CMDS          4       /* uma por chave reconhecida */
#define NET_CODEC_MAX_CONFIG_LEN    256     /* tamanho máximo aceite para config/set */
#define NET_CODEC_TELEMETRY_MAX     256     /* buffer suficiente para todas as grandezas */

#define NET_CODEC_ERR_JSON          (-1)    /* não é JSON, ou não é um objeto */
#define NET_CODEC_ERR_VALUE         (-2)    /* chave reconhecida com valor inválido */

/** Telemetria -> JSON. Devolve o comprimento (sem '\0') ou -1 se não couber em cap. */
int net_codec_telemetry_json(const telemetry_msg_t *msg, char *buf, size_t cap);

/**
 * config/set -> comandos. Devolve o número de comandos escritos em cmds (0 se não
 * houver nenhuma chave reconhecida) ou um NET_CODEC_ERR_*.
 */
int net_codec_parse_config(const char *json, size_t len, app_cmd_t *cmds, size_t max_cmds);
