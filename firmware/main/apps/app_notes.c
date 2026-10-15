/* Meeting notes. A list of saved notes with "New recording" on top; while
 * recording, a timer and the live transcript; afterwards the summary. */
#include <stdlib.h>
#include <string.h>
#include "apps/app.h"
#include "ui/mono.h"
#include "ui/mono_list.h"
#include "audio/audio.h"
#include "link/link.h"
#include "assets/icons.h"
#include "esp_heap_caps.h"
#include <assert.h>
#include "esp_timer.h"

#define MAX_NOTES       30
#define LINES_MAX       1200
#define SUMMARY_MAX     3000
#define TEXT_W          (MONO_SCREEN_W - 2 * MONO_PAD)
#define SUMMARY_AREA_Y  40
#define SUMMARY_AREA_H  (MONO_CONTENT_H - SUMMARY_AREA_Y)

typedef enum { NOTES_LIST, NOTES_REC, NOTES_SUMMARY } notes_mode_t;

typedef struct {
    lv_obj_t *root;
    notes_mode_t mode;

    /* list */
    lv_obj_t *list_root;
    mono_list_t *list;
    char ids[MAX_NOTES][48];
    char titles[MAX_NOTES][40];
    const char *items[MAX_NOTES + 1];
    int note_count;

    /* recording */
    lv_obj_t *rec_root, *timer_lbl, *meter, *lines_lbl, *lines_area, *rec_status, *dot;
    int64_t rec_started_us;
    char lines_buf[LINES_MAX];
    bool stopping;

    /* summary */
    lv_obj_t *sum_root, *sum_area, *sum_scroll;
    int sum_y;
    char sum_buf[SUMMARY_MAX];
} notes_t;

static notes_t *s;

/* ------------------------------------------------------------------ list */

static void rebuild_list(void)
{
    s->items[0] = "+ New recording";
    for (int i = 0; i < s->note_count; i++) {
        s->items[i + 1] = s->titles[i];
    }
    mono_list_set_items(s->list, s->items, s->note_count + 1);
}

static void show_mode(notes_mode_t mode)
{
    s->mode = mode;
    lv_obj_set_hidden(s->list_root, true);
    lv_obj_set_hidden(s->rec_root, true);
    lv_obj_set_hidden(s->sum_root, true);
    lv_obj_t *show = mode == NOTES_LIST ? s->list_root : mode == NOTES_REC ? s->rec_root : s->sum_root;
    lv_obj_set_hidden(show, false);
    /* Slide the new view in from the right, MonoUI page style. */
    lv_obj_set_x(show, 60);
    lv_obj_set_style_opa(show, LV_OPA_0, 0);
    mono_anim_x(show, 0, MONO_ANIM_MS);
    mono_anim_opa(show, LV_OPA_COVER, MONO_ANIM_MS);
}

static void start_recording(void)
{
    if (!link_connected()) {
        mono_toast("Server offline");
        return;
    }
    s->lines_buf[0] = '\0';
    lv_label_set_text(s->lines_lbl, "");
    lv_obj_set_y(s->lines_lbl, 0);
    s->stopping = false;
    s->rec_started_us = esp_timer_get_time();
    lv_label_set_text(s->rec_status, "Tap to stop");
    link_send_type("notes.start");
    audio_capture_start();
    show_mode(NOTES_REC);
}

static void stop_recording(void)
{
    if (s->stopping) {
        return;
    }
    s->stopping = true;
    audio_capture_stop();
    link_send_type("notes.stop");
    lv_label_set_text(s->rec_status, "Finishing...");
}

static void on_list_select(mono_list_t *l, int index, void *ctx)
{
    if (index == 0) {
        start_recording();
    } else if (index - 1 < s->note_count) {
        link_send_type_str("notes.get", "id", s->ids[index - 1]);
    }
}

/* --------------------------------------------------------------- summary */

static void sum_scroll_to(int y, bool animate)
{
    lv_obj_update_layout(s->sum_scroll);
    int h = lv_obj_get_height(s->sum_scroll);
    int min_y = SUMMARY_AREA_H - h;
    if (min_y > 0) {
        min_y = 0;
    }
    if (y < min_y) {
        y = min_y;
    }
    if (y > 0) {
        y = 0;
    }
    s->sum_y = y;
    if (animate) {
        mono_anim_y(s->sum_scroll, y, MONO_ANIM_MS);
    } else {
        lv_obj_set_y(s->sum_scroll, y);
    }
}

static void show_summary(const cJSON *msg)
{
    const cJSON *title = cJSON_GetObjectItem(msg, "title");
    const cJSON *summary = cJSON_GetObjectItem(msg, "summary");
    const cJSON *items = cJSON_GetObjectItem(msg, "action_items");
    const cJSON *date = cJSON_GetObjectItem(msg, "date");

    lv_obj_clean(s->sum_scroll);
    int y = 0;
    lv_obj_t *t = mono_paragraph(s->sum_scroll, cJSON_IsString(title) ? title->valuestring : "Untitled", MONO_FONT_LARGE, TEXT_W);
    lv_obj_set_pos(t, 0, y);
    lv_obj_update_layout(t);
    y += lv_obj_get_height(t) + 6;

    if (cJSON_IsString(date)) {
        lv_obj_t *d = mono_label(s->sum_scroll, date->valuestring, MONO_FONT_SMALL);
        lv_obj_set_style_opa(d, LV_OPA_60, 0);
        lv_obj_set_pos(d, 0, y);
        y += 28;
    }

    lv_obj_t *body = mono_paragraph(s->sum_scroll, cJSON_IsString(summary) ? summary->valuestring : "", MONO_FONT_SMALL, TEXT_W);
    lv_obj_set_pos(body, 0, y);
    lv_obj_update_layout(body);
    y += lv_obj_get_height(body) + 20;

    if (cJSON_IsArray(items) && cJSON_GetArraySize(items) > 0) {
        lv_obj_t *h = mono_label(s->sum_scroll, "ACTION ITEMS", MONO_FONT_SMALL);
        lv_obj_set_pos(h, 0, y);
        y += 26;
        size_t used = 0;
        s->sum_buf[0] = '\0';
        const cJSON *it;
        cJSON_ArrayForEach(it, items) {
            if (!cJSON_IsString(it)) {
                continue;
            }
            int n = snprintf(s->sum_buf + used, SUMMARY_MAX - used, "- %s\n", it->valuestring);
            if (n < 0 || used + n >= SUMMARY_MAX) {
                break;
            }
            used += n;
        }
        lv_obj_t *list = mono_paragraph(s->sum_scroll, s->sum_buf, MONO_FONT_SMALL, TEXT_W);
        lv_obj_set_pos(list, 0, y);
        lv_obj_update_layout(list);
        y += lv_obj_get_height(list);
    }
    lv_obj_set_height(s->sum_scroll, y + 20);
    sum_scroll_to(0, false);
    show_mode(NOTES_SUMMARY);
}

/* ------------------------------------------------------------ callbacks */

static void on_enter(app_t *self, lv_obj_t *root)
{
    s = heap_caps_calloc(1, sizeof(*s), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    assert(s);
    s->root = root;

    /* list view */
    s->list_root = mono_container(root, 0, 0, MONO_SCREEN_W, MONO_CONTENT_H);
    lv_obj_t *title = mono_label(s->list_root, "NOTES", MONO_FONT_SMALL);
    lv_obj_set_pos(title, MONO_PAD, 8);
    s->list = mono_list_create(s->list_root, 0, 36, MONO_SCREEN_W, MONO_CONTENT_H - 36);
    mono_list_set_on_select(s->list, on_list_select, NULL);
    rebuild_list();

    /* recording view */
    s->rec_root = mono_container(root, 0, 0, MONO_SCREEN_W, MONO_CONTENT_H);
    lv_obj_t *rt = mono_label(s->rec_root, "RECORDING", MONO_FONT_SMALL);
    lv_obj_set_pos(rt, MONO_PAD, 8);
    s->dot = mono_fill(s->rec_root);
    lv_obj_set_size(s->dot, 10, 10);
    lv_obj_set_style_radius(s->dot, 5, 0);
    lv_obj_set_pos(s->dot, MONO_SCREEN_W - MONO_PAD - 10, 11);
    s->timer_lbl = mono_label(s->rec_root, "00:00", MONO_FONT_LARGE);
    lv_obj_align(s->timer_lbl, LV_ALIGN_TOP_MID, 0, 48);
    s->meter = mono_fill(s->rec_root);
    lv_obj_set_pos(s->meter, MONO_PAD, 96);
    lv_obj_set_size(s->meter, 0, 6);
    s->lines_area = mono_container(s->rec_root, MONO_PAD, 116, TEXT_W, MONO_CONTENT_H - 116 - 44);
    s->lines_lbl = mono_paragraph(s->lines_area, "", MONO_FONT_SMALL, TEXT_W);
    lv_obj_set_style_opa(s->lines_lbl, LV_OPA_80, 0);
    s->rec_status = mono_label(s->rec_root, "Tap to stop", MONO_FONT_SMALL);
    lv_obj_set_pos(s->rec_status, MONO_PAD, MONO_CONTENT_H - 32);

    /* summary view */
    s->sum_root = mono_container(root, 0, 0, MONO_SCREEN_W, MONO_CONTENT_H);
    lv_obj_t *st = mono_label(s->sum_root, "NOTE", MONO_FONT_SMALL);
    lv_obj_set_pos(st, MONO_PAD, 8);
    s->sum_area = mono_container(s->sum_root, MONO_PAD, SUMMARY_AREA_Y, TEXT_W, SUMMARY_AREA_H);
    s->sum_scroll = mono_container(s->sum_area, 0, 0, TEXT_W, 10);

    show_mode(NOTES_LIST);
    link_send_type("notes.list");
}

static void app_exit(app_t *self)
{
    audio_capture_stop();
    mono_list_delete(s->list);
    free(s);
    s = NULL;
}

static bool on_input(app_t *self, const ui_input_t *ev)
{
    switch (s->mode) {
    case NOTES_LIST:
        return mono_list_handle_input(s->list, ev);
    case NOTES_REC:
        if (ev->type == UI_INPUT_TAP || ev->type == UI_INPUT_BACK) {
            stop_recording();
            return true;
        }
        return ev->type != UI_INPUT_SWIPE_RIGHT ? false : true;  /* no accidental exit */
    case NOTES_SUMMARY:
        if (ev->type == UI_INPUT_SWIPE_UP) {
            sum_scroll_to(s->sum_y - 160, true);
            return true;
        }
        if (ev->type == UI_INPUT_SWIPE_DOWN) {
            sum_scroll_to(s->sum_y + 160, true);
            return true;
        }
        if (ev->type == UI_INPUT_BACK || ev->type == UI_INPUT_TAP || ev->type == UI_INPUT_SWIPE_RIGHT) {
            show_mode(NOTES_LIST);
            link_send_type("notes.list");
            return true;
        }
        return false;
    }
    return false;
}

static void on_message(app_t *self, const char *verb, const cJSON *msg)
{
    if (strcmp(verb, "list") == 0) {
        const cJSON *notes = cJSON_GetObjectItem(msg, "notes");
        s->note_count = 0;
        const cJSON *n;
        cJSON_ArrayForEach(n, notes) {
            if (s->note_count >= MAX_NOTES) {
                break;
            }
            const cJSON *id = cJSON_GetObjectItem(n, "id");
            const cJSON *title = cJSON_GetObjectItem(n, "title");
            if (!cJSON_IsString(id) || !cJSON_IsString(title)) {
                continue;
            }
            strlcpy(s->ids[s->note_count], id->valuestring, sizeof(s->ids[0]));
            strlcpy(s->titles[s->note_count], title->valuestring, sizeof(s->titles[0]));
            s->note_count++;
        }
        if (s->list) {
            rebuild_list();
        }
    } else if (strcmp(verb, "line") == 0) {
        const cJSON *text = cJSON_GetObjectItem(msg, "text");
        if (cJSON_IsString(text)) {
            size_t len = strlen(s->lines_buf);
            size_t add = strlen(text->valuestring) + 1;
            if (len + add >= LINES_MAX) {
                /* keep the tail: drop the oldest half */
                memmove(s->lines_buf, s->lines_buf + LINES_MAX / 2, len - LINES_MAX / 2 + 1);
                len = strlen(s->lines_buf);
            }
            snprintf(s->lines_buf + len, LINES_MAX - len, "%s%s", len ? "\n" : "", text->valuestring);
            lv_label_set_text(s->lines_lbl, s->lines_buf);
            lv_obj_update_layout(s->lines_lbl);
            int area_h = lv_obj_get_height(s->lines_area);
            int h = lv_obj_get_height(s->lines_lbl);
            mono_anim_y(s->lines_lbl, h > area_h ? area_h - h : 0, MONO_ANIM_MS);
        }
    } else if (strcmp(verb, "summary") == 0 || strcmp(verb, "note") == 0) {
        show_summary(msg);
    } else if (strcmp(verb, "status") == 0) {
        const cJSON *text = cJSON_GetObjectItem(msg, "text");
        if (cJSON_IsString(text) && s->mode == NOTES_REC) {
            lv_label_set_text(s->rec_status, text->valuestring);
        }
    } else if (strcmp(verb, "error") == 0) {
        const cJSON *m = cJSON_GetObjectItem(msg, "message");
        mono_toast(cJSON_IsString(m) ? m->valuestring : "Notes error");
        if (s->mode == NOTES_REC && s->stopping) {
            show_mode(NOTES_LIST);
        }
    }
}

static void on_link_state(app_t *self, bool connected)
{
    if (!connected && s->mode == NOTES_REC) {
        audio_capture_stop();
        lv_label_set_text(s->rec_status, "Server offline");
    }
}

static void on_tick(app_t *self)
{
    if (s->mode != NOTES_REC) {
        return;
    }
    int secs = (int)((esp_timer_get_time() - s->rec_started_us) / 1000000);
    lv_label_set_text_fmt(s->timer_lbl, "%02d:%02d", secs / 60, secs % 60);
    lv_obj_align(s->timer_lbl, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_width(s->meter, s->stopping ? 0 : audio_capture_level() * TEXT_W / 100);
    lv_obj_set_style_opa(s->dot, (secs * 2 + ((esp_timer_get_time() / 500000) & 1)) & 1 ? LV_OPA_COVER : LV_OPA_20, 0);
}

app_t app_notes = {
    .id = "notes",
    .title = "Notes",
    .icon = &icon_notes,
    .on_enter = on_enter,
    .on_exit = app_exit,
    .on_input = on_input,
    .on_message = on_message,
    .on_link_state = on_link_state,
    .on_tick = on_tick,
};
