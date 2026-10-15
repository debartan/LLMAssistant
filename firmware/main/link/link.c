/* Wi-Fi station + WebSocket client. Everything the device says to the server
 * goes through here, and everything the server says comes back through the
 * app manager's queue (text) or straight into audio playback (binary). */
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include "link/link.h"
#include "audio/audio.h"
#include "apps/app_manager.h"
#include "board/board.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "link";

#define RX_MAX_BYTES    (64 * 1024)
#define SEND_TIMEOUT    pdMS_TO_TICKS(1000)

static esp_websocket_client_handle_t s_client;
static esp_timer_handle_t s_reconnect_timer;
static volatile bool s_wifi_connected;
static volatile bool s_ws_connected;
static bool s_ws_started;
static char s_ip[16];
static bool s_time_set;
static int s_tz_offset_min;

/* Reassembly of one WebSocket message delivered across several DATA events. */
static struct {
    uint8_t *buf;
    int cap;
    int len;
    uint8_t op;
} s_rx;

/* ------------------------------------------------------------- outbound */

static esp_err_t send_text(const char *text, int len)
{
    if (!s_client || !s_ws_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    int n = esp_websocket_client_send_text(s_client, text, len, SEND_TIMEOUT);
    return n == len ? ESP_OK : ESP_FAIL;
}

esp_err_t link_send_json(cJSON *obj)
{
    char *text = cJSON_PrintUnformatted(obj);
    if (!text) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = send_text(text, strlen(text));
    cJSON_free(text);
    return err;
}

esp_err_t link_send_type(const char *type)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", type);
    esp_err_t err = link_send_json(o);
    cJSON_Delete(o);
    return err;
}

esp_err_t link_send_type_str(const char *type, const char *key, const char *value)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", type);
    cJSON_AddStringToObject(o, key, value);
    esp_err_t err = link_send_json(o);
    cJSON_Delete(o);
    return err;
}

esp_err_t link_send_audio(const int16_t *pcm, size_t samples)
{
    if (!s_client || !s_ws_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t frame[1 + AUDIO_FRAME_SAMPLES * 2];
    if (samples > AUDIO_FRAME_SAMPLES) {
        samples = AUDIO_FRAME_SAMPLES;
    }
    frame[0] = LINK_AUDIO_PCM16_16K;
    memcpy(frame + 1, pcm, samples * 2);
    int len = 1 + samples * 2;
    int n = esp_websocket_client_send_bin(s_client, (const char *)frame, len, SEND_TIMEOUT);
    return n == len ? ESP_OK : ESP_FAIL;
}

static void send_hello(void)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "sys.hello");
    cJSON_AddStringToObject(o, "device", CONFIG_ASSISTANT_DEVICE_NAME);
    cJSON_AddStringToObject(o, "fw", FW_VERSION);
    link_send_json(o);
    cJSON_Delete(o);
}

/* -------------------------------------------------------------- inbound */

static void rx_dispatch(void)
{
    if (s_rx.op == 0x02) {                       /* binary: audio */
        if (s_rx.len > 1 && s_rx.buf[0] == LINK_AUDIO_PCM16_16K) {
            audio_playback_push(s_rx.buf + 1, s_rx.len - 1);
        }
        return;
    }
    if (s_rx.op != 0x01) {
        return;
    }
    s_rx.buf[s_rx.len] = '\0';
    /* Flush is latency-sensitive: act on it here as well as in the UI. */
    if (strstr((char *)s_rx.buf, "\"sys.audio_flush\"")) {
        audio_playback_flush();
    }
    char *copy = strdup((char *)s_rx.buf);
    if (copy) {
        app_manager_post_message(copy);
    }
}

static void on_ws_data(const esp_websocket_event_data_t *d)
{
    if (d->op_code == 0x08) {                    /* close frame */
        return;
    }
    if (d->op_code != 0x00 && d->op_code != 0x01 && d->op_code != 0x02) {
        return;                                  /* ping/pong etc. */
    }
    if (d->payload_offset == 0 && d->op_code != 0x00) {
        if (d->payload_len > RX_MAX_BYTES) {
            ESP_LOGW(TAG, "dropping %d byte message", d->payload_len);
            s_rx.len = -1;
            return;
        }
        if (s_rx.cap < d->payload_len + 1) {
            free(s_rx.buf);
            s_rx.cap = d->payload_len + 1;
            s_rx.buf = malloc(s_rx.cap);
        }
        s_rx.len = 0;
        s_rx.op = d->op_code;
    }
    if (s_rx.len < 0 || !s_rx.buf) {
        return;                                  /* inside a dropped message */
    }
    if (s_rx.len + d->data_len + 1 > s_rx.cap) {
        uint8_t *bigger = realloc(s_rx.buf, s_rx.len + d->data_len + 1);
        if (!bigger) {
            s_rx.len = -1;
            return;
        }
        s_rx.buf = bigger;
        s_rx.cap = s_rx.len + d->data_len + 1;
    }
    memcpy(s_rx.buf + s_rx.len, d->data_ptr, d->data_len);
    s_rx.len += d->data_len;
    bool complete = (d->payload_offset + d->data_len >= d->payload_len) && d->fin;
    if (complete) {
        rx_dispatch();
        s_rx.len = 0;
    }
}

static void ws_event_handler(void *arg, esp_event_base_t base, int32_t id, void *event_data)
{
    esp_websocket_event_data_t *d = event_data;
    switch (id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "server connected");
        s_ws_connected = true;
        send_hello();
        app_manager_post_link_state(true);
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
    case WEBSOCKET_EVENT_ERROR:
        if (s_ws_connected) {
            ESP_LOGW(TAG, "server disconnected");
            s_ws_connected = false;
            audio_playback_flush();
            app_manager_post_link_state(false);
        }
        break;
    case WEBSOCKET_EVENT_DATA:
        on_ws_data(d);
        break;
    default:
        break;
    }
}

static void ws_start(void)
{
    if (s_ws_started) {
        return;
    }
    esp_websocket_client_config_t cfg = {
        .uri = CONFIG_ASSISTANT_SERVER_URI,
        .reconnect_timeout_ms = 3000,
        .network_timeout_ms = 10000,
        .buffer_size = 4096,
        .task_stack = 8192,
        .ping_interval_sec = 15,
        .pingpong_timeout_sec = 30,
        .enable_close_reconnect = true,
    };
    s_client = esp_websocket_client_init(&cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "websocket init failed");
        return;
    }
    esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY, ws_event_handler, NULL);
    esp_websocket_client_start(s_client);
    s_ws_started = true;
    ESP_LOGI(TAG, "connecting to %s", CONFIG_ASSISTANT_SERVER_URI);
}

/* ----------------------------------------------------------------- wifi */

static void reconnect_cb(void *arg)
{
    esp_wifi_connect();
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_connected) {
            ESP_LOGW(TAG, "wifi lost");
        }
        s_wifi_connected = false;
        s_ip[0] = '\0';
        esp_timer_start_once(s_reconnect_timer, 3000 * 1000);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&ev->ip_info.ip));
        s_wifi_connected = true;
        ESP_LOGI(TAG, "wifi up, ip %s", s_ip);
        ws_start();
    }
}

esp_err_t link_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));

    const esp_timer_create_args_t targs = { .callback = reconnect_cb, .name = "wifi_rc" };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_reconnect_timer));

    wifi_config_t cfg = { 0 };
    strncpy((char *)cfg.sta.ssid, CONFIG_ASSISTANT_WIFI_SSID, sizeof(cfg.sta.ssid) - 1);
    strncpy((char *)cfg.sta.password, CONFIG_ASSISTANT_WIFI_PASSWORD, sizeof(cfg.sta.password) - 1);
    cfg.sta.threshold.authmode = strlen(CONFIG_ASSISTANT_WIFI_PASSWORD) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "joining %s", CONFIG_ASSISTANT_WIFI_SSID);
    return ESP_OK;
}

bool link_wifi_connected(void)
{
    return s_wifi_connected;
}

bool link_connected(void)
{
    return s_ws_connected;
}

const char *link_ip(void)
{
    return s_ip;
}

/* ----------------------------------------------------------------- time */

void link_set_time(int64_t epoch, int tz_offset_min)
{
    struct timeval tv = { .tv_sec = (time_t)epoch, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    s_tz_offset_min = tz_offset_min;
    s_time_set = true;
}

bool link_local_time(int *hour, int *minute)
{
    if (!s_time_set) {
        return false;
    }
    time_t now = time(NULL) + (time_t)s_tz_offset_min * 60;
    struct tm tm;
    gmtime_r(&now, &tm);
    *hour = tm.tm_hour;
    *minute = tm.tm_min;
    return true;
}
