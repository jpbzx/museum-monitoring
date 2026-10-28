# 'main' é apenas o ponto de composição: lê app_config.h, cria o IPC e arranca as tarefas.
# Nenhum componente pode depender de 'main', por isso tudo o que é partilhado vive em components/.
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        nvs_flash
        esp_netif
        esp_event
        esp_driver_gpio
        app_types
        sensor_manager
        app_logic
        net_manager
)
