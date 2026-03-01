idf_component_register(SRCS "main.c" "gap.c" "ble_power_service.c" "stroke_detector.c" "imu_power.c"
                             "wifi_log_server.c"
                       PRIV_REQUIRES bt nvs_flash esp_wifi esp_http_server esp_event esp_netif
                       INCLUDE_DIRS ".")
