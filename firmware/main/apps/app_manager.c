/* Owns the screen: status bar on top, a content area below that shows either
 * the launcher or one app. Translates touch and the BOOT button into
 * ui_input_t events and delivers server messages to the open app. Everything
 * here runs on the LVGL thread. */
#include <stdlib.h>
#include <string.h>
#include "apps/app_manager.h"
#include "ui/mono.h"
#include "ui/mono_tiles.h"
#include "ui/mono_status.h"
#include "board/board.h"
#include "link/link.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static const char *TAG = "apps";

#define SWIPE_PX        40
#define TICK_MS         20

typedef struct {
    enum { POST_MESSAGE, POST_LINK } kind;
    char *json;
    bool connected;
} post_t;

static QueueHandle_t s_queue;
static lv_obj_t *s_screen, *s_content, *s_app_root;
static mono_tiles_t *s_launcher;
static app_t *s_current;
static lv_point_t s_press;
static bool s_long_sent;
static bool s_btn_was_down;
static uint32_t s_btn_down_ms;
static uint32_t s_ms;

/* ------------------------------------------------------------- posting */

void app_manager_post_message(char *json)
{
    post_t p = { .kind = POST_MESSAGE, .json = json };
    if (!s_queue || xQueueSend(s_queue, &p, 0) != pdTRUE) {
        free(json);
    }
}

void app_manager_post_link_state(bool connected)
{
    post_t p = { .kind = POST_LINK, .connected = connected };
    if (s_queue) {
        xQueueSend(s_queue, &p, 0);
    }
}

/* -------------------------------------------------------------- routing */

static void deliver_input(const ui_input_t *ev)
{
    if (s_current) {
        if (s_current->on_input && s_current->on_input(s_current, ev)) {
            return;
        }
        if (ev->type == UI_INPUT_BACK || ev->type == UI_INPUT_SWIPE_RIGHT) {
            app_manager_back();
        }
        return;
    }
    mono_tiles_handle_input(s_launcher, ev);
}

static void handle_sys(const char *type, const cJSON *msg)
{
    if (strcmp(type, "sys.hello_ack") == 0) {
        const cJSON *t = cJSON_GetObjectItem(msg, "time");
        const cJSON *tz = cJSON_GetObjectItem(msg, "tz_offset_min");
        if (cJSON_IsNumber(t)) {
            link_set_time((int64_t)t->valuedouble, cJSON_IsNumber(tz) ? tz->valueint : 0);
        }
        mono_status_update();
        mono_toast("Connected");
    } else if (strcmp(type, "sys.error") == 0) {
        const cJSON *m = cJSON_GetObjectItem(msg, "message");
        mono_toast(cJSON_IsString(m) ? m->valuestring : "Server error");
    }
    /* sys.audio_* are handled by the audio module and polled by apps. */
}

static void dispatch_message(char *json)
{
    cJSON *msg = cJSON_Parse(json);
    if (!msg) {
        ESP_LOGW(TAG, "bad JSON: %.60s", json);
        free(json);
        return;
    }
    const cJSON *type = cJSON_GetObjectItem(msg, "type");
    if (cJSON_IsString(type)) {
        const char *t = type->valuestring;
        if (strncmp(t, "sys.", 4) == 0) {
            handle_sys(t, msg);
        } else if (strncmp(t, "app.", 4) == 0) {
            /* opened/closed acknowledgements: nothing to do */
        } else if (s_current) {
            size_t n = strlen(s_current->id);
            if (strncmp(t, s_current->id, n) == 0 && t[n] == '.' && s_current->on_message) {
                s_current->on_message(s_current, t + n + 1, msg);
            }
        }
    }
    cJSON_Delete(msg);
    free(json);
}

static void drain_queue(void)
{
    post_t p;
    for (int i = 0; i < 8 && s_queue && xQueueReceive(s_queue, &p, 0) == pdTRUE; i++) {
        if (p.kind == POST_MESSAGE) {
            dispatch_message(p.json);
        } else {
            mono_status_update();
            if (s_current && s_current->on_link_state) {
                s_current->on_link_state(s_current, p.connected);
            }
            if (!p.connected) {
                mono_toast("Server offline");
            }
        }
    }
}

/* ---------------------------------------------------------------- input */

static void screen_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    lv_point_t p = { 0, 0 };
    if (indev) {
        lv_indev_get_point(indev, &p);
    }
    ui_input_t ev = { .x = (int16_t)p.x, .y = (int16_t)p.y };

    switch (code) {
    case LV_EVENT_PRESSED:
        s_press = p;
        s_long_sent = false;
        break;
    case LV_EVENT_LONG_PRESSED:
        s_long_sent = true;
        ev.type = UI_INPUT_LONG_PRESS;
        ev.x = (int16_t)s_press.x;
        ev.y = (int16_t)s_press.y;
        deliver_input(&ev);
        break;
    case LV_EVENT_RELEASED: {
        if (s_long_sent) {
            break;
        }
        int dx = p.x - s_press.x;
        int dy = p.y - s_press.y;
        int adx = dx < 0 ? -dx : dx;
        int ady = dy < 0 ? -dy : dy;
        if (adx >= SWIPE_PX && adx >= ady) {
            ev.type = dx > 0 ? UI_INPUT_SWIPE_RIGHT : UI_INPUT_SWIPE_LEFT;
        } else if (ady >= SWIPE_PX) {
            ev.type = dy > 0 ? UI_INPUT_SWIPE_DOWN : UI_INPUT_SWIPE_UP;
        } else {
            ev.type = UI_INPUT_TAP;
            ev.x = (int16_t)s_press.x;
            ev.y = (int16_t)s_press.y;
        }
        deliver_input(&ev);
        break;
    }
    default:
        break;
    }
}

static void poll_button(void)
{
    bool down = board_button_pressed();
    if (down && !s_btn_was_down) {
        s_btn_down_ms = s_ms;
    } else if (!down && s_btn_was_down && s_ms - s_btn_down_ms >= 30) {
        ui_input_t ev = { .type = UI_INPUT_BACK };
        deliver_input(&ev);
    }
    s_btn_was_down = down;
}

/* ---------------------------------------------------------------- tick */

static void tick_cb(lv_timer_t *t)
{
    s_ms += TICK_MS;
    drain_queue();
    poll_button();
    if (s_ms % 100 == 0 && s_current && s_current->on_tick) {
        s_current->on_tick(s_current);
    }
    if (s_ms % 1000 == 0) {
        mono_status_update();
    }
}

/* ------------------------------------------------------------ lifecycle */

static void on_tile_open(int index, void *ctx)
{
    app_manager_open(app_registry[index]);
}

void app_manager_init(void)
{
    s_queue = xQueueCreate(64, sizeof(post_t));
    s_screen = lv_screen_active();
    mono_theme_apply(s_screen);
    mono_status_create(s_screen);
    s_content = mono_container(s_screen, 0, MONO_STATUS_H, MONO_SCREEN_W, MONO_CONTENT_H);
    s_launcher = mono_tiles_create(s_content, app_registry, app_registry_count, on_tile_open, NULL);
    mono_tiles_show(s_launcher);

    lv_obj_set_clickable(s_screen, true);
    lv_obj_add_event_cb(s_screen, screen_event_cb, LV_EVENT_ALL, NULL);
    lv_timer_create(tick_cb, TICK_MS, NULL);
    ESP_LOGI(TAG, "%d apps on the launcher", app_registry_count);
}

void app_manager_open(app_t *app)
{
    if (s_current) {
        app_manager_back();
    }
    s_current = app;
    lv_obj_set_hidden(s_launcher->root, true);
    s_app_root = mono_container(s_content, 0, 0, MONO_SCREEN_W, MONO_CONTENT_H);
    link_send_type_str("app.open", "app", app->id);
    if (app->on_enter) {
        app->on_enter(app, s_app_root);
    }
    ESP_LOGI(TAG, "opened %s", app->id);
}

void app_manager_back(void)
{
    if (!s_current) {
        return;
    }
    app_t *app = s_current;
    s_current = NULL;
    if (app->on_exit) {
        app->on_exit(app);
    }
    lv_obj_delete(s_app_root);
    s_app_root = NULL;
    link_send_type("app.close");
    mono_tiles_show(s_launcher);
    ESP_LOGI(TAG, "closed %s", app->id);
}

app_t *app_manager_current(void)
{
    return s_current;
}
