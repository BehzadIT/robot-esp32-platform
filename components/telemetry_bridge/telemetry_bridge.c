// telemetry_bridge.c — ESP-IDF v5.x
// Current primary capability: UART2 -> WebSocket telemetry bridge

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bridge_network.h"
#include "bridge_status.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "telemetry_bridge.h"

#define UART_PORT   UART_NUM_2
#define UART_BAUD   115200
#define UART_RX_PIN 16
#define UART_TX_PIN 17

#define UART_RX_BUFFER_SIZE 4096
#define UART_EVENT_QUEUE_LEN 20
#define LINE_QUEUE_DEPTH 32
#define UART_READ_CHUNK 256
#define UART_IDLE_MS 5000
#define WARNING_RATE_LIMIT_MS 5000
#define TRUNCATION_SUFFIX " [truncated]"
#define TRUNCATION_PREFIX_MAX (BRIDGE_BACKLOG_LINE_MAX - (sizeof(TRUNCATION_SUFFIX) - 1))

static const char *TAG = "TELEMETRY";
static QueueHandle_t s_line_q;
static QueueHandle_t s_uart_event_q;
static bool s_uart_stream_active;
static uint64_t s_last_uart_line_ms;

static uint64_t uptime_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000ULL);
}

static uint32_t warning_count_for_type(bridge_warning_type_t type)
{
    bridge_status_snapshot_t snapshot = {0};
    bridge_status_snapshot(&snapshot);

    switch (type) {
    case BRIDGE_WARNING_UART_FIFO_OVERFLOW:
        return snapshot.uart_fifo_overflows;
    case BRIDGE_WARNING_UART_BUFFER_FULL:
        return snapshot.uart_buffer_full_events;
    case BRIDGE_WARNING_UART_FRAME_ERROR:
        return snapshot.uart_frame_errors;
    case BRIDGE_WARNING_UART_PARITY_ERROR:
        return snapshot.uart_parity_errors;
    case BRIDGE_WARNING_LINE_TRUNCATED:
        return snapshot.truncated_lines;
    case BRIDGE_WARNING_LINE_DROPPED:
        return snapshot.dropped_lines;
    default:
        return 0;
    }
}

static const char *warning_type_name(bridge_warning_type_t type)
{
    switch (type) {
    case BRIDGE_WARNING_UART_FIFO_OVERFLOW:
        return "uart_fifo_overflow";
    case BRIDGE_WARNING_UART_BUFFER_FULL:
        return "uart_buffer_full";
    case BRIDGE_WARNING_UART_FRAME_ERROR:
        return "uart_frame_error";
    case BRIDGE_WARNING_UART_PARITY_ERROR:
        return "uart_parity_error";
    case BRIDGE_WARNING_LINE_TRUNCATED:
        return "line_truncated";
    case BRIDGE_WARNING_LINE_DROPPED:
        return "line_dropped";
    default:
        return "unknown";
    }
}

static void publish_system_line(const char *line)
{
    if (!line || !line[0]) {
        return;
    }

    bridge_status_record_backlog_line(line);
    bridge_network_broadcast_text(line);
}

static void emit_warning_if_due(bridge_warning_type_t type)
{
    char line[BRIDGE_BACKLOG_LINE_MAX + 1];

    /*
     * Warnings remain visible to operators, but they are rate-limited so a
     * broken UART path does not flood websocat harder than the original fault.
     */
    if (!bridge_status_warning_should_emit(type, WARNING_RATE_LIMIT_MS)) {
        return;
    }

    snprintf(
        line,
        sizeof(line),
        "[bridge] warning type=%s count=%lu",
        warning_type_name(type),
        (unsigned long)warning_count_for_type(type)
    );
    publish_system_line(line);
}

static void publish_live_line(const char *line)
{
    if (!line || !line[0]) {
        return;
    }

    bridge_status_record_backlog_line(line);
    if (xQueueSend(s_line_q, line, 0) != pdTRUE) {
        bridge_status_note_dropped_line();
        emit_warning_if_due(BRIDGE_WARNING_LINE_DROPPED);
    }
}

static void mark_uart_stream_active(void)
{
    s_last_uart_line_ms = uptime_ms();
    if (!s_uart_stream_active) {
        s_uart_stream_active = true;
        publish_system_line("[bridge] uart stream detected");
    }
}

static void maybe_emit_uart_idle(void)
{
    uint64_t now_ms = uptime_ms();

    if (s_uart_stream_active && now_ms - s_last_uart_line_ms >= UART_IDLE_MS) {
        s_uart_stream_active = false;
        publish_system_line("[bridge] uart stream idle idle_ms=5000");
    }
}

static void finalize_line(char *line, size_t *line_len, bool *line_truncated)
{
    if (*line_len == 0) {
        *line_truncated = false;
        return;
    }

    line[*line_len] = '\0';
    bridge_status_note_uart_line();
    mark_uart_stream_active();
    publish_live_line(line);
    *line_len = 0;
    *line_truncated = false;
}

static void append_uart_bytes(
    const uint8_t *bytes,
    size_t byte_count,
    char *line,
    size_t *line_len,
    bool *line_truncated
)
{
    for (size_t i = 0; i < byte_count; ++i) {
        uint8_t ch = bytes[i];

        if (ch == '\r') {
            continue;
        }

        if (ch == '\n') {
            finalize_line(line, line_len, line_truncated);
            continue;
        }

        if (*line_truncated) {
            continue;
        }

        if (*line_len < TRUNCATION_PREFIX_MAX) {
            line[(*line_len)++] = (char)ch;
            continue;
        }

        memcpy(&line[*line_len], TRUNCATION_SUFFIX, sizeof(TRUNCATION_SUFFIX) - 1);
        *line_len += sizeof(TRUNCATION_SUFFIX) - 1;
        *line_truncated = true;
        bridge_status_note_truncated_line();
        emit_warning_if_due(BRIDGE_WARNING_LINE_TRUNCATED);
    }
}

static void reset_uart_line_state(char *line, size_t *line_len, bool *line_truncated)
{
    (void)line;
    *line_len = 0;
    *line_truncated = false;
}

static void ws_sender_task(void *arg)
{
    char line[BRIDGE_BACKLOG_LINE_MAX + 1];

    (void)arg;

    while (1) {
        if (xQueueReceive(s_line_q, line, portMAX_DELAY) == pdTRUE) {
            bridge_network_broadcast_text(line);
        }
    }
}

static void uart_event_task(void *arg)
{
    uart_event_t event;
    uint8_t chunk[UART_READ_CHUNK];
    char line[BRIDGE_BACKLOG_LINE_MAX + 1];
    size_t line_len = 0;
    bool line_truncated = false;

    (void)arg;
    memset(line, 0, sizeof(line));

    while (1) {
        if (xQueueReceive(s_uart_event_q, &event, pdMS_TO_TICKS(100)) != pdTRUE) {
            maybe_emit_uart_idle();
            continue;
        }

        switch (event.type) {
        case UART_DATA: {
            size_t remaining = event.size;

            while (remaining > 0) {
                size_t to_read = remaining > sizeof(chunk) ? sizeof(chunk) : remaining;
                int read_len = uart_read_bytes(UART_PORT, chunk, to_read, pdMS_TO_TICKS(20));
                if (read_len <= 0) {
                    break;
                }

                append_uart_bytes(chunk, (size_t)read_len, line, &line_len, &line_truncated);
                remaining -= (size_t)read_len;
            }
            break;
        }
        case UART_FIFO_OVF:
            /*
             * Espressif's UART event example flushes and resets after FIFO/ring
             * overflow. We follow the same recovery path because partial line
             * state is already invalid once the driver reports lost bytes.
             */
            bridge_status_note_uart_fifo_overflow();
            uart_flush_input(UART_PORT);
            xQueueReset(s_uart_event_q);
            reset_uart_line_state(line, &line_len, &line_truncated);
            emit_warning_if_due(BRIDGE_WARNING_UART_FIFO_OVERFLOW);
            break;
        case UART_BUFFER_FULL:
            bridge_status_note_uart_buffer_full();
            uart_flush_input(UART_PORT);
            xQueueReset(s_uart_event_q);
            reset_uart_line_state(line, &line_len, &line_truncated);
            emit_warning_if_due(BRIDGE_WARNING_UART_BUFFER_FULL);
            break;
        case UART_FRAME_ERR:
            bridge_status_note_uart_frame_error();
            emit_warning_if_due(BRIDGE_WARNING_UART_FRAME_ERROR);
            break;
        case UART_PARITY_ERR:
            bridge_status_note_uart_parity_error();
            emit_warning_if_due(BRIDGE_WARNING_UART_PARITY_ERROR);
            break;
        default:
            break;
        }

        maybe_emit_uart_idle();
    }
}

void telemetry_bridge_start(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    s_line_q = xQueueCreate(LINE_QUEUE_DEPTH, BRIDGE_BACKLOG_LINE_MAX + 1);
    bridge_network_start();

    uart_config_t uc = {
        .baud_rate = UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(
        uart_driver_install(
            UART_PORT,
            UART_RX_BUFFER_SIZE,
            0,
            UART_EVENT_QUEUE_LEN,
            &s_uart_event_q,
            0
        )
    );
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uc));
    ESP_ERROR_CHECK(
        uart_set_pin(
            UART_PORT,
            UART_TX_PIN,
            UART_RX_PIN,
            UART_PIN_NO_CHANGE,
            UART_PIN_NO_CHANGE
        )
    );

    xTaskCreate(
        uart_event_task,
        "uart_event_task",
        4096,
        NULL,
        10,
        NULL
    );

    xTaskCreate(
        ws_sender_task,
        "ws_sender",
        4096,
        NULL,
        10,
        NULL
    );

    ESP_LOGI(TAG, "Telemetry bridge started");
}
