#include "platform_runtime.h"

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>

#include "controller_gateway.h"
#include "controller_transport.h"
#include "telemetry_bridge.h"

#define BLUEPAD32_VENDOR_COMMIT "e9b755faabc240585da42e6d26164bb2cdd064d3"
#define BTSTACK_VENDOR_COMMIT "5d4d8cc7b1d35a90bbd6d5ffd2d3050b2bfc861c"

static const char* TAG = "PLATFORM_RT";

static void init_nvs(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void platform_runtime_start(void) {
    // This function is the ESP32 composition root: it starts finite platform
    // services and then transfers control to the long-lived Bluepad32/BTstack
    // run loop. Keeping that wiring here makes the startup ownership boundary
    // explicit and avoids implying that the controller gateway or transport
    // owns robot-drive semantics.
    ESP_LOGI(TAG, "Starting robot ESP32 platform runtime");
    ESP_LOGI(TAG, "Bluepad32 commit=%s btstack commit=%s", BLUEPAD32_VENDOR_COMMIT, BTSTACK_VENDOR_COMMIT);

    init_nvs();

    // Start finite ESP-IDF services before entering the Bluepad32/BTstack run
    // loop. The controller gateway is an adapter layer on the ESP32 side, not
    // a robot-control owner, so composition stays here rather than inside the
    // Pico-facing logic that will come later.
    telemetry_bridge_start();

    if (!controller_gateway_start()) {
        ESP_LOGE(TAG, "Failed to start controller gateway");
        return;
    }
    if (!controller_transport_start()) {
        ESP_LOGE(TAG, "Failed to start controller transport");
        return;
    }

    controller_gateway_run_loop();
}
