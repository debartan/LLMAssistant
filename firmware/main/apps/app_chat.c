/* Chat with Claude. Tap to start listening, tap again to send. The reply
 * streams in as text and is spoken through the speaker. Long press starts a
 * fresh conversation. */
#include <stdlib.h>
#include <string.h>
#include "apps/app.h"
#include "ui/mono.h"
#include "audio/audio.h"
#include "link/link.h"
#include "assets/icons.h"
#include "esp_heap_caps.h"
#include <assert.h>

typedef enum { CHAT_IDLE, CHAT_LISTENING, CHAT_THINKING, CHAT_SPEAKING } chat_state_t;

#define REPLY_MAX       4096
#define REPLY_AREA_Y    100
#define REPLY_AREA_H    256
#define TEXT_W          (MONO_SCREEN_W - 2 * MONO_PAD)

typedef struct {
    lv_obj_t *state_lbl, *transcript, *reply_area, *reply, *status, *meter;
    chat_state_t state;
    bool done;
    char reply_buf[REPLY_MAX];
    size_t reply_len;
    int reply_y;
    int blink;
} chat_t;

static chat_t *s;

static void set_state(chat_state_t st)
{
    static const char *names[] = { "", "LISTENING", "THINKING", "SPEAKING" };
    s->state = st;
    lv_label_set_text(s->state_lbl, names[st]);
    lv_obj_align(s->state_lbl, LV_ALIGN_TOP_RIGHT, -MONO_PAD, 8);
    if (st != CHAT_LISTENING) {
        lv_obj_set_width(s->meter, 0);
    }
}

static void set_status(const char *text)
{
    lv_label_set_text(s->status, text);
}

static void reply_scroll_to(int y, bool animate)
{
    lv_obj_update_layout(s->reply);
    int h = lv_obj_get_height(s->reply);
    int min_y = REPLY_AREA_H - h;
    if (min_y > 0) {
        min_y = 0;
    }
    if (y < min_y) {
        y = min_y;
    }
    if (y > 0) {
        y = 0;
    }
    s->reply_y = y;
    if (animate) {
        mono_anim_y(s->reply, y, MONO_ANIM_MS);
    } else {
        lv_obj_set_y(s->reply, y);
    }
}

static void reply_clear(void)
{
    s->reply_len = 0;
    s->reply_buf[0] = '\0';
    lv_label_set_text(s->reply, "");
    reply_scroll_to(0, false);
}

static void reply_append(const char *text)
{
    size_t n = strlen(text);
    if (s->reply_len + n >= REPLY_MAX) {
        n = REPLY_MAX - 1 - s->reply_len;
    }
    memcpy(s->reply_buf + s->reply_len, text, n);
    s->reply_len += n;
    s->reply_buf[s->reply_len] = '\0';
    lv_label_set_text(s->reply, s->reply_buf);
    /* Follow the newest text, like a terminal. */
    lv_obj_update_layout(s->reply);
    reply_scroll_to(REPLY_AREA_H - lv_obj_get_height(s->reply), true);
}

static void start_listening(void)
{
    if (!link_connected()) {
        mono_toast("Server offline");
        return;
    }
    lv_label_set_text(s->transcript, "");
    reply_clear();
    s->done = false;
    link_send_type("chat.ptt_start");
    audio_capture_start();
    set_state(CHAT_LISTENING);
    set_status("Listening... tap to send");
}

static void stop_listening(void)
{
    audio_capture_stop();
    link_send_type("chat.ptt_stop");
    set_state(CHAT_THINKING);
    set_status("Thinking");
}

static void cancel_turn(void)
{
    audio_capture_stop();
    audio_playback_flush();
    link_send_type("chat.cancel");
    set_state(CHAT_IDLE);
    set_status("Tap to talk");
}

/* ------------------------------------------------------------ callbacks */

static void on_enter(app_t *self, lv_obj_t *root)
{
    s = heap_caps_calloc(1, sizeof(*s), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(s);
    lv_obj_t *title = mono_label(root, "CHAT", MONO_FONT_SMALL);
    lv_obj_set_pos(title, MONO_PAD, 8);
    s->state_lbl = mono_label(root, "", MONO_FONT_SMALL);

    s->transcript = mono_paragraph(root, "", MONO_FONT_SMALL, TEXT_W);
    lv_obj_set_pos(s->transcript, MONO_PAD, 36);
    lv_obj_set_height(s->transcript, 56);
    lv_obj_set_style_opa(s->transcript, LV_OPA_60, 0);

    lv_obj_t *rule = mono_fill(root);
    lv_obj_set_size(rule, TEXT_W, 1);
    lv_obj_set_pos(rule, MONO_PAD, REPLY_AREA_Y - 6);
    lv_obj_set_style_opa(rule, LV_OPA_40, 0);

    s->reply_area = mono_container(root, MONO_PAD, REPLY_AREA_Y, TEXT_W, REPLY_AREA_H);
    s->reply = mono_paragraph(s->reply_area, "", MONO_FONT_SMALL, TEXT_W);

    s->meter = mono_fill(root);
    lv_obj_set_pos(s->meter, MONO_PAD, REPLY_AREA_Y + REPLY_AREA_H + 12);
    lv_obj_set_size(s->meter, 0, 6);

    s->status = mono_label(root, link_connected() ? "Tap to talk" : "Server offline", MONO_FONT_SMALL);
    lv_obj_set_pos(s->status, MONO_PAD, MONO_CONTENT_H - 32);

    set_state(CHAT_IDLE);
}

static void app_exit(app_t *self)
{
    audio_capture_stop();
    audio_playback_flush();
    free(s);
    s = NULL;
}

static bool on_input(app_t *self, const ui_input_t *ev)
{
    switch (ev->type) {
    case UI_INPUT_TAP:
        switch (s->state) {
        case CHAT_IDLE:      start_listening(); break;
        case CHAT_LISTENING: stop_listening();  break;
        default:             cancel_turn();     break;
        }
        return true;
    case UI_INPUT_LONG_PRESS:
        cancel_turn();
        link_send_type("chat.reset");
        lv_label_set_text(s->transcript, "");
        reply_clear();
        mono_toast("New conversation");
        return true;
    case UI_INPUT_SWIPE_UP:
        reply_scroll_to(s->reply_y - 120, true);
        return true;
    case UI_INPUT_SWIPE_DOWN:
        reply_scroll_to(s->reply_y + 120, true);
        return true;
    default:
        return false;
    }
}

static void on_message(app_t *self, const char *verb, const cJSON *msg)
{
    const cJSON *text = cJSON_GetObjectItem(msg, "text");
    const char *str = cJSON_IsString(text) ? text->valuestring : "";

    if (strcmp(verb, "status") == 0) {
        set_status(str);
    } else if (strcmp(verb, "transcript") == 0) {
        lv_label_set_text_fmt(s->transcript, "> %s", str);
    } else if (strcmp(verb, "delta") == 0) {
        if (s->state == CHAT_THINKING) {
            set_state(CHAT_SPEAKING);
        }
        reply_append(str);
    } else if (strcmp(verb, "done") == 0) {
        s->done = true;
        if (!audio_playback_active()) {
            set_state(CHAT_IDLE);
            set_status("Tap to talk");
        }
    } else if (strcmp(verb, "error") == 0) {
        const cJSON *m = cJSON_GetObjectItem(msg, "message");
        mono_toast(cJSON_IsString(m) ? m->valuestring : "Chat error");
        set_state(CHAT_IDLE);
    }
}

static void on_link_state(app_t *self, bool connected)
{
    if (!connected) {
        audio_capture_stop();
        set_state(CHAT_IDLE);
        set_status("Server offline");
    } else {
        set_status("Tap to talk");
    }
}

static void on_tick(app_t *self)
{
    if (s->state == CHAT_LISTENING) {
        int w = audio_capture_level() * TEXT_W / 100;
        lv_obj_set_width(s->meter, w);
    } else if (s->state == CHAT_SPEAKING && s->done && !audio_playback_active()) {
        set_state(CHAT_IDLE);
        set_status("Tap to talk");
    } else if (s->state == CHAT_THINKING) {
        s->blink = (s->blink + 1) % 10;
        lv_obj_set_style_opa(s->state_lbl, s->blink < 5 ? LV_OPA_COVER : LV_OPA_30, 0);
    }
    if (s->state != CHAT_THINKING) {
        lv_obj_set_style_opa(s->state_lbl, LV_OPA_COVER, 0);
    }
}

app_t app_chat = {
    .id = "chat",
    .title = "Chat",
    .icon = &icon_chat,
    .on_enter = on_enter,
    .on_exit = app_exit,
    .on_input = on_input,
    .on_message = on_message,
    .on_link_state = on_link_state,
    .on_tick = on_tick,
};
