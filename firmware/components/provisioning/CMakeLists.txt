idf_component_register(
    SRCS "provisioning.c" "prov_http.c" "prov_form.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES nvs_flash esp_wifi esp_netif esp_event esp_http_server
)
