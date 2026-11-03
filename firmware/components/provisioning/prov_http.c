/*
 * Servidor HTTP do modo configuração.
 *
 *   GET  /      -> formulário (HTML em flash)
 *   POST /save  -> recebe application/x-www-form-urlencoded
 *
 * O POST é decodificado e validado por prov_form_parse(); se for válido grava
 * as credenciais na NVS e reinicia a board. Os valores nunca vão para o log.
 */

#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "prov_form.h"
#include "provisioning.h"

/* Com URL-encoding cada byte pode ocupar 3 caracteres; 5 campos de até 127 bytes
 * cabem folgadamente em 2 KiB. Acima disto rejeitamos (evita esgotar o heap). */
#define PROV_HTTP_MAX_BODY  2048

/* Dá tempo à resposta HTTP para chegar ao telemóvel antes de reiniciar. */
#define PROV_RESTART_DELAY_MS 1500

static const char *TAG = "prov_http";

static const char FORM_HTML[] =
    "<!DOCTYPE html><html lang=\"pt\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>Configuração do nó</title>"
    "<style>"
    "body{font-family:sans-serif;max-width:28rem;margin:1rem auto;padding:0 1rem}"
    "label{display:block;margin-top:1rem;font-weight:bold}"
    "input{width:100%;box-sizing:border-box;padding:.6rem;font-size:1rem}"
    "button{margin-top:1.5rem;width:100%;padding:.8rem;font-size:1rem}"
    "</style></head><body>"
    "<h1>Configuração do nó</h1>"
    "<form method=\"POST\" action=\"/save\">"
    "<h2>Wi-Fi</h2>"
    "<label>SSID<input name=\"ssid\" maxlength=\"32\" required></label>"
    "<label>Password<input name=\"wifi_pass\" type=\"password\" maxlength=\"63\"></label>"
    "<h2>MQTT</h2>"
    "<label>Broker URI<input name=\"mqtt_uri\" maxlength=\"127\" required "
    "placeholder=\"mqtt://192.168.1.10:1883\"></label>"
    "<label>Username<input name=\"mqtt_user\" maxlength=\"63\"></label>"
    "<label>Password<input name=\"mqtt_pass\" type=\"password\" maxlength=\"63\"></label>"
    "<button type=\"submit\">Guardar</button>"
    "</form></body></html>";

static esp_err_t form_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, FORM_HTML, HTTPD_RESP_USE_STRLEN);
}

/* 400 com a mensagem de erro (texto fixo) e ligação para voltar ao formulário já preenchido. */
static esp_err_t send_form_error(httpd_req_t *req, prov_form_result_t r)
{
    char page[512];
    snprintf(page, sizeof page,
             "<!DOCTYPE html><html lang=\"pt\"><meta charset=\"utf-8\">"
             "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
             "<body style=\"font-family:sans-serif;margin:1rem\">"
             "<h1>Configuração inválida</h1><p>%s</p>"
             "<p><a href=\"javascript:history.back()\">Voltar</a></p></body></html>",
             prov_form_result_str(r));

    httpd_resp_set_status(req, HTTPD_400);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_sendstr(req, page);
}

static esp_err_t save_post_handler(httpd_req_t *req)
{
    if (req->content_len == 0 || req->content_len > PROV_HTTP_MAX_BODY) {
        ESP_LOGW(TAG, "corpo rejeitado (content_len=%u)", (unsigned)req->content_len);
        return httpd_resp_send_err(req, HTTPD_413_CONTENT_TOO_LARGE, "Pedido demasiado grande");
    }

    char *body = malloc(req->content_len + 1);
    if (body == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, NULL);
    }

    /* httpd_req_recv pode devolver menos do que o pedido: ler até completar. */
    size_t received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, body + received, req->content_len - received);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;               /* timeout parcial: tentar de novo */
        }
        if (n <= 0) {
            ESP_LOGW(TAG, "ligação perdida a meio do corpo");
            free(body);
            return ESP_FAIL;
        }
        received += n;
    }
    body[received] = '\0';

    prov_creds_t creds;
    prov_form_result_t r = prov_form_parse(body, received, &creds);
    free(body);

    if (r != PROV_FORM_OK) {
        ESP_LOGW(TAG, "formulário rejeitado: %s", prov_form_result_str(r));
        return send_form_error(req, r);
    }

    esp_err_t err = prov_save(&creds);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Falha a gravar a configuração");
    }
    ESP_LOGI(TAG, "configuração gravada (ssid='%s', broker='%s'), a reiniciar",
             creds.wifi_ssid, creds.mqtt_uri);

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr(req, "<!DOCTYPE html><html lang=\"pt\"><meta charset=\"utf-8\">"
                            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                            "<body style=\"font-family:sans-serif;margin:1rem\">"
                            "<h1>Configuração guardada</h1>"
                            "<p>O nó vai reiniciar e ligar-se à rede indicada.</p></body></html>");

    vTaskDelay(pdMS_TO_TICKS(PROV_RESTART_DELAY_MS));
    esp_restart();
    return ESP_OK;      /* inalcançável */
}

esp_err_t prov_http_start(void)
{
    static httpd_handle_t server = NULL;
    if (server != NULL) {
        return ESP_OK;
    }

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    esp_err_t err = httpd_start(&server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start: %s", esp_err_to_name(err));
        server = NULL;
        return err;
    }

    static const httpd_uri_t uri_form = { .uri = "/",     .method = HTTP_GET,  .handler = form_get_handler };
    static const httpd_uri_t uri_save = { .uri = "/save", .method = HTTP_POST, .handler = save_post_handler };
    httpd_register_uri_handler(server, &uri_form);
    httpd_register_uri_handler(server, &uri_save);

    ESP_LOGI(TAG, "servidor HTTP ativo na porta %d", cfg.server_port);
    return ESP_OK;
}
