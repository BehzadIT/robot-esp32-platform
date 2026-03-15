idf_component_register(
    SRCS "bridge_network.c"
    INCLUDE_DIRS "include"
    REQUIRES diagnostics esp_event esp_http_server esp_netif esp_wifi
    PRIV_REQUIRES freertos esp_timer
)
