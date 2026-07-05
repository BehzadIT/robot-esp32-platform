idf_component_register(
    SRCS "controller_gateway.c"
    INCLUDE_DIRS "include"
    REQUIRES controller_snapshot bluepad32 btstack
    PRIV_REQUIRES esp_timer freertos
)
