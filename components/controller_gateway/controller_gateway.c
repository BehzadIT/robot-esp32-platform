#include "controller_gateway.h"

#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

#include <btstack_port_esp32.h>
#include <btstack_run_loop.h>
#include <btstack_stdio_esp32.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <freertos/task.h>
#include <uni.h>

static const char* TAG = "CTRL_GATEWAY";
static const char* DIAG_TAG = "CTRL_DIAG";
static const TickType_t SNAPSHOT_PERIOD_TICKS = pdMS_TO_TICKS(200);

static portMUX_TYPE s_snapshot_lock = portMUX_INITIALIZER_UNLOCKED;
static controller_snapshot_v1_t s_latest_snapshot;
static bool s_have_snapshot;
static uint32_t s_seq;
static uni_hid_device_t* s_active_device;

static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static void publish_snapshot_from_gamepad(uni_hid_device_t* device, const uni_gamepad_t* gamepad) {
    controller_snapshot_v1_t next = {0};
    const uint32_t seq = ++s_seq;
    controller_snapshot_fill_from_gamepad(&next, device, gamepad, seq, now_ms());

    taskENTER_CRITICAL(&s_snapshot_lock);
    s_latest_snapshot = next;
    s_have_snapshot = true;
    taskEXIT_CRITICAL(&s_snapshot_lock);
}

static void publish_disconnected_snapshot(uni_hid_device_t* device) {
    controller_snapshot_v1_t next = {0};
    const uint32_t seq = ++s_seq;
    controller_snapshot_fill_disconnected(&next, device, seq, now_ms());

    taskENTER_CRITICAL(&s_snapshot_lock);
    s_latest_snapshot = next;
    s_have_snapshot = true;
    taskEXIT_CRITICAL(&s_snapshot_lock);
}

static bool device_is_active(const uni_hid_device_t* device) {
    return device != NULL && device == s_active_device;
}

static bool is_keyboard_cod(uint32_t cod) {
    return ((cod & UNI_BT_COD_MINOR_MASK) & UNI_BT_COD_MINOR_KEYBOARD) == UNI_BT_COD_MINOR_KEYBOARD;
}

static void maybe_log_button_transition(
    const controller_snapshot_v1_t* previous,
    const controller_snapshot_v1_t* current
) {
    if (!previous->connected) {
        return;
    }
    if (previous->buttons == current->buttons &&
        previous->misc_buttons == current->misc_buttons &&
        previous->dpad == current->dpad) {
        return;
    }
    ESP_LOGI(
        TAG,
        "button transition seq=%" PRIu32 " buttons=0x%04x misc=0x%02x dpad=0x%02x",
        current->seq,
        current->buttons,
        current->misc_buttons,
        current->dpad
    );
}

static void sampled_snapshot_task(void* arg) {
    controller_snapshot_v1_t snapshot = {0};
    uint32_t last_seq = 0;

    (void)arg;

    while (true) {
        if (controller_gateway_copy_latest(&snapshot) && snapshot.seq != last_seq) {
            last_seq = snapshot.seq;
            ESP_LOGI(
                DIAG_TAG,
                "seq=%" PRIu32 " connected=%d type=%s name=%s batt=%u "
                "lx=%d ly=%d rx=%d ry=%d lt=%u rt=%u buttons=0x%04x misc=0x%02x dpad=0x%02x age_ms=%" PRIu32,
                snapshot.seq,
                snapshot.connected,
                controller_snapshot_type_name(snapshot.controller_type),
                snapshot.name[0] ? snapshot.name : "<unknown>",
                snapshot.battery,
                snapshot.left_stick_x,
                snapshot.left_stick_y,
                snapshot.right_stick_x,
                snapshot.right_stick_y,
                snapshot.left_trigger,
                snapshot.right_trigger,
                snapshot.buttons,
                snapshot.misc_buttons,
                snapshot.dpad,
                now_ms() - snapshot.updated_at_ms
            );
        }
        vTaskDelay(SNAPSHOT_PERIOD_TICKS);
    }
}

static void gateway_platform_init(int argc, const char** argv) {
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);
    ESP_LOGI(TAG, "custom platform init");
}

static void gateway_platform_on_init_complete(void) {
    ESP_LOGI(TAG, "bluetooth init complete; enabling discovery");

    // Phase 1B keeps open pairing so the real-app coexistence bench validation
    // follows the same connection path already proven in Phase 1A.
    // Clearing keys on boot is intentional for this phase: it makes each cold
    // start exercise the same discovery/pairing path instead of silently
    // relying on a previously stored bond.
    uni_bt_start_scanning_and_autoconnect_unsafe();
    uni_bt_allow_incoming_connections(true);
    uni_bt_del_keys_unsafe();
}

static uni_error_t gateway_platform_on_device_discovered(
    bd_addr_t addr,
    const char* name,
    uint16_t cod,
    uint8_t rssi
) {
    (void)addr;
    ARG_UNUSED(rssi);

    if (s_active_device != NULL) {
        ESP_LOGW(
            TAG,
            "ignoring discovered device while active controller is connected: name=%s cod=0x%04x",
            name ? name : "<unknown>",
            cod
        );
        return UNI_ERROR_IGNORE_DEVICE;
    }
    if (is_keyboard_cod(cod)) {
        ESP_LOGI(TAG, "ignoring keyboard-like device during controller gateway startup");
        return UNI_ERROR_IGNORE_DEVICE;
    }
    return UNI_ERROR_SUCCESS;
}

static void gateway_platform_on_device_connected(uni_hid_device_t* device) {
    ESP_LOGI(
        TAG,
        "device connected ptr=%p name=%s vid=0x%04x pid=0x%04x",
        device,
        device->name,
        device->vendor_id,
        device->product_id
    );
}

static void gateway_platform_on_device_disconnected(uni_hid_device_t* device) {
    const bool was_active = device_is_active(device);

    ESP_LOGI(TAG, "device disconnected ptr=%p active=%d name=%s", device, was_active, device->name);
    if (!was_active) {
        return;
    }

    s_active_device = NULL;
    publish_disconnected_snapshot(device);
}

static uni_error_t gateway_platform_on_device_ready(uni_hid_device_t* device) {
    if (s_active_device != NULL && s_active_device != device) {
        ESP_LOGW(TAG, "rejecting additional ready device ptr=%p name=%s", device, device->name);
        uni_hid_device_disconnect(device);
        return UNI_ERROR_IGNORE_DEVICE;
    }
    if (!uni_hid_device_is_gamepad(device)) {
        ESP_LOGW(
            TAG,
            "rejecting non-gamepad device ptr=%p name=%s type=%s",
            device,
            device->name,
            controller_snapshot_type_name(device->controller_type)
        );
        uni_hid_device_disconnect(device);
        return UNI_ERROR_IGNORE_DEVICE;
    }

    s_active_device = device;
    ESP_LOGI(
        TAG,
        "active controller ready ptr=%p name=%s type=%s vid=0x%04x pid=0x%04x",
        device,
        device->name,
        controller_snapshot_type_name(device->controller_type),
        device->vendor_id,
        device->product_id
    );
    return UNI_ERROR_SUCCESS;
}

static void gateway_platform_on_controller_data(uni_hid_device_t* device, uni_controller_t* controller) {
    controller_snapshot_v1_t previous = {0};
    bool had_previous = false;

    if (!device_is_active(device)) {
        return;
    }
    if (controller->klass != UNI_CONTROLLER_CLASS_GAMEPAD) {
        return;
    }

    taskENTER_CRITICAL(&s_snapshot_lock);
    if (s_have_snapshot) {
        previous = s_latest_snapshot;
        had_previous = true;
    }
    taskEXIT_CRITICAL(&s_snapshot_lock);

    publish_snapshot_from_gamepad(device, &controller->gamepad);

    if (had_previous) {
        controller_snapshot_v1_t current = {0};
        if (controller_gateway_copy_latest(&current)) {
            maybe_log_button_transition(&previous, &current);
        }
    }
}

static const uni_property_t* gateway_platform_get_property(uni_property_idx_t idx) {
    ARG_UNUSED(idx);
    return NULL;
}

static void gateway_platform_on_oob_event(uni_platform_oob_event_t event, void* data) {
    switch (event) {
        case UNI_PLATFORM_OOB_BLUETOOTH_ENABLED:
            ESP_LOGI(TAG, "bluetooth enabled=%d", data != NULL);
            break;
        default:
            ESP_LOGI(TAG, "oob event=0x%04x", event);
            break;
    }
}

static struct uni_platform* gateway_platform_get(void) {
    static struct uni_platform platform = {
        .name = "robot-esp32-platform",
        .init = gateway_platform_init,
        .on_init_complete = gateway_platform_on_init_complete,
        .on_device_discovered = gateway_platform_on_device_discovered,
        .on_device_connected = gateway_platform_on_device_connected,
        .on_device_disconnected = gateway_platform_on_device_disconnected,
        .on_device_ready = gateway_platform_on_device_ready,
        .on_oob_event = gateway_platform_on_oob_event,
        .on_controller_data = gateway_platform_on_controller_data,
        .get_property = gateway_platform_get_property,
    };
    return &platform;
}

bool controller_gateway_start(void) {
    taskENTER_CRITICAL(&s_snapshot_lock);
    controller_snapshot_clear(&s_latest_snapshot);
    s_have_snapshot = false;
    s_seq = 0;
    taskEXIT_CRITICAL(&s_snapshot_lock);
    s_active_device = NULL;

#ifdef CONFIG_ESP_CONSOLE_UART
#ifndef CONFIG_BLUEPAD32_USB_CONSOLE_ENABLE
    btstack_stdio_init();
#endif
#endif

    if (xTaskCreate(sampled_snapshot_task, "controller_diag", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to create controller diagnostics task");
        return false;
    }

    btstack_init();
    uni_platform_set_custom(gateway_platform_get());
    uni_init(0, NULL);
    return true;
}

void controller_gateway_run_loop(void) {
    btstack_run_loop_execute();
}

bool controller_gateway_copy_latest(controller_snapshot_v1_t* out_snapshot) {
    bool have_snapshot = false;

    if (out_snapshot == NULL) {
        return false;
    }

    taskENTER_CRITICAL(&s_snapshot_lock);
    if (s_have_snapshot) {
        *out_snapshot = s_latest_snapshot;
        have_snapshot = true;
    }
    taskEXIT_CRITICAL(&s_snapshot_lock);
    return have_snapshot;
}
