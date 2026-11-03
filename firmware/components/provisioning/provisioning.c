/*
 * Credenciais de rede/MQTT persistidas na NVS (namespace "prov").
 *
 * Chaves: ssid, wifi_pass, mqtt_uri, mqtt_user, mqtt_pass (strings) e
 * "provisioned" (u8). A flag é a última a ser escrita: se faltar a energia a
 * meio de um prov_save(), fica a 0 e a board volta ao modo de configuração.
 */

#include <stdio.h>
#include <string.h>
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "provisioning.h"

#define PROV_NAMESPACE  "prov"
#define KEY_FLAG        "provisioned"   /* as chaves NVS têm no máximo 15 caracteres */
#define KEY_SSID        "ssid"
#define KEY_WIFI_PASS   "wifi_pass"
#define KEY_MQTT_URI    "mqtt_uri"
#define KEY_MQTT_USER   "mqtt_user"
#define KEY_MQTT_PASS   "mqtt_pass"

static const char *TAG = "prov";

/* Lê uma string da NVS para um buffer de tamanho fixo. */
static esp_err_t get_str(nvs_handle_t h, const char *key, char *dst, size_t dst_size)
{
    size_t len = dst_size;      /* in: capacidade do buffer; out: tamanho lido (com '\0') */
    return nvs_get_str(h, key, dst, &len);
}

esp_err_t prov_save(const prov_creds_t *creds)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(PROV_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %s", esp_err_to_name(err));
        return err;
    }

    /* 1) invalida: se algo falhar daqui para a frente, a board não fica "meio provisionada" */
    err = nvs_set_u8(h, KEY_FLAG, 0);
    if (err == ESP_OK) err = nvs_commit(h);

    /* 2) dados */
    if (err == ESP_OK) err = nvs_set_str(h, KEY_SSID,      creds->wifi_ssid);
    if (err == ESP_OK) err = nvs_set_str(h, KEY_WIFI_PASS, creds->wifi_password);
    if (err == ESP_OK) err = nvs_set_str(h, KEY_MQTT_URI,  creds->mqtt_uri);
    if (err == ESP_OK) err = nvs_set_str(h, KEY_MQTT_USER, creds->mqtt_username);
    if (err == ESP_OK) err = nvs_set_str(h, KEY_MQTT_PASS, creds->mqtt_password);

    /* 3) valida */
    if (err == ESP_OK) err = nvs_set_u8(h, KEY_FLAG, 1);
    if (err == ESP_OK) err = nvs_commit(h);

    nvs_close(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "falha a gravar credenciais: %s", esp_err_to_name(err));
    }
    return err;
}

bool prov_load(prov_creds_t *out)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(PROV_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        /* ESP_ERR_NVS_NOT_FOUND: namespace ainda não existe (primeiro boot) */
        return false;
    }

    uint8_t flag = 0;
    err = nvs_get_u8(h, KEY_FLAG, &flag);
    if (err != ESP_OK || flag != 1) {
        nvs_close(h);
        return false;
    }

    /* Lê para uma cópia local: *out só é tocado se tudo correr bem. */
    prov_creds_t tmp;
    err = get_str(h, KEY_SSID,      tmp.wifi_ssid,     sizeof tmp.wifi_ssid);
    if (err == ESP_OK) err = get_str(h, KEY_WIFI_PASS, tmp.wifi_password, sizeof tmp.wifi_password);
    if (err == ESP_OK) err = get_str(h, KEY_MQTT_URI,  tmp.mqtt_uri,      sizeof tmp.mqtt_uri);
    if (err == ESP_OK) err = get_str(h, KEY_MQTT_USER, tmp.mqtt_username, sizeof tmp.mqtt_username);
    if (err == ESP_OK) err = get_str(h, KEY_MQTT_PASS, tmp.mqtt_password, sizeof tmp.mqtt_password);
    nvs_close(h);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "credenciais corrompidas: %s", esp_err_to_name(err));
        return false;
    }

    *out = tmp;
    return true;
}

esp_err_t prov_erase(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(PROV_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_all(h);     /* apaga só as chaves deste namespace */
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

/* ---------------------------------------------------------------------------
 * SoftAP
 * ------------------------------------------------------------------------- */

#define PROV_AP_MAX_CONNECTIONS 2   /* 2 e não 1: evita ficar bloqueado por uma associação antiga do telemóvel */

static void softap_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        const wifi_event_ap_staconnected_t *e = data;
        ESP_LOGI(TAG, "cliente ligado   " MACSTR " (aid=%d)", MAC2STR(e->mac), e->aid);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        const wifi_event_ap_stadisconnected_t *e = data;
        ESP_LOGI(TAG, "cliente desligado " MACSTR " (aid=%d)", MAC2STR(e->mac), e->aid);
    } else if (base == IP_EVENT && id == IP_EVENT_AP_STAIPASSIGNED) {
        const ip_event_ap_staipassigned_t *e = data;
        ESP_LOGI(TAG, "IP atribuído " IPSTR " a " MACSTR, IP2STR(&e->ip), MAC2STR(e->mac));
    }
}

esp_err_t prov_start_softap(const prov_softap_config_t *cfg)
{
    ESP_RETURN_ON_FALSE(cfg != NULL && cfg->ssid_prefix != NULL && cfg->password != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "config inválida");
    size_t pass_len = strlen(cfg->password);
    ESP_RETURN_ON_FALSE(pass_len >= 8 && pass_len <= 63,
                        ESP_ERR_INVALID_ARG, TAG, "password do AP deve ter 8-63 caracteres");

    /* SSID único por board: prefixo + 2 últimos bytes do MAC */
    uint8_t mac[6];
    ESP_RETURN_ON_ERROR(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP), TAG, "esp_read_mac");

    wifi_config_t ap = {
        .ap = {
            .channel        = cfg->channel,
            .authmode       = WIFI_AUTH_WPA2_PSK,
            .max_connection = PROV_AP_MAX_CONNECTIONS,
        },
    };
    int n = snprintf((char *)ap.ap.ssid, sizeof ap.ap.ssid, "%s-%02X%02X",
                     cfg->ssid_prefix, mac[4], mac[5]);
    ESP_RETURN_ON_FALSE(n > 0 && n < (int)sizeof ap.ap.ssid,
                        ESP_ERR_INVALID_ARG, TAG, "ssid_prefix demasiado longo");
    ap.ap.ssid_len = (uint8_t)n;
    memcpy(ap.ap.password, cfg->password, pass_len + 1);

    /* Sequência padrão: netif -> wifi_init -> handlers -> modo -> config -> start */
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "esp_wifi_init");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "set_storage");   /* não persistir na NVS do Wi-Fi */

    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                   softap_event_handler, NULL), TAG, "handler wifi");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED,
                                                   softap_event_handler, NULL), TAG, "handler ip");

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "set_mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap), TAG, "set_config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi_start");

    ESP_LOGI(TAG, "AP ativo: ssid='%s' canal=%u (http://192.168.4.1)", ap.ap.ssid, ap.ap.channel);
    return ESP_OK;
}
