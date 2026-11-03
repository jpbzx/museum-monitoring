#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "prov_form.h"

typedef struct {
    const char *name;
    size_t      offset;
    size_t      size;       /* capacidade do campo, com '\0' */
} field_t;

static const field_t FIELDS[] = {
    { "ssid",      offsetof(prov_creds_t, wifi_ssid),     PROV_SSID_MAX },
    { "wifi_pass", offsetof(prov_creds_t, wifi_password), PROV_WIFI_PASS_MAX },
    { "mqtt_uri",  offsetof(prov_creds_t, mqtt_uri),      PROV_MQTT_URI_MAX },
    { "mqtt_user", offsetof(prov_creds_t, mqtt_username), PROV_MQTT_USER_MAX },
    { "mqtt_pass", offsetof(prov_creds_t, mqtt_password), PROV_MQTT_PASS_MAX },
};

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* '+' -> ' ', %XX -> byte. Escreve em dst (capacidade cap, com '\0'). */
static prov_form_result_t url_decode(const char *src, size_t len, char *dst, size_t cap)
{
    size_t o = 0;
    for (size_t i = 0; i < len; i++) {
        char c = src[i];
        if (c == '+') {
            c = ' ';
        } else if (c == '%') {
            if (i + 2 >= len) {
                return PROV_FORM_BAD_ENCODING;
            }
            int hi = hex_val(src[i + 1]);
            int lo = hex_val(src[i + 2]);
            if (hi < 0 || lo < 0) {
                return PROV_FORM_BAD_ENCODING;
            }
            c = (char)((hi << 4) | lo);
            if (c == '\0') {
                return PROV_FORM_BAD_ENCODING;      /* um '\0' truncaria a string */
            }
            i += 2;
        }
        if (o >= cap - 1) {
            return PROV_FORM_FIELD_TOO_LONG;
        }
        dst[o++] = c;
    }
    dst[o] = '\0';
    return PROV_FORM_OK;
}

static bool has_prefix(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static prov_form_result_t validate(const prov_creds_t *c)
{
    if (c->wifi_ssid[0] == '\0') {
        return PROV_FORM_SSID_INVALID;
    }

    /* vazia = rede aberta; o máximo (63) já é garantido pelo tamanho do campo */
    size_t pass_len = strlen(c->wifi_password);
    if (pass_len != 0 && pass_len < 8) {
        return PROV_FORM_WIFI_PASS_INVALID;
    }

    const char *uri = c->mqtt_uri;
    size_t prefix_len = has_prefix(uri, "mqtts://") ? 8 : has_prefix(uri, "mqtt://") ? 7 : 0;
    if (prefix_len == 0 || uri[prefix_len] == '\0') {
        return PROV_FORM_URI_INVALID;
    }
    for (const char *p = uri; *p != '\0'; p++) {
        if ((unsigned char)*p <= ' ') {         /* espaços e caracteres de controlo */
            return PROV_FORM_URI_INVALID;
        }
    }
    return PROV_FORM_OK;
}

static prov_form_result_t parse_fields(const char *body, size_t len, prov_creds_t *out)
{
    size_t pos = 0;
    while (pos < len) {
        size_t end = pos;
        while (end < len && body[end] != '&') {
            end++;
        }

        const char *pair = body + pos;
        size_t pair_len = end - pos;
        const char *eq = memchr(pair, '=', pair_len);
        if (eq != NULL) {
            size_t key_len = (size_t)(eq - pair);
            for (size_t i = 0; i < sizeof FIELDS / sizeof FIELDS[0]; i++) {
                if (strlen(FIELDS[i].name) == key_len && memcmp(FIELDS[i].name, pair, key_len) == 0) {
                    prov_form_result_t r = url_decode(eq + 1, pair_len - key_len - 1,
                                                      (char *)out + FIELDS[i].offset, FIELDS[i].size);
                    if (r != PROV_FORM_OK) {
                        return r;
                    }
                    break;
                }
            }
        }
        pos = end + 1;
    }
    return validate(out);
}

prov_form_result_t prov_form_parse(const char *body, size_t len, prov_creds_t *out)
{
    memset(out, 0, sizeof *out);
    prov_form_result_t r = parse_fields(body, len, out);
    if (r != PROV_FORM_OK) {
        memset(out, 0, sizeof *out);
    }
    return r;
}

const char *prov_form_result_str(prov_form_result_t r)
{
    switch (r) {
    case PROV_FORM_OK:                return "ok";
    case PROV_FORM_BAD_ENCODING:      return "Dados do formulário inválidos.";
    case PROV_FORM_FIELD_TOO_LONG:    return "Um dos campos excede o tamanho máximo.";
    case PROV_FORM_SSID_INVALID:      return "O SSID é obrigatório.";
    case PROV_FORM_WIFI_PASS_INVALID: return "A password Wi-Fi deve ter 8 a 63 caracteres (ou ficar vazia para rede aberta).";
    case PROV_FORM_URI_INVALID:       return "O URI do broker deve começar por mqtt:// ou mqtts://.";
    }
    return "Erro desconhecido.";
}
