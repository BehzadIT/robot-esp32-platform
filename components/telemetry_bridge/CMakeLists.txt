idf_component_register(
    SRCS "telemetry_bridge.c"
    INCLUDE_DIRS "include"
    REQUIRES diagnostics network
    PRIV_REQUIRES freertos nvs_flash esp_driver_gpio esp_driver_uart esp_timer
)
