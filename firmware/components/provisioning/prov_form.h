#pragma once

/*
 * Parse e validação do formulário (application/x-www-form-urlencoded).
 * Interno ao componente (fora de include/) e sem dependências do ESP-IDF
 * além dos tipos de provisioning.h, para poder ser testado no PC.
 */

#include <stddef.h>
#include "provisioning.h"

typedef enum {
    PROV_FORM_OK = 0,
    PROV_FORM_BAD_ENCODING,         /* %XX inválido/truncado ou byte nulo */
    PROV_FORM_FIELD_TOO_LONG,
    PROV_FORM_SSID_INVALID,
    PROV_FORM_WIFI_PASS_INVALID,
    PROV_FORM_URI_INVALID,
} prov_form_result_t;

/**
 * Decodifica e valida o corpo do POST (não precisa de '\0' final).
 * Campos: ssid, wifi_pass, mqtt_uri, mqtt_user, mqtt_pass. Chaves desconhecidas
 * são ignoradas; chaves repetidas ficam com o último valor.
 * Em erro, *out é posto a zero.
 */
prov_form_result_t prov_form_parse(const char *body, size_t len, prov_creds_t *out);

/** Mensagem para o utilizador (texto fixo, sem dados do pedido). */
const char *prov_form_result_str(prov_form_result_t r);
