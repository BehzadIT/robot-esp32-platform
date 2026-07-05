idf_component_register(
    SRCS "controller_transport.c"
    INCLUDE_DIRS "include"
    REQUIRES controller_snapshot controller_gateway
    PRIV_REQUIRES driver esp_timer freertos
)
