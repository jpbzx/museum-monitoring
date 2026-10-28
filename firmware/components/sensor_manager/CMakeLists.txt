# Isolamento:
#   include/      -> API pública (o que os outros componentes podem ver)
#   src/          -> implementação e drivers, invisíveis fora do componente
#   REQUIRES      -> dependências expostas nos headers públicos (propagam-se a quem nos usa)
#   PRIV_REQUIRES -> dependências usadas só nos .c (não se propagam)
idf_component_register(
    SRCS
        "src/sensor_manager.c"
        "src/drivers/drv_sht4x.c"
        "src/drivers/drv_bh1750.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src/drivers"
    REQUIRES app_types esp_driver_gpio esp_driver_i2c
)
