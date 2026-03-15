#ifndef BRIDGE_STATUS_H
#define BRIDGE_STATUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BRIDGE_BACKLOG_LINES 128
#define BRIDGE_BACKLOG_LINE_MAX 384
#define BRIDGE_STATUS_HOSTNAME_MAX 64
#define BRIDGE_STATUS_IP_MAX 16
#define BRIDGE_STATUS_WS_PATH_MAX 16

typedef enum {
    BRIDGE_WIFI_STATE_BOOTING = 0,
    BRIDGE_WIFI_STATE_CONNECTING,
    BRIDGE_WIFI_STATE_WAITING_IP,
    BRIDGE_WIFI_STATE_ONLINE,
    BRIDGE_WIFI_STATE_RECOVERING,
} bridge_wifi_state_t;

typedef enum {
    BRIDGE_WARNING_UART_FIFO_OVERFLOW = 0,
    BRIDGE_WARNING_UART_BUFFER_FULL,
    BRIDGE_WARNING_UART_FRAME_ERROR,
    BRIDGE_WARNING_UART_PARITY_ERROR,
    BRIDGE_WARNING_LINE_TRUNCATED,
    BRIDGE_WARNING_LINE_DROPPED,
    BRIDGE_WARNING_TYPE_COUNT,
} bridge_warning_type_t;

typedef struct {
    bridge_wifi_state_t wifi_state;
    char hostname[BRIDGE_STATUS_HOSTNAME_MAX];
    char ip[BRIDGE_STATUS_IP_MAX];
    char ws_path[BRIDGE_STATUS_WS_PATH_MAX];
    uint32_t ws_clients;
    bool uart_seen_since_boot;
    bool last_uart_line_seen;
    uint64_t uptime_ms;
    uint64_t last_uart_line_age_ms;
    uint32_t backlog_lines;
    uint32_t dropped_lines;
    uint32_t truncated_lines;
    uint32_t uart_fifo_overflows;
    uint32_t uart_buffer_full_events;
    uint32_t uart_frame_errors;
    uint32_t uart_parity_errors;
    uint32_t backlog_overwrites;
} bridge_status_snapshot_t;

void bridge_status_init(const char *hostname, const char *ws_path);
void bridge_status_set_wifi_state(bridge_wifi_state_t state);
void bridge_status_set_ip_string(const char *ip);
void bridge_status_clear_ip(void);
void bridge_status_set_ws_clients(uint32_t ws_clients);
void bridge_status_note_uart_line(void);
void bridge_status_note_backlog_overwrite(void);
void bridge_status_note_dropped_line(void);
void bridge_status_note_truncated_line(void);
void bridge_status_note_uart_fifo_overflow(void);
void bridge_status_note_uart_buffer_full(void);
void bridge_status_note_uart_frame_error(void);
void bridge_status_note_uart_parity_error(void);
bool bridge_status_warning_should_emit(bridge_warning_type_t type, uint64_t min_interval_ms);
void bridge_status_record_backlog_line(const char *text);
size_t bridge_status_copy_backlog(
    char lines[][BRIDGE_BACKLOG_LINE_MAX + 1],
    size_t max_lines
);
void bridge_status_snapshot(bridge_status_snapshot_t *snapshot);
void bridge_status_format_ready_text(char *buffer, size_t buffer_len);
void bridge_status_format_ready_json(char *buffer, size_t buffer_len);
void bridge_status_format_health_json(char *buffer, size_t buffer_len);
const char *bridge_status_wifi_state_string(bridge_wifi_state_t state);

#endif
