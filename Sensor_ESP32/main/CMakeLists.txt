idf_component_register(SRCS "app_main.c" "shtc3_sensor.c"
                    PRIV_REQUIRES mqtt nvs_flash esp_netif driver
                    INCLUDE_DIRS ".")
