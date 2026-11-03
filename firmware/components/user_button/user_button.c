/*
 * Deteção de pressão longa por polling.
 *
 * A tarefa lê o pino a cada USER_BUTTON_POLL_MS. Enquanto o botão está premido
 * acumula o tempo; a pressão só "acaba" depois de USER_BUTTON_RELEASE_SAMPLES
 * leituras seguidas em repouso (debounce de largada), para que um ressalto
 * durante os 6 s não reinicie a contagem.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "user_button.h"

#define USER_BUTTON_POLL_MS         20
#define USER_BUTTON_RELEASE_SAMPLES 3       /* 3 x 20 ms = 60 ms */
#define USER_BUTTON_TASK_STACK      3072
#define USER_BUTTON_TASK_PRIO       2

static const char *TAG = "button";

static user_button_config_t s_cfg;      /* cópia: a tarefa vive mais do que o chamador */

static inline bool is_pressed(void)
{
    int level = gpio_get_level(s_cfg.pin);
    return s_cfg.active_low ? (level == 0) : (level == 1);
}

static void user_button_task(void *arg)
{
    uint32_t held_ms = 0;
    uint32_t idle_samples = 0;
    bool fired = false;

    for (;;) {
        if (is_pressed()) {
            idle_samples = 0;
            if (held_ms == 0) {
                ESP_LOGI(TAG, "premido");
            }
            held_ms += USER_BUTTON_POLL_MS;

            if (!fired && held_ms >= s_cfg.long_press_ms) {
                fired = true;
                ESP_LOGW(TAG, "pressão longa (%u ms)", (unsigned)held_ms);
                if (s_cfg.on_long_press != NULL) {
                    s_cfg.on_long_press(s_cfg.ctx);
                }
            }
        } else if (held_ms > 0 && ++idle_samples >= USER_BUTTON_RELEASE_SAMPLES) {
            ESP_LOGI(TAG, "largado após %u ms%s", (unsigned)held_ms,
                     fired ? "" : " (pressão curta, ignorada)");
            held_ms = 0;
            idle_samples = 0;
            fired = false;      /* rearma para a próxima pressão */
        }

        vTaskDelay(pdMS_TO_TICKS(USER_BUTTON_POLL_MS));
    }
}

esp_err_t user_button_start(const user_button_config_t *cfg)
{
    if (cfg == NULL || !GPIO_IS_VALID_GPIO(cfg->pin) || cfg->long_press_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    s_cfg = *cfg;

    /* GPIO34-39 (ex.: o 35) são só de entrada e NÃO têm pull interno: exigem resistência externa. */
    bool has_pull = GPIO_IS_VALID_OUTPUT_GPIO(cfg->pin);
    if (!has_pull) {
        ESP_LOGW(TAG, "GPIO%d sem pull interno: confirmar resistência externa", (int)cfg->pin);
    }

    const gpio_config_t io = {
        .pin_bit_mask = 1ULL << cfg->pin,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = (has_pull && cfg->active_low)  ? GPIO_PULLUP_ENABLE   : GPIO_PULLUP_DISABLE,
        .pull_down_en = (has_pull && !cfg->active_low) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }

    BaseType_t ok = xTaskCreate(user_button_task, "button", USER_BUTTON_TASK_STACK,
                                NULL, USER_BUTTON_TASK_PRIO, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
