#include "platform_runtime.h"

void app_main(void)
{
    // `platform_runtime_start()` is the ESP32 composition root. It wires up
    // telemetry bridging and controller transport, then hands control to the
    // long-running Bluetooth/controller runtime without taking motor authority
    // away from the Pico.
    platform_runtime_start();
}
