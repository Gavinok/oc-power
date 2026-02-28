idf_component_register(SRCS "main.c" "gap.c" "ble_power_service.c" "stroke_detector.c" "imu_power.c"
                       PRIV_REQUIRES bt nvs_flash
                       INCLUDE_DIRS ".")
