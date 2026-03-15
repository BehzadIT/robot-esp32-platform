#include "bridge_network.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bridge_status.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mdns.h"

#define WIFI_SSID   "BIZI-HOME-24"
#define WIFI_PASS   "AUxHHs7#2V3DZwuVwkK5"

#define BRIDGE_HOST_LABEL "robot-esp32"
#define BRIDGE_HOSTNAME_LOCAL "robot-esp32.local"
#define BRIDGE_INSTANCE_NAME "Robot ESP32 Platform"
#define BRIDGE_WS_PATH "/ws"
#define BRIDGE_HTTP_PORT 80
#define BRIDGE_MAX_CLIENTS 4
#define BRIDGE_HTTPD_SOCKET_BUDGET (BRIDGE_MAX_CLIENTS + 3)
#define BRIDGE_MESSAGE_MAX 512
#define BRIDGE_POST_HANDSHAKE_DELAY_MS 50

static const char *TAG = "BRIDGE_NET";

typedef struct {
} ws_broadcast_t;

typedef struct {
    httpd_handle_t server;
    int fd;
    esp_timer_handle_t timer;
} ws_connect_work_t;

static httpd_handle_t s_server;
static esp_netif_t *s_sta_netif;
static SemaphoreHandle_t s_clients_mtx;
static int s_client_fds[BRIDGE_MAX_CLIENTS];
static bool s_mdns_started;
static bool s_have_ip;
static esp_ip4_addr_t s_ip_addr;
static uint64_t s_last_got_ip_ms;
static uint64_t s_last_ws_connect_ms;

static uint64_t uptime_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000ULL);
}

static void utf8_sanitize(char *s)
{
    uint8_t *p = (uint8_t *)s;
    size_t out = 0;

    for (size_t i = 0; s[i]; ) {
        uint8_t c = p[i];
        if (c < 0x80) {
            p[out++] = c;
            i++;
        } else if ((c & 0xE0) == 0xC0 &&
                   (p[i + 1] & 0xC0) == 0x80) {
            p[out++] = p[i++];
            p[out++] = p[i++];
        } else if ((c & 0xF0) == 0xE0 &&
                   (p[i + 1] & 0xC0) == 0x80 &&
                   (p[i + 2] & 0xC0) == 0x80) {
            p[out++] = p[i++];
            p[out++] = p[i++];
            p[out++] = p[i++];
        } else {
            p[out++] = '?';
            i++;
        }
    }

    p[out] = '\0';
}

static void clients_init(void)
{
    for (int i = 0; i < BRIDGE_MAX_CLIENTS; ++i) {
        s_client_fds[i] = -1;
    }
    bridge_status_set_ws_clients(0);
}

static uint32_t clients_count_locked(void)
{
    uint32_t count = 0;

    for (int i = 0; i < BRIDGE_MAX_CLIENTS; ++i) {
        if (s_client_fds[i] >= 0) {
            ++count;
        }
    }

    return count;
}

static bool clients_has_capacity(void)
{
    bool has_capacity = false;

    xSemaphoreTake(s_clients_mtx, portMAX_DELAY);
    for (int i = 0; i < BRIDGE_MAX_CLIENTS; ++i) {
        if (s_client_fds[i] < 0) {
            has_capacity = true;
            break;
        }
    }
    xSemaphoreGive(s_clients_mtx);

    return has_capacity;
}

static bool clients_try_add(int fd)
{
    bool added = false;

    xSemaphoreTake(s_clients_mtx, portMAX_DELAY);
    for (int i = 0; i < BRIDGE_MAX_CLIENTS; ++i) {
        if (s_client_fds[i] < 0) {
            s_client_fds[i] = fd;
            added = true;
            break;
        }
    }
    bridge_status_set_ws_clients(clients_count_locked());
    xSemaphoreGive(s_clients_mtx);

    return added;
}

static void clients_remove(int fd)
{
    xSemaphoreTake(s_clients_mtx, portMAX_DELAY);
    for (int i = 0; i < BRIDGE_MAX_CLIENTS; ++i) {
        if (s_client_fds[i] == fd) {
            s_client_fds[i] = -1;
            break;
        }
    }
    bridge_status_set_ws_clients(clients_count_locked());
    xSemaphoreGive(s_clients_mtx);
}

static void stop_mdns(void)
{
    if (s_mdns_started) {
        mdns_free();
        s_mdns_started = false;
    }
}

static void start_mdns(void)
{
    mdns_txt_item_t service_txt[] = {
        {"board", "esp32"},
        {"path", BRIDGE_WS_PATH},
    };

    stop_mdns();
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(BRIDGE_HOST_LABEL));
    ESP_ERROR_CHECK(mdns_instance_name_set(BRIDGE_INSTANCE_NAME));
    ESP_ERROR_CHECK(mdns_service_add(
        BRIDGE_INSTANCE_NAME,
        "_http",
        "_tcp",
        BRIDGE_HTTP_PORT,
        service_txt,
        sizeof(service_txt) / sizeof(service_txt[0])
    ));
    s_mdns_started = true;
}

static const char *wifi_reason_string(uint8_t reason)
{
    switch ((wifi_err_reason_t)reason) {
    case WIFI_REASON_BEACON_TIMEOUT:
        return "beacon_timeout";
    case WIFI_REASON_ASSOC_COMEBACK_TIME_TOO_LONG:
        return "assoc_comeback_time_too_long";
    case WIFI_REASON_NO_AP_FOUND:
        return "no_ap_found";
    case WIFI_REASON_AUTH_FAIL:
        return "auth_fail";
    case WIFI_REASON_ASSOC_FAIL:
        return "assoc_fail";
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return "handshake_timeout";
    default:
        return "unknown";
    }
}

static void close_fn(void *ctx, int sockfd)
{
    clients_remove(sockfd);
    ESP_LOGI(TAG, "WS client disconnected fd=%d", sockfd);
}

static void ws_broadcast_work(void *arg)
{
    ws_broadcast_t *work = arg;
    char *text = (char *)(work + 1);

    utf8_sanitize(text);

    httpd_ws_frame_t frame = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)text,
        .len = strlen(text),
    };

    xSemaphoreTake(s_clients_mtx, portMAX_DELAY);
    for (int i = 0; i < BRIDGE_MAX_CLIENTS; ++i) {
        int fd = s_client_fds[i];
        if (fd < 0) {
            continue;
        }

        if (httpd_ws_get_fd_info(s_server, fd) != HTTPD_WS_CLIENT_WEBSOCKET ||
            httpd_ws_send_frame_async(s_server, fd, &frame) != ESP_OK) {
            s_client_fds[i] = -1;
        }
    }
    bridge_status_set_ws_clients(clients_count_locked());
    xSemaphoreGive(s_clients_mtx);

    free(work);
}

void bridge_network_broadcast_text(const char *text)
{
    size_t text_len;
    ws_broadcast_t *work;
    char *payload;

    if (!s_server || !text || !text[0]) {
        return;
    }

    text_len = strnlen(text, BRIDGE_MESSAGE_MAX - 1);
    work = calloc(1, sizeof(*work) + text_len + 1);
    if (!work) {
        return;
    }

    payload = (char *)(work + 1);
    memcpy(payload, text, text_len);
    payload[text_len] = '\0';

    if (httpd_queue_work(s_server, ws_broadcast_work, work) != ESP_OK) {
        free(work);
    }
}

static esp_err_t healthz_handler(httpd_req_t *req)
{
    char json[BRIDGE_MESSAGE_MAX];

    bridge_status_format_health_json(json, sizeof(json));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static void ws_connect_work_free(ws_connect_work_t *work)
{
    if (work->timer) {
        esp_timer_delete(work->timer);
        work->timer = NULL;
    }
    free(work);
}

static esp_err_t ws_send_connect_frame_async(httpd_handle_t server,
                                             int fd,
                                             const char *label,
                                             char *text,
                                             size_t buffer_len)
{
    httpd_ws_frame_t frame = {0};

    if (!text || !text[0] || buffer_len == 0) {
        return ESP_OK;
    }

    if (text[strlen(text) - 1] != '\n') {
        strlcat(text, "\n", buffer_len);
    }
    utf8_sanitize(text);

    frame.final = true;
    frame.fragmented = false;
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = (uint8_t *)text;
    frame.len = strlen(text);

    esp_err_t send_err = httpd_ws_send_frame_async(server, fd, &frame);
    ESP_LOGI(TAG, "WS connect send fd=%d label=%s err=%s", fd, label, esp_err_to_name(send_err));
    return send_err;
}

static void ws_connect_work(void *arg)
{
    ws_connect_work_t *work = arg;
    char ready_text[BRIDGE_MESSAGE_MAX];
    char ready_json[BRIDGE_MESSAGE_MAX];
    httpd_ws_client_info_t fd_info;
    esp_err_t send_err;

    ESP_LOGI(TAG, "WS connect work start fd=%d", work->fd);

    /*
     * The websocket URI handler runs in the handshake path. Queueing work back
     * onto the HTTP server task after a short defer keeps the first frames on
     * the documented async-send path instead of creating a separate task.
     */
    fd_info = work->server ? httpd_ws_get_fd_info(work->server, work->fd) : HTTPD_WS_CLIENT_INVALID;
    ESP_LOGI(TAG, "WS connect work fd=%d state=%d", work->fd, (int)fd_info);

    if (!s_server ||
        s_server != work->server ||
        fd_info != HTTPD_WS_CLIENT_WEBSOCKET) {
        ESP_LOGW(TAG, "WS connect work aborted fd=%d", work->fd);
        ws_connect_work_free(work);
        return;
    }

    bridge_status_format_ready_text(ready_text, sizeof(ready_text));
    send_err = ws_send_connect_frame_async(work->server, work->fd, "ready_text", ready_text, sizeof(ready_text));
    if (send_err != ESP_OK) {
        ws_connect_work_free(work);
        return;
    }

    bridge_status_format_ready_json(ready_json, sizeof(ready_json));
    send_err = ws_send_connect_frame_async(work->server, work->fd, "ready_json", ready_json, sizeof(ready_json));
    if (send_err != ESP_OK) {
        ws_connect_work_free(work);
        return;
    }

    if (!clients_try_add(work->fd)) {
        ESP_LOGW(TAG, "WS client race lost during connect fd=%d", work->fd);
    } else {
        s_last_ws_connect_ms = uptime_ms();
        ESP_LOGI(TAG, "WS client connected fd=%d", work->fd);
    }

    ws_connect_work_free(work);
}

static void ws_connect_timer_cb(void *arg)
{
    ws_connect_work_t *work = arg;
    esp_err_t err = httpd_queue_work(work->server, ws_connect_work, work);

    ESP_LOGI(TAG, "WS connect queue fd=%d err=%s", work->fd, esp_err_to_name(err));
    if (err != ESP_OK) {
        ws_connect_work_free(work);
    }
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ws_connect_work_t *work;
        int fd = httpd_req_to_sockfd(req);
        esp_timer_create_args_t timer_args = {0};
        esp_err_t err;

        if (!clients_has_capacity()) {
            httpd_resp_set_status(req, "503 Service Unavailable");
            return httpd_resp_sendstr(req, "bridge client limit reached");
        }

        work = calloc(1, sizeof(*work));
        if (!work) {
            httpd_resp_set_status(req, "500 Internal Server Error");
            return httpd_resp_sendstr(req, "bridge connect worker alloc failed");
        }
        work->server = req->handle;
        work->fd = fd;
        timer_args.callback = ws_connect_timer_cb;
        timer_args.arg = work;
        timer_args.name = "ws_connect";

        err = esp_timer_create(&timer_args, &work->timer);
        if (err != ESP_OK) {
            free(work);
            httpd_resp_set_status(req, "500 Internal Server Error");
            return httpd_resp_sendstr(req, "bridge connect timer create failed");
        }

        err = esp_timer_start_once(work->timer, BRIDGE_POST_HANDSHAKE_DELAY_MS * 1000ULL);
        if (err != ESP_OK) {
            ws_connect_work_free(work);
            httpd_resp_set_status(req, "500 Internal Server Error");
            return httpd_resp_sendstr(req, "bridge connect timer start failed");
        }
        ESP_LOGI(TAG, "WS connect timer armed fd=%d delay_ms=%d", fd, BRIDGE_POST_HANDSHAKE_DELAY_MS);

        return ESP_OK;
    }

    httpd_ws_frame_t frame = {0};
    frame.type = HTTPD_WS_TYPE_TEXT;
    if (httpd_ws_recv_frame(req, &frame, 0) != ESP_OK) {
        return ESP_FAIL;
    }

    if (frame.len) {
        frame.payload = malloc(frame.len);
        if (frame.payload) {
            httpd_ws_recv_frame(req, &frame, frame.len);
            free(frame.payload);
        }
    }

    return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t hd = NULL;

    cfg.max_open_sockets = BRIDGE_HTTPD_SOCKET_BUDGET;
    cfg.lru_purge_enable = true;
    cfg.keep_alive_enable = true;
    cfg.keep_alive_idle = 5;
    cfg.keep_alive_interval = 5;
    cfg.keep_alive_count = 3;
    cfg.close_fn = close_fn;

    if (httpd_start(&hd, &cfg) != ESP_OK) {
        return NULL;
    }

    static httpd_uri_t ws = {
        .uri = BRIDGE_WS_PATH,
        .method = HTTP_GET,
        .handler = ws_handler,
        .is_websocket = true,
    };
    static httpd_uri_t healthz = {
        .uri = "/healthz",
        .method = HTTP_GET,
        .handler = healthz_handler,
    };

    httpd_register_uri_handler(hd, &ws);
    httpd_register_uri_handler(hd, &healthz);
    return hd;
}

static void stop_webserver(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }

    clients_init();
}

static void set_status_ip(const esp_ip4_addr_t *ip)
{
    char ip_string[BRIDGE_STATUS_IP_MAX];

    snprintf(
        ip_string,
        sizeof(ip_string),
        IPSTR,
        IP2STR(ip)
    );
    bridge_status_set_ip_string(ip_string);
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;

    if (base != WIFI_EVENT) {
        return;
    }

    if (id == WIFI_EVENT_STA_START) {
        bridge_status_set_wifi_state(BRIDGE_WIFI_STATE_CONNECTING);
        ESP_LOGI(TAG, "Wi-Fi started, connecting...");
        esp_wifi_connect();
        return;
    }

    if (id == WIFI_EVENT_STA_CONNECTED) {
        bridge_status_set_wifi_state(BRIDGE_WIFI_STATE_WAITING_IP);
        ESP_LOGI(TAG, "Wi-Fi connected, waiting for IP...");
        return;
    }

    if (id == WIFI_EVENT_STA_BEACON_TIMEOUT) {
        bridge_status_snapshot_t snapshot = {0};
        bridge_status_snapshot(&snapshot);
        ESP_LOGW(
            TAG,
            "Wi-Fi beacon timeout ws_clients=%lu uptime_ms=%llu online_ms=%llu since_ws_ms=%llu",
            (unsigned long)snapshot.ws_clients,
            (unsigned long long)uptime_ms(),
            (unsigned long long)(s_last_got_ip_ms ? (uptime_ms() - s_last_got_ip_ms) : 0),
            (unsigned long long)(s_last_ws_connect_ms ? (uptime_ms() - s_last_ws_connect_ms) : 0)
        );
        return;
    }

    if (id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = data;
        bridge_status_snapshot_t snapshot = {0};
        bridge_status_snapshot(&snapshot);
        /*
         * ESP-IDF clears TCP state on station disconnect. Rebuilding the HTTP
         * server avoids keeping sockets around in a wrong state after Wi-Fi loss.
         */
        s_have_ip = false;
        memset(&s_ip_addr, 0, sizeof(s_ip_addr));
        bridge_status_set_wifi_state(BRIDGE_WIFI_STATE_RECOVERING);
        bridge_status_clear_ip();
        stop_webserver();
        stop_mdns();
        ESP_LOGW(
            TAG,
            "Wi-Fi disconnected reason=%u(%s) rssi=%d ws_clients=%lu uptime_ms=%llu online_ms=%llu since_ws_ms=%llu, retrying...",
            event ? event->reason : 0,
            event ? wifi_reason_string(event->reason) : "unknown",
            event ? event->rssi : 0,
            (unsigned long)snapshot.ws_clients,
            (unsigned long long)uptime_ms(),
            (unsigned long long)(s_last_got_ip_ms ? (uptime_ms() - s_last_got_ip_ms) : 0),
            (unsigned long long)(s_last_ws_connect_ms ? (uptime_ms() - s_last_ws_connect_ms) : 0)
        );
        esp_wifi_connect();
    }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;

    if (id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = data;
        bool ip_changed = !s_have_ip ||
                          event->ip_changed ||
                          s_ip_addr.addr != event->ip_info.ip.addr;

        s_have_ip = true;
        s_ip_addr = event->ip_info.ip;
        s_last_got_ip_ms = uptime_ms();
        bridge_status_set_wifi_state(BRIDGE_WIFI_STATE_ONLINE);
        set_status_ip(&event->ip_info.ip);

        /*
         * A changed IP invalidates socket identity for clients, so rebuild the
         * advertised services and HTTP server rather than trying to preserve them.
         */
        if (ip_changed) {
            stop_webserver();
            start_mdns();
        }

        if (!s_server) {
            s_server = start_webserver();
        }

        ESP_LOGI(
            TAG,
            "Bridge ready hostname=%s ip=" IPSTR " ws=ws://%s%s",
            BRIDGE_HOSTNAME_LOCAL,
            IP2STR(&event->ip_info.ip),
            BRIDGE_HOSTNAME_LOCAL,
            BRIDGE_WS_PATH
        );
    }
}

void bridge_network_start(void)
{
    bridge_status_init(BRIDGE_HOSTNAME_LOCAL, BRIDGE_WS_PATH);
    s_clients_mtx = xSemaphoreCreateMutex();
    clients_init();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta_netif = esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(esp_netif_set_hostname(s_sta_netif, BRIDGE_HOST_LABEL));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_ip_event, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi = {0};
    snprintf((char *)wifi.sta.ssid, sizeof(wifi.sta.ssid), "%s", WIFI_SSID);
    snprintf((char *)wifi.sta.password, sizeof(wifi.sta.password), "%s", WIFI_PASS);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi));
    ESP_ERROR_CHECK(esp_wifi_start());

    /*
     * This bridge is latency-sensitive and Linux terminal-driven, so Wi-Fi power
     * saving stays disabled to avoid extra reconnect/jitter surprises.
     */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
}
