idf_component_register(SRCS "main.c" "gap.c" "ble_power_service.c"
                       PRIV_REQUIRES bt nvs_flash
                       INCLUDE_DIRS ".")
