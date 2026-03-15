#include "bridge_status.h"

#include <stdio.h>
#include <string.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct {
    bridge_wifi_state_t wifi_state;
    char hostname[BRIDGE_STATUS_HOSTNAME_MAX];
    char ip[BRIDGE_STATUS_IP_MAX];
    char ws_path[BRIDGE_STATUS_WS_PATH_MAX];
    uint32_t ws_clients;
    bool uart_seen_since_boot;
    bool last_uart_line_seen;
    uint64_t last_uart_line_ms;
    uint32_t dropped_lines;
    uint32_t truncated_lines;
    uint32_t uart_fifo_overflows;
    uint32_t uart_buffer_full_events;
    uint32_t uart_frame_errors;
    uint32_t uart_parity_errors;
    uint32_t backlog_overwrites;
    uint64_t last_warning_emit_ms[BRIDGE_WARNING_TYPE_COUNT];
    char backlog[BRIDGE_BACKLOG_LINES][BRIDGE_BACKLOG_LINE_MAX + 1];
    size_t backlog_head;
    size_t backlog_count;
} bridge_status_runtime_t;

static bridge_status_runtime_t s_status;
static SemaphoreHandle_t s_status_mtx;

const char *bridge_status_wifi_state_string(bridge_wifi_state_t state)
{
    switch (state) {
    case BRIDGE_WIFI_STATE_BOOTING:
        return "booting";
    case BRIDGE_WIFI_STATE_CONNECTING:
        return "connecting";
    case BRIDGE_WIFI_STATE_WAITING_IP:
        return "waiting_ip";
    case BRIDGE_WIFI_STATE_ONLINE:
        return "online";
    case BRIDGE_WIFI_STATE_RECOVERING:
        return "recovering";
    default:
        return "unknown";
    }
}

static void status_lock(void)
{
    xSemaphoreTake(s_status_mtx, portMAX_DELAY);
}

static void status_unlock(void)
{
    xSemaphoreGive(s_status_mtx);
}

static uint64_t uptime_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000ULL);
}

void bridge_status_init(const char *hostname, const char *ws_path)
{
    if (!s_status_mtx) {
        s_status_mtx = xSemaphoreCreateMutex();
    }

    status_lock();
    memset(&s_status, 0, sizeof(s_status));
    s_status.wifi_state = BRIDGE_WIFI_STATE_BOOTING;
    snprintf(s_status.hostname, sizeof(s_status.hostname), "%s", hostname ? hostname : "");
    snprintf(s_status.ws_path, sizeof(s_status.ws_path), "%s", ws_path ? ws_path : "");
    status_unlock();
}

void bridge_status_set_wifi_state(bridge_wifi_state_t state)
{
    status_lock();
    s_status.wifi_state = state;
    status_unlock();
}

void bridge_status_set_ip_string(const char *ip)
{
    status_lock();
    snprintf(s_status.ip, sizeof(s_status.ip), "%s", ip ? ip : "");
    status_unlock();
}

void bridge_status_clear_ip(void)
{
    bridge_status_set_ip_string("");
}

void bridge_status_set_ws_clients(uint32_t ws_clients)
{
    status_lock();
    s_status.ws_clients = ws_clients;
    status_unlock();
}

void bridge_status_note_uart_line(void)
{
    status_lock();
    s_status.uart_seen_since_boot = true;
    s_status.last_uart_line_seen = true;
    s_status.last_uart_line_ms = uptime_ms();
    status_unlock();
}

void bridge_status_note_backlog_overwrite(void)
{
    status_lock();
    ++s_status.backlog_overwrites;
    status_unlock();
}

void bridge_status_note_dropped_line(void)
{
    status_lock();
    ++s_status.dropped_lines;
    status_unlock();
}

void bridge_status_note_truncated_line(void)
{
    status_lock();
    ++s_status.truncated_lines;
    status_unlock();
}

void bridge_status_note_uart_fifo_overflow(void)
{
    status_lock();
    ++s_status.uart_fifo_overflows;
    status_unlock();
}

void bridge_status_note_uart_buffer_full(void)
{
    status_lock();
    ++s_status.uart_buffer_full_events;
    status_unlock();
}

void bridge_status_note_uart_frame_error(void)
{
    status_lock();
    ++s_status.uart_frame_errors;
    status_unlock();
}

void bridge_status_note_uart_parity_error(void)
{
    status_lock();
    ++s_status.uart_parity_errors;
    status_unlock();
}

bool bridge_status_warning_should_emit(bridge_warning_type_t type, uint64_t min_interval_ms)
{
    uint64_t now_ms = uptime_ms();
    bool should_emit = false;

    if (type >= BRIDGE_WARNING_TYPE_COUNT) {
        return false;
    }

    status_lock();
    if (s_status.last_warning_emit_ms[type] == 0 ||
        now_ms - s_status.last_warning_emit_ms[type] >= min_interval_ms) {
        s_status.last_warning_emit_ms[type] = now_ms;
        should_emit = true;
    }
    status_unlock();

    return should_emit;
}

void bridge_status_record_backlog_line(const char *text)
{
    size_t index;

    if (!text || !text[0]) {
        return;
    }

    status_lock();
    if (s_status.backlog_count == BRIDGE_BACKLOG_LINES) {
        index = s_status.backlog_head;
        s_status.backlog_head = (s_status.backlog_head + 1U) % BRIDGE_BACKLOG_LINES;
        ++s_status.backlog_overwrites;
    } else {
        index = (s_status.backlog_head + s_status.backlog_count) % BRIDGE_BACKLOG_LINES;
        ++s_status.backlog_count;
    }

    snprintf(s_status.backlog[index], sizeof(s_status.backlog[index]), "%s", text);
    status_unlock();
}

void bridge_status_snapshot(bridge_status_snapshot_t *snapshot)
{
    uint64_t now_ms;

    if (!snapshot) {
        return;
    }

    now_ms = uptime_ms();

    status_lock();
    snapshot->wifi_state = s_status.wifi_state;
    snprintf(snapshot->hostname, sizeof(snapshot->hostname), "%s", s_status.hostname);
    snprintf(snapshot->ip, sizeof(snapshot->ip), "%s", s_status.ip);
    snprintf(snapshot->ws_path, sizeof(snapshot->ws_path), "%s", s_status.ws_path);
    snapshot->ws_clients = s_status.ws_clients;
    snapshot->uart_seen_since_boot = s_status.uart_seen_since_boot;
    snapshot->last_uart_line_seen = s_status.last_uart_line_seen;
    snapshot->last_uart_line_age_ms =
        s_status.last_uart_line_seen ? (now_ms - s_status.last_uart_line_ms) : 0;
    snapshot->backlog_lines = (uint32_t)s_status.backlog_count;
    snapshot->dropped_lines = s_status.dropped_lines;
    snapshot->truncated_lines = s_status.truncated_lines;
    snapshot->uart_fifo_overflows = s_status.uart_fifo_overflows;
    snapshot->uart_buffer_full_events = s_status.uart_buffer_full_events;
    snapshot->uart_frame_errors = s_status.uart_frame_errors;
    snapshot->uart_parity_errors = s_status.uart_parity_errors;
    snapshot->backlog_overwrites = s_status.backlog_overwrites;
    status_unlock();

    snapshot->uptime_ms = now_ms;
}

void bridge_status_format_ready_text(char *buffer, size_t buffer_len)
{
    bridge_status_snapshot_t snapshot = {0};
    bridge_status_snapshot(&snapshot);

    snprintf(
        buffer,
        buffer_len,
        "[bridge] ready hostname=%s ip=%s ws=ws://%s%s uart_seen=%s uptime_ms=%llu",
        snapshot.hostname,
        snapshot.ip[0] ? snapshot.ip : "0.0.0.0",
        snapshot.hostname,
        snapshot.ws_path,
        snapshot.uart_seen_since_boot ? "yes" : "no",
        (unsigned long long)snapshot.uptime_ms
    );
}

void bridge_status_format_ready_json(char *buffer, size_t buffer_len)
{
    bridge_status_snapshot_t snapshot = {0};
    bridge_status_snapshot(&snapshot);

    snprintf(
        buffer,
        buffer_len,
        "{\"source\":\"esp32_platform\",\"type\":\"bridge_status\",\"state\":\"ready\","
        "\"hostname\":\"%s\",\"ip\":\"%s\",\"ws_path\":\"%s\",\"ws_clients\":%lu,"
        "\"uart_seen_since_boot\":%s,\"uptime_ms\":%llu}",
        snapshot.hostname,
        snapshot.ip[0] ? snapshot.ip : "0.0.0.0",
        snapshot.ws_path,
        (unsigned long)snapshot.ws_clients,
        snapshot.uart_seen_since_boot ? "true" : "false",
        (unsigned long long)snapshot.uptime_ms
    );
}

void bridge_status_format_health_json(char *buffer, size_t buffer_len)
{
    bridge_status_snapshot_t snapshot = {0};
    bridge_status_snapshot(&snapshot);

    if (!snapshot.last_uart_line_seen) {
        snprintf(
            buffer,
            buffer_len,
            "{\"status\":\"ok\",\"wifi_state\":\"%s\",\"hostname\":\"%s\",\"ip\":\"%s\","
            "\"ws_path\":\"%s\",\"ws_clients\":%lu,\"uart_seen_since_boot\":%s,"
            "\"uptime_ms\":%llu,\"last_uart_line_age_ms\":null,"
            "\"backlog_lines\":%lu,\"dropped_lines\":%lu,\"truncated_lines\":%lu,"
            "\"uart_fifo_overflows\":%lu,\"uart_buffer_full_events\":%lu,"
            "\"uart_frame_errors\":%lu,\"uart_parity_errors\":%lu,"
            "\"backlog_overwrites\":%lu}",
            bridge_status_wifi_state_string(snapshot.wifi_state),
            snapshot.hostname,
            snapshot.ip[0] ? snapshot.ip : "0.0.0.0",
            snapshot.ws_path,
            (unsigned long)snapshot.ws_clients,
            snapshot.uart_seen_since_boot ? "true" : "false",
            (unsigned long long)snapshot.uptime_ms,
            (unsigned long)snapshot.backlog_lines,
            (unsigned long)snapshot.dropped_lines,
            (unsigned long)snapshot.truncated_lines,
            (unsigned long)snapshot.uart_fifo_overflows,
            (unsigned long)snapshot.uart_buffer_full_events,
            (unsigned long)snapshot.uart_frame_errors,
            (unsigned long)snapshot.uart_parity_errors,
            (unsigned long)snapshot.backlog_overwrites
        );
        return;
    }

    snprintf(
        buffer,
        buffer_len,
        "{\"status\":\"ok\",\"wifi_state\":\"%s\",\"hostname\":\"%s\",\"ip\":\"%s\","
        "\"ws_path\":\"%s\",\"ws_clients\":%lu,\"uart_seen_since_boot\":%s,"
        "\"uptime_ms\":%llu,\"last_uart_line_age_ms\":%llu,"
        "\"backlog_lines\":%lu,\"dropped_lines\":%lu,\"truncated_lines\":%lu,"
        "\"uart_fifo_overflows\":%lu,\"uart_buffer_full_events\":%lu,"
        "\"uart_frame_errors\":%lu,\"uart_parity_errors\":%lu,"
        "\"backlog_overwrites\":%lu}",
        bridge_status_wifi_state_string(snapshot.wifi_state),
        snapshot.hostname,
        snapshot.ip[0] ? snapshot.ip : "0.0.0.0",
        snapshot.ws_path,
        (unsigned long)snapshot.ws_clients,
        snapshot.uart_seen_since_boot ? "true" : "false",
        (unsigned long long)snapshot.uptime_ms,
        (unsigned long long)snapshot.last_uart_line_age_ms,
        (unsigned long)snapshot.backlog_lines,
        (unsigned long)snapshot.dropped_lines,
        (unsigned long)snapshot.truncated_lines,
        (unsigned long)snapshot.uart_fifo_overflows,
        (unsigned long)snapshot.uart_buffer_full_events,
        (unsigned long)snapshot.uart_frame_errors,
        (unsigned long)snapshot.uart_parity_errors,
        (unsigned long)snapshot.backlog_overwrites
    );
}
