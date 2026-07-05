#ifndef CONTROLLER_SNAPSHOT_H
#define CONTROLLER_SNAPSHOT_H

#include <stdbool.h>
#include <stdint.h>

#include <controller/uni_controller.h>
#include <controller/uni_controller_type.h>
#include <uni_hid_device.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CONTROLLER_SNAPSHOT_NAME_MAX 64

typedef struct controller_snapshot_v1_s {
    uint32_t seq;
    bool connected;
    uni_controller_type_t controller_type;
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t battery;
    char name[CONTROLLER_SNAPSHOT_NAME_MAX];
    int16_t left_stick_x;
    int16_t left_stick_y;
    int16_t right_stick_x;
    int16_t right_stick_y;
    uint16_t left_trigger;
    uint16_t right_trigger;
    uint16_t buttons;
    uint8_t misc_buttons;
    uint8_t dpad;
    uint32_t updated_at_ms;
} controller_snapshot_v1_t;

void controller_snapshot_clear(controller_snapshot_v1_t* snapshot);
// Normalized ranges are transport-oriented, not robot-oriented:
// - sticks:   -1000..1000
// - triggers:    0..1000
// This keeps ESP32 limited to controller/library normalization while Pico
// remains the owner of drive semantics and safety behavior.
void controller_snapshot_fill_from_gamepad(
    controller_snapshot_v1_t* snapshot,
    const uni_hid_device_t* device,
    const uni_gamepad_t* gamepad,
    uint32_t seq,
    uint32_t updated_at_ms
);
void controller_snapshot_fill_disconnected(
    controller_snapshot_v1_t* snapshot,
    const uni_hid_device_t* device,
    uint32_t seq,
    uint32_t updated_at_ms
);
const char* controller_snapshot_type_name(uni_controller_type_t type);

#ifdef __cplusplus
}
#endif

#endif
