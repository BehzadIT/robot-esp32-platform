#include "controller_transport.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "controller_gateway.h"
#include "controller_snapshot.h"

static const char* TAG = "CTRL_TX";

enum {
    CONTROLLER_UART_PORT = UART_NUM_1,
    CONTROLLER_UART_BAUD = 115200,
    CONTROLLER_UART_TX_PIN = GPIO_NUM_25,
    CONTROLLER_UART_RX_PIN = GPIO_NUM_26,
    CONTROLLER_UART_BUF_SIZE = 256,
    FRAME_MAGIC_0 = 0xA5,
    FRAME_MAGIC_1 = 0x5A,
    FRAME_VERSION = 0x01,
    FRAME_TYPE_SNAPSHOT = 0x01,
    SNAPSHOT_FLAG_CONNECTED = 0x01,
    SNAPSHOT_PAYLOAD_LEN = 26,
    FRAME_HEADER_LEN = 6,
    FRAME_CRC_LEN = 2,
    FRAME_TOTAL_LEN = FRAME_HEADER_LEN + SNAPSHOT_PAYLOAD_LEN + FRAME_CRC_LEN,
    SEND_PERIOD_MS = 50,
    HEARTBEAT_PERIOD_MS = 100,
};

static bool s_transport_started;

static uint32_t now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static void put_u16_le(uint8_t* out, uint16_t value) {
    out[0] = (uint8_t)(value & 0xff);
    out[1] = (uint8_t)((value >> 8) & 0xff);
}

static void put_u32_le(uint8_t* out, uint32_t value) {
    out[0] = (uint8_t)(value & 0xff);
    out[1] = (uint8_t)((value >> 8) & 0xff);
    out[2] = (uint8_t)((value >> 16) & 0xff);
    out[3] = (uint8_t)((value >> 24) & 0xff);
}

static uint16_t crc16_ccitt(const uint8_t* data, size_t len) {
    uint16_t crc = 0xffff;

    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x8000) {
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            } else {
                crc <<= 1;
            }
        }
    }

    return crc;
}

static void encode_snapshot_payload(uint8_t* payload, const controller_snapshot_v1_t* snapshot, uint32_t source_uptime_ms) {
    uint8_t flags = 0;

    if (snapshot->connected) {
        flags |= SNAPSHOT_FLAG_CONNECTED;
    }

    put_u32_le(&payload[0], snapshot->seq);
    put_u32_le(&payload[4], source_uptime_ms);
    payload[8] = flags;
    payload[9] = snapshot->dpad;
    put_u16_le(&payload[10], snapshot->buttons);
    payload[12] = snapshot->misc_buttons;
    payload[13] = 0;
    put_u16_le(&payload[14], (uint16_t)snapshot->left_stick_x);
    put_u16_le(&payload[16], (uint16_t)snapshot->left_stick_y);
    put_u16_le(&payload[18], (uint16_t)snapshot->right_stick_x);
    put_u16_le(&payload[20], (uint16_t)snapshot->right_stick_y);
    put_u16_le(&payload[22], snapshot->left_trigger);
    put_u16_le(&payload[24], snapshot->right_trigger);
}

static size_t encode_frame(uint8_t* frame, const controller_snapshot_v1_t* snapshot) {
    const uint32_t source_uptime_ms = now_ms();

    frame[0] = FRAME_MAGIC_0;
    frame[1] = FRAME_MAGIC_1;
    frame[2] = FRAME_VERSION;
    frame[3] = FRAME_TYPE_SNAPSHOT;
    put_u16_le(&frame[4], SNAPSHOT_PAYLOAD_LEN);
    encode_snapshot_payload(&frame[6], snapshot, source_uptime_ms);

    const uint16_t crc = crc16_ccitt(&frame[2], 4 + SNAPSHOT_PAYLOAD_LEN);
    put_u16_le(&frame[6 + SNAPSHOT_PAYLOAD_LEN], crc);
    return FRAME_TOTAL_LEN;
}

static bool write_frame(const uint8_t* frame, size_t frame_len) {
    const int written = uart_write_bytes(CONTROLLER_UART_PORT, (const char*)frame, frame_len);
    if (written != (int)frame_len) {
        ESP_LOGW(TAG, "short UART write written=%d expected=%u", written, (unsigned)frame_len);
        return false;
    }
    return true;
}

static void controller_transport_task(void* arg) {
    controller_snapshot_v1_t snapshot = {0};
    uint8_t frame[FRAME_TOTAL_LEN];
    uint32_t last_sent_seq = 0;
    uint32_t last_sent_ms = 0;
    bool have_last_send = false;

    (void)arg;

    while (true) {
        if (!controller_gateway_copy_latest(&snapshot)) {
            vTaskDelay(pdMS_TO_TICKS(SEND_PERIOD_MS));
            continue;
        }

        const uint32_t now = now_ms();
        const bool seq_changed = !have_last_send || snapshot.seq != last_sent_seq;
        // Repeated seq values are expected while the controller state is idle.
        // The heartbeat keeps Pico-side freshness logic alive without forcing
        // fake state churn in the BLE/controller layer.
        const bool heartbeat_due = !have_last_send || (now - last_sent_ms) >= HEARTBEAT_PERIOD_MS;

        if (seq_changed || heartbeat_due) {
            const size_t frame_len = encode_frame(frame, &snapshot);
            if (write_frame(frame, frame_len)) {
                last_sent_seq = snapshot.seq;
                last_sent_ms = now;
                have_last_send = true;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(SEND_PERIOD_MS));
    }
}

bool controller_transport_start(void) {
    const uart_config_t uart_config = {
        .baud_rate = CONTROLLER_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    if (s_transport_started) {
        return true;
    }

    ESP_ERROR_CHECK(uart_driver_install(CONTROLLER_UART_PORT, CONTROLLER_UART_BUF_SIZE, CONTROLLER_UART_BUF_SIZE, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(CONTROLLER_UART_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(CONTROLLER_UART_PORT, CONTROLLER_UART_TX_PIN, CONTROLLER_UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    if (xTaskCreate(controller_transport_task, "controller_tx", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to create controller transport task");
        return false;
    }

    s_transport_started = true;
    ESP_LOGI(
        TAG,
        "controller UART started uart=%d tx=%d rx=%d baud=%d",
        CONTROLLER_UART_PORT,
        CONTROLLER_UART_TX_PIN,
        CONTROLLER_UART_RX_PIN,
        CONTROLLER_UART_BAUD
    );
    return true;
}
