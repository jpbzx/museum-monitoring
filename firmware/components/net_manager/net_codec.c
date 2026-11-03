#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "net_codec.h"

/* Chave JSON e casas decimais de cada grandeza (índice = meas_type_t). */
static const struct {
    const char *key;
    int         decimals;
} MEAS_JSON[MEAS_COUNT] = {
    [MEAS_TEMPERATURE] = { "temperature_c",   2 },
    [MEAS_HUMIDITY]    = { "humidity_pct",    2 },
    [MEAS_ILLUMINANCE] = { "illuminance_lux", 1 },
    [MEAS_CO2]         = { "co2_ppm",         0 },
    [MEAS_VOC_INDEX]   = { "voc_index",       0 },
    [MEAS_BATTERY]     = { "battery_mv",      0 },
};

int net_codec_telemetry_json(const telemetry_msg_t *msg, char *buf, size_t cap)
{
    int n = snprintf(buf, cap, "{\"seq\":%" PRIu32 ",\"ts_us\":%" PRId64, msg->seq, msg->timestamp_us);
    if (n < 0 || (size_t)n >= cap) {
        return -1;
    }
    size_t o = (size_t)n;

    for (int t = 0; t < MEAS_COUNT; t++) {
        float v = msg->data.value[t];
        if (!(msg->data.valid_mask & MEAS_BIT(t)) || !isfinite(v)) {
            continue;       /* NaN/inf não existem em JSON */
        }
        n = snprintf(buf + o, cap - o, ",\"%s\":%.*f", MEAS_JSON[t].key, MEAS_JSON[t].decimals, (double)v);
        if (n < 0 || (size_t)n >= cap - o) {
            return -1;
        }
        o += (size_t)n;
    }

    if (o + 2 > cap) {      /* '}' e '\0' */
        return -1;
    }
    buf[o++] = '}';
    buf[o] = '\0';
    return (int)o;
}

static app_cmd_t *next_cmd(app_cmd_t *cmds, size_t max, size_t *n)
{
    if (*n >= max) {
        return NULL;
    }
    app_cmd_t *c = &cmds[(*n)++];
    memset(c, 0, sizeof *c);
    return c;
}

static bool parse_power_mode(const cJSON *item, power_mode_t *out)
{
    if (!cJSON_IsString(item)) return false;
    if (strcmp(item->valuestring, "performance") == 0) { *out = POWER_MODE_PERFORMANCE; return true; }
    if (strcmp(item->valuestring, "balanced") == 0)    { *out = POWER_MODE_BALANCED;    return true; }
    if (strcmp(item->valuestring, "ultra_low") == 0)   { *out = POWER_MODE_ULTRA_LOW;   return true; }
    return false;
}

int net_codec_parse_config(const char *json, size_t len, app_cmd_t *cmds, size_t max_cmds)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (root == NULL) {
        return NET_CODEC_ERR_JSON;
    }
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return NET_CODEC_ERR_JSON;
    }

    size_t n = 0;
    int result = NET_CODEC_ERR_VALUE;       /* qualquer saída antecipada por "goto bad" é valor inválido */
    const cJSON *item;
    app_cmd_t *c;

    if ((item = cJSON_GetObjectItemCaseSensitive(root, "sample_period_s")) != NULL) {
        if (!cJSON_IsNumber(item) || item->valuedouble != floor(item->valuedouble) ||
            item->valuedouble < APP_SAMPLE_PERIOD_MIN_S || item->valuedouble > APP_SAMPLE_PERIOD_MAX_S ||
            (c = next_cmd(cmds, max_cmds, &n)) == NULL) {
            goto out;
        }
        c->type = CMD_SET_SAMPLE_PERIOD;
        c->sample_period_s = (uint32_t)item->valuedouble;
    }

    if ((item = cJSON_GetObjectItemCaseSensitive(root, "power_mode")) != NULL) {
        power_mode_t mode;
        if (!parse_power_mode(item, &mode) || (c = next_cmd(cmds, max_cmds, &n)) == NULL) {
            goto out;
        }
        c->type = CMD_SET_POWER_MODE;
        c->power_mode = mode;
    }

    if ((item = cJSON_GetObjectItemCaseSensitive(root, "sensor")) != NULL) {
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
        const cJSON *enabled = cJSON_GetObjectItemCaseSensitive(item, "enabled");
        if (!cJSON_IsObject(item) || !cJSON_IsString(name) || !cJSON_IsBool(enabled) ||
            name->valuestring[0] == '\0' || strlen(name->valuestring) >= APP_SENSOR_NAME_LEN ||
            (c = next_cmd(cmds, max_cmds, &n)) == NULL) {
            goto out;
        }
        c->type = CMD_SET_SENSOR_ENABLED;
        strcpy(c->sensor.name, name->valuestring);      /* comprimento verificado acima */
        c->sensor.enabled = cJSON_IsTrue(enabled);
    }

    if ((item = cJSON_GetObjectItemCaseSensitive(root, "report_config")) != NULL) {
        if (!cJSON_IsBool(item)) {
            goto out;
        }
        if (cJSON_IsTrue(item)) {
            if ((c = next_cmd(cmds, max_cmds, &n)) == NULL) {
                goto out;
            }
            c->type = CMD_REPORT_CONFIG;
        }
    }

    result = (int)n;
out:
    cJSON_Delete(root);
    return result;
}
