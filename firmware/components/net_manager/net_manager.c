/*
 * Rede (Core 0).
 *
 * Wi-Fi STA com reconexão: tenta para sempre, com backoff exponencial limitado
 * (a board está num museu, sem ninguém para a reiniciar). APP_EVT_WIFI_UP no
 * event group enquanto houver IP.
 *
 * MQTT (arranca depois do primeiro IP; o cliente reconecta sozinho):
 *   - Last Will "offline" (retained) em topics.status; "online" ao ligar
 *   - subscreve topics.config_set: JSON -> app_cmd_t -> command_q (net_codec.h)
 *   - publica telemetry_q em topics.telemetry; só consome a fila com o MQTT ligado,
 *     por isso as amostras acumulam-se na fila enquanto a rede está em baixo
 *   - APP_EVT_MQTT_UP enquanto ligado; APP_EVT_TX_IDLE quando a fila está vazia e
 *     o outbox do cliente (mensagens QoS 1 sem PUBACK) também
 *
 * TODO: publicar topics.config_state (precisa de um canal de retorno app_logic -> net);
 *       mqtts:// (certificados).
 */

#include <inttypes.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "net_codec.h"
#include "net_manager.h"

static const char *TAG = "net";

#define MQTT_STATUS_ONLINE      "online"
#define MQTT_STATUS_OFFLINE     "offline"

/* Estado partilhado com os handlers de eventos (event loop do sistema e tarefa do MQTT). */
static const net_manager_config_t *s_cfg;
static esp_mqtt_client_handle_t    s_mqtt;
static EventGroupHandle_t   s_events;
static esp_timer_handle_t   s_reconnect_timer;
static uint32_t             s_backoff_ms;
static uint32_t             s_backoff_min_ms;
static uint32_t             s_backoff_max_ms;

static void reconnect_timer_cb(void *arg)
{
    esp_wifi_connect();
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "a ligar ao Wi-Fi...");
        esp_wifi_connect();

    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *e = data;
        xEventGroupClearBits(s_events, APP_EVT_WIFI_UP);
        ESP_LOGW(TAG, "Wi-Fi sem ligação (motivo %d), nova tentativa em %" PRIu32 " ms",
                 e->reason, s_backoff_ms);

        /* Não bloquear o event loop: agenda a tentativa e duplica o intervalo até ao máximo. */
        esp_timer_start_once(s_reconnect_timer, (uint64_t)s_backoff_ms * 1000ULL);
        s_backoff_ms = (s_backoff_ms * 2 > s_backoff_max_ms) ? s_backoff_max_ms : s_backoff_ms * 2;

    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        ESP_LOGI(TAG, "Wi-Fi ligado, IP " IPSTR, IP2STR(&e->ip_info.ip));
        s_backoff_ms = s_backoff_min_ms;
        xEventGroupSetBits(s_events, APP_EVT_WIFI_UP);
    }
}

/* Requer esp_netif_init() e esp_event_loop_create_default() já feitos (main.c). */
static void wifi_start_sta(const net_manager_config_t *cfg)
{
    s_cfg            = cfg;
    s_events         = cfg->ctx->events;
    s_backoff_min_ms = cfg->wifi_backoff_min_ms;
    s_backoff_max_ms = cfg->wifi_backoff_max_ms;
    s_backoff_ms     = s_backoff_min_ms;

    const esp_timer_create_args_t timer_args = {
        .callback = reconnect_timer_cb,
        .name     = "wifi_reconnect",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_reconnect_timer));

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));

    /* Sem isto o esp_wifi guarda SSID/password por conta própria na NVS (nvs.net80211),
     * uma cópia que o prov_erase() não apaga. A fonte única é a nossa partição "prov". */
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));

    wifi_config_t sta = { 0 };
    strlcpy((char *)sta.sta.ssid, cfg->wifi_ssid, sizeof sta.sta.ssid);
    strlcpy((char *)sta.sta.password, cfg->wifi_password, sizeof sta.sta.password);
    /* Com password, exige pelo menos WPA2: impede ligar a uma rede aberta com o mesmo SSID. */
    sta.sta.threshold.authmode = (cfg->wifi_password[0] != '\0') ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    sta.sta.pmf_cfg.capable    = true;
    sta.sta.pmf_cfg.required   = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    ESP_ERROR_CHECK(esp_wifi_start());
}

/* ---------------------------------------------------------------------------
 * MQTT
 * ------------------------------------------------------------------------- */

/* TX_IDLE: nada à espera na fila e nada por confirmar no outbox do cliente. */
static void update_tx_idle(void)
{
    bool idle = uxQueueMessagesWaiting(s_cfg->ctx->telemetry_q) == 0 &&
                esp_mqtt_client_get_outbox_size(s_mqtt) == 0;
    if (idle) {
        xEventGroupSetBits(s_events, APP_EVT_TX_IDLE);
    } else {
        xEventGroupClearBits(s_events, APP_EVT_TX_IDLE);
    }
}

static void handle_config_set(const esp_mqtt_event_handle_t e)
{
    /* Mensagens fragmentadas ou demasiado grandes não são comandos legítimos. */
    if (e->data_len != e->total_data_len || e->total_data_len > NET_CODEC_MAX_CONFIG_LEN) {
        ESP_LOGW(TAG, "config/set ignorado: %d B (máx. %d, sem fragmentos)",
                 e->total_data_len, NET_CODEC_MAX_CONFIG_LEN);
        return;
    }
    if (e->topic_len != (int)strlen(s_cfg->topics.config_set) ||
        memcmp(e->topic, s_cfg->topics.config_set, (size_t)e->topic_len) != 0) {
        return;
    }

    app_cmd_t cmds[NET_CODEC_MAX_CMDS];
    int n = net_codec_parse_config(e->data, (size_t)e->data_len, cmds, NET_CODEC_MAX_CMDS);
    if (n < 0) {
        ESP_LOGW(TAG, "config/set rejeitado: %s",
                 n == NET_CODEC_ERR_JSON ? "JSON inválido" : "valor inválido");
        return;
    }

    for (int i = 0; i < n; i++) {
        if (xQueueSend(s_cfg->ctx->command_q, &cmds[i], 0) != pdPASS) {
            ESP_LOGW(TAG, "command_q cheia, comando %d descartado", i);
        }
    }
    ESP_LOGI(TAG, "config/set: %d comando(s) enviados à lógica", n);
}

static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    esp_mqtt_event_handle_t e = data;

    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT ligado");
        esp_mqtt_client_publish(e->client, s_cfg->topics.status, MQTT_STATUS_ONLINE, 0, 1, 1);
        esp_mqtt_client_subscribe(e->client, s_cfg->topics.config_set, 1);
        xEventGroupSetBits(s_events, APP_EVT_MQTT_UP);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT desligado");
        xEventGroupClearBits(s_events, APP_EVT_MQTT_UP | APP_EVT_TX_IDLE);
        break;

    case MQTT_EVENT_PUBLISHED:
        update_tx_idle();
        break;

    case MQTT_EVENT_DATA:
        handle_config_set(e);
        break;

    case MQTT_EVENT_ERROR:
        if (e->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            ESP_LOGE(TAG, "broker recusou a ligação (código %d): credenciais?",
                     e->error_handle->connect_return_code);
        } else if (e->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGW(TAG, "MQTT sem transporte TCP (errno %d)", e->error_handle->esp_transport_sock_errno);
        }
        break;

    default:
        break;
    }
}

static void mqtt_init(const net_manager_config_t *cfg)
{
    if (strncmp(cfg->broker_uri, "mqtts://", 8) == 0) {
        ESP_LOGW(TAG, "mqtts:// ainda não suportado (sem certificados): a ligação vai falhar");
    }

    /* Strings vazias viram NULL: sem username não se envia autenticação. */
    const esp_mqtt_client_config_t mc = {
        .broker.address.uri                  = cfg->broker_uri,
        .credentials.client_id               = cfg->client_id,
        .credentials.username                = cfg->mqtt_username[0] ? cfg->mqtt_username : NULL,
        .credentials.authentication.password = cfg->mqtt_password[0] ? cfg->mqtt_password : NULL,
        .session.keepalive                   = cfg->mqtt_keepalive_s,
        .session.last_will = {
            .topic   = cfg->topics.status,
            .msg     = MQTT_STATUS_OFFLINE,
            .msg_len = sizeof MQTT_STATUS_OFFLINE - 1,
            .qos     = 1,
            .retain  = 1,
        },
    };
    s_mqtt = esp_mqtt_client_init(&mc);        /* copia as strings */
    ESP_ERROR_CHECK(s_mqtt == NULL ? ESP_FAIL : ESP_OK);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_mqtt, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
}

void net_manager_task(void *arg)
{
    const net_manager_config_t *cfg = arg;
    const app_ctx_t *ctx = cfg->ctx;

    wifi_start_sta(cfg);
    mqtt_init(cfg);

    /* O cliente só arranca com rede; depois trata das reconexões sozinho. */
    xEventGroupWaitBits(ctx->events, APP_EVT_WIFI_UP, pdFALSE, pdTRUE, portMAX_DELAY);
    ESP_ERROR_CHECK(esp_mqtt_client_start(s_mqtt));

    char json[NET_CODEC_TELEMETRY_MAX];
    telemetry_msg_t msg;
    for (;;) {
        /* Sem MQTT não se consome a fila: as amostras esperam lá (ver APP_TELEMETRY_QUEUE_LEN). */
        xEventGroupWaitBits(ctx->events, APP_EVT_MQTT_UP, pdFALSE, pdTRUE, portMAX_DELAY);

        if (xQueueReceive(ctx->telemetry_q, &msg, pdMS_TO_TICKS(500)) == pdPASS) {
            int len = net_codec_telemetry_json(&msg, json, sizeof json);
            if (len < 0) {
                ESP_LOGE(TAG, "telemetria seq=%" PRIu32 " não cabe no buffer, descartada", msg.seq);
                continue;
            }
            int id = esp_mqtt_client_publish(s_mqtt, cfg->topics.telemetry, json, len, cfg->telemetry_qos, 0);
            if (id < 0) {
                /* Ligação caiu entre o wait e o publish: devolve a amostra ao início da fila. */
                xQueueSendToFront(ctx->telemetry_q, &msg, 0);
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            ESP_LOGI(TAG, "publicado %s seq=%" PRIu32 " (%d B)", cfg->topics.telemetry, msg.seq, len);
        }
        update_tx_idle();
    }
}
