#include "controller_snapshot.h"

#include <string.h>

// Bluepad32's virtual gamepad uses a different effective range for sticks and
// triggers. Keep the proof DTO normalized, but match the library's actual
// semantics so later UART work is built on realistic values.
enum {
    STICK_NEGATIVE_RANGE = 512,
    STICK_POSITIVE_RANGE = 511,
    TRIGGER_RANGE = 1023,
};

static int32_t clamp_i32(int32_t value, int32_t min, int32_t max) {
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static int16_t normalize_axis(int32_t value) {
    if (value < 0) {
        const int32_t clamped = clamp_i32(value, -STICK_NEGATIVE_RANGE, 0);
        return (int16_t)((clamped * 1000) / STICK_NEGATIVE_RANGE);
    }

    const int32_t clamped = clamp_i32(value, 0, STICK_POSITIVE_RANGE);
    return (int16_t)((clamped * 1000) / STICK_POSITIVE_RANGE);
}

static uint16_t normalize_trigger(int32_t value) {
    const int32_t clamped = clamp_i32(value, 0, TRIGGER_RANGE);
    return (uint16_t)((clamped * 1000) / TRIGGER_RANGE);
}

static void copy_device_identity(controller_snapshot_v1_t* snapshot, const uni_hid_device_t* device) {
    snapshot->controller_type = device->controller_type;
    snapshot->vendor_id = device->vendor_id;
    snapshot->product_id = device->product_id;
    if (device->name[0] == '\0') {
        snapshot->name[0] = '\0';
        return;
    }
    strncpy(snapshot->name, device->name, sizeof(snapshot->name) - 1);
    snapshot->name[sizeof(snapshot->name) - 1] = '\0';
}

void controller_snapshot_clear(controller_snapshot_v1_t* snapshot) {
    memset(snapshot, 0, sizeof(*snapshot));
}

void controller_snapshot_fill_from_gamepad(
    controller_snapshot_v1_t* snapshot,
    const uni_hid_device_t* device,
    const uni_gamepad_t* gamepad,
    uint32_t seq,
    uint32_t updated_at_ms
) {
    controller_snapshot_clear(snapshot);
    snapshot->seq = seq;
    snapshot->connected = true;
    snapshot->battery = device->controller.battery;
    snapshot->updated_at_ms = updated_at_ms;
    snapshot->left_stick_x = normalize_axis(gamepad->axis_x);
    snapshot->left_stick_y = normalize_axis(gamepad->axis_y);
    snapshot->right_stick_x = normalize_axis(gamepad->axis_rx);
    snapshot->right_stick_y = normalize_axis(gamepad->axis_ry);
    snapshot->left_trigger = normalize_trigger(gamepad->brake);
    snapshot->right_trigger = normalize_trigger(gamepad->throttle);
    snapshot->buttons = gamepad->buttons;
    snapshot->misc_buttons = gamepad->misc_buttons;
    snapshot->dpad = gamepad->dpad;
    copy_device_identity(snapshot, device);
}

void controller_snapshot_fill_disconnected(
    controller_snapshot_v1_t* snapshot,
    const uni_hid_device_t* device,
    uint32_t seq,
    uint32_t updated_at_ms
) {
    controller_snapshot_clear(snapshot);
    snapshot->seq = seq;
    snapshot->connected = false;
    snapshot->updated_at_ms = updated_at_ms;
    copy_device_identity(snapshot, device);
}

const char* controller_snapshot_type_name(uni_controller_type_t type) {
    switch (type) {
        case CONTROLLER_TYPE_XBox360Controller:
            return "xbox360";
        case CONTROLLER_TYPE_XBoxOneController:
            return "xbox_one";
        case CONTROLLER_TYPE_PS3Controller:
            return "ps3";
        case CONTROLLER_TYPE_PS4Controller:
            return "ps4";
        case CONTROLLER_TYPE_PS5Controller:
            return "ps5";
        case CONTROLLER_TYPE_SwitchProController:
            return "switch_pro";
        case CONTROLLER_TYPE_SwitchJoyConLeft:
            return "joycon_left";
        case CONTROLLER_TYPE_SwitchJoyConRight:
            return "joycon_right";
        case CONTROLLER_TYPE_SwitchJoyConPair:
            return "joycon_pair";
        case CONTROLLER_TYPE_8BitdoController:
            return "8bitdo";
        case CONTROLLER_TYPE_GenericController:
            return "generic";
        default:
            return "unknown";
    }
}
