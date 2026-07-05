idf_component_register(
    SRCS "platform_runtime.c"
    INCLUDE_DIRS "include"
    REQUIRES controller_gateway controller_transport telemetry_bridge
    PRIV_REQUIRES nvs_flash
)
