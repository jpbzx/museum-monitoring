idf_component_register(
    SRCS "app_logic.c"
    INCLUDE_DIRS "include"
    REQUIRES app_types sensor_manager
    PRIV_REQUIRES esp_timer
)
