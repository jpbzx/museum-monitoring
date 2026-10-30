/*
 * Ponto de composição do firmware.
 *
 * Arquitetura:
 *   Core 0 (APP_CORE_NET) : net_manager_task  — Wi-Fi + MQTT
 *   Core 1 (APP_CORE_APP) : app_logic_task    — amostragem (sensor_manager) + lógica
 *
 *   app_logic --[telemetry_q: telemetry_msg_t]--> net_manager
 *   app_logic <--[command_q:  app_cmd_t      ]--  net_manager
 *   events (EventGroup): estado da rede, usado pela lógica para decidir quando dormir
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "app_config.h"
#include "app_types.h"
#include "app_logic.h"
#include "net_manager.h"
#include "user_button.h"
#include "provisioning.h"

static const char *TAG = "main";

/* Pressão longa: esquece a configuração e reinicia; no próximo boot a board arranca em modo configuração. */
static void on_button_long_press(void *ctx)
{
    ESP_LOGW(TAG, "botão: a apagar credenciais e a reiniciar");
    esp_err_t err = prov_erase();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "prov_erase falhou: %s", esp_err_to_name(err));
    }
    esp_restart();
}

/*
 * Board sem credenciais: modo configuração. O AP fica a correr em background.
 * O POST /save valida, grava na NVS e reinicia a board (em modo normal no próximo boot).
 */
static void enter_provisioning_mode(void)
{
    ESP_LOGW(TAG, "board não provisionada: modo configuração");

    const prov_softap_config_t ap = {
        .ssid_prefix = APP_PROV_AP_SSID_PREFIX,
        .password    = APP_PROV_AP_PASSWORD,
        .channel     = APP_PROV_AP_CHANNEL,
    };
    ESP_ERROR_CHECK(prov_start_softap(&ap));
    ESP_ERROR_CHECK(prov_http_start());
}

/* Duração estática: as tarefas guardam ponteiros para estas estruturas. */
static app_ctx_t s_ctx;

static const app_logic_config_t s_logic_cfg = {
    .ctx = &s_ctx,
    .sensors = {
        .i2c_sda     = APP_PIN_I2C_SDA,
        .i2c_scl     = APP_PIN_I2C_SCL,
        .i2c_freq_hz = APP_I2C_FREQ_HZ,
        .power_en    = APP_PIN_SENSOR_PWR_EN,
    },
    .sample_period_s = APP_SAMPLE_PERIOD_DEFAULT_S,
    .power_mode      = APP_POWER_MODE_DEFAULT,
};

/* Credenciais lidas da NVS por prov_load() antes de arrancar a rede. Têm de ter duração
 * estática: o net_manager guarda ponteiros para estes buffers (o endereço de um membro
 * de um objeto estático é constante, por isso serve de inicializador). */
static prov_creds_t s_creds;

static const net_manager_config_t s_net_cfg = {
    .ctx           = &s_ctx,
    .wifi_ssid     = s_creds.wifi_ssid,
    .wifi_password = s_creds.wifi_password,
    .broker_uri    = s_creds.mqtt_uri,
    .mqtt_username = s_creds.mqtt_username,
    .mqtt_password = s_creds.mqtt_password,
    .wifi_backoff_min_ms = APP_WIFI_BACKOFF_MIN_MS,
    .wifi_backoff_max_ms = APP_WIFI_BACKOFF_MAX_MS,
    .client_id           = APP_DEVICE_ID,
    .mqtt_keepalive_s    = APP_MQTT_KEEPALIVE_S,
    .telemetry_qos       = APP_MQTT_QOS_TELEMETRY,
    .topics = {
        .telemetry    = APP_MQTT_TOPIC_TELEMETRY,
        .status       = APP_MQTT_TOPIC_STATUS,
        .config_set   = APP_MQTT_TOPIC_CONFIG_SET,
        .config_state = APP_MQTT_TOPIC_CONFIG_STATE,
    },
};

static esp_err_t nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS inválida (%s), a apagar partição", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

static esp_err_t ipc_init(app_ctx_t *ctx)
{
    ctx->telemetry_q = xQueueCreate(APP_TELEMETRY_QUEUE_LEN, sizeof(telemetry_msg_t));
    ctx->command_q   = xQueueCreate(APP_COMMAND_QUEUE_LEN, sizeof(app_cmd_t));
    ctx->events      = xEventGroupCreate();

    if (ctx->telemetry_q == NULL || ctx->command_q == NULL || ctx->events == NULL) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void start_task(TaskFunction_t fn, const char *name, uint32_t stack,
                       const void *arg, UBaseType_t prio, BaseType_t core)
{
    BaseType_t ok = xTaskCreatePinnedToCore(fn, name, stack, (void *)arg, prio, NULL, core);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "falha a criar tarefa '%s' (heap livre: %u B)",
                 name, (unsigned)xPortGetFreeHeapSize());
        abort();
    }
    ESP_LOGI(TAG, "tarefa '%s' criada no core %d", name, (int)core);
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_init());

    const user_button_config_t btn_cfg = {
        .pin           = APP_PIN_USER_BUTTON,
        .active_low    = APP_BUTTON_ACTIVE_LOW,
        .long_press_ms = APP_BUTTON_LONG_PRESS_MS,
        .on_long_press = on_button_long_press,
    };
    ESP_ERROR_CHECK(user_button_start(&btn_cfg));   /* a config é copiada */

    /* Singletons de sistema necessários ao Wi-Fi (STA e AP); criados aqui uma única vez. */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Sem credenciais não arrancamos rede nem lógica: ficamos em modo configuração. */
    if (!prov_load(&s_creds)) {
        enter_provisioning_mode();      /* o AP corre em background; app_main retorna */
        return;
    }
    ESP_LOGI(TAG, "board provisionada (ssid='%s', broker='%s')", s_creds.wifi_ssid, s_creds.mqtt_uri);

    ESP_ERROR_CHECK(ipc_init(&s_ctx));

    start_task(net_manager_task, APP_TASK_NET_NAME, APP_TASK_NET_STACK,
               &s_net_cfg, APP_TASK_NET_PRIO, APP_CORE_NET);
    start_task(app_logic_task, APP_TASK_APP_NAME, APP_TASK_APP_STACK,
               &s_logic_cfg, APP_TASK_APP_PRIO, APP_CORE_APP);

    /* app_main retorna: a main task é removida e os recursos ficam com as tarefas. */
}
