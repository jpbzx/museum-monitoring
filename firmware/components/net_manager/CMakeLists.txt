idf_component_register(
    SRCS "net_manager.c" "net_codec.c"
    INCLUDE_DIRS "include"
    REQUIRES app_types
    PRIV_REQUIRES esp_wifi esp_netif esp_event esp_timer mqtt json
)
