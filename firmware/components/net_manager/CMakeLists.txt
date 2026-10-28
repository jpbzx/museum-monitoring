# TODO: acrescentar esp_wifi, mqtt e json a PRIV_REQUIRES ao implementar.
idf_component_register(
    SRCS "net_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES app_types
)
