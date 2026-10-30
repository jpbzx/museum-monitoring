#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Tamanhos incluem o '\0'. A password Wi-Fi WPA2 tem no máximo 63 caracteres. */
#define PROV_SSID_MAX       33
#define PROV_WIFI_PASS_MAX  64
#define PROV_MQTT_URI_MAX   128
#define PROV_MQTT_USER_MAX  64
#define PROV_MQTT_PASS_MAX  64

typedef struct {
    char wifi_ssid[PROV_SSID_MAX];
    char wifi_password[PROV_WIFI_PASS_MAX];
    char mqtt_uri[PROV_MQTT_URI_MAX];
    char mqtt_username[PROV_MQTT_USER_MAX];
    char mqtt_password[PROV_MQTT_PASS_MAX];
} prov_creds_t;

typedef struct {
    const char *ssid_prefix;    /* o SSID final é "<prefixo>-XXXX" (XXXX = fim do MAC) */
    const char *password;       /* WPA2: 8 a 63 caracteres */
    uint8_t     channel;        /* 1..13 */
} prov_softap_config_t;

/**
 * Arranca o ponto de acesso de configuração (IP 192.168.4.1) e regista o log
 * de ligações. Não bloqueia: o AP fica a correr em background.
 * Requer esp_netif_init() e esp_event_loop_create_default() já feitos.
 */
esp_err_t prov_start_softap(const prov_softap_config_t *cfg);

/** Arranca o servidor HTTP com o formulário de configuração (porta 80). */
esp_err_t prov_http_start(void);

/** Grava as credenciais na NVS. Requer nvs_flash_init() já feito. */
esp_err_t prov_save(const prov_creds_t *creds);

/**
 * Lê as credenciais da NVS.
 * @return true se existirem credenciais completas (e *out foi preenchido);
 *         false se a board ainda não foi provisionada (*out fica intacto).
 */
bool prov_load(prov_creds_t *out);

/** Apaga todas as credenciais (a board volta ao estado "não provisionada"). */
esp_err_t prov_erase(void);

#ifdef __cplusplus
}
#endif
