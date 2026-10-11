/* Settings: a plain MonoUI list. Tapping a row cycles its value. Also the
 * smallest possible example of an app. */
#include <stdio.h>
#include <string.h>
#include "apps/app.h"
#include "ui/mono.h"
#include "ui/mono_list.h"
#include "board/board.h"
#include "audio/audio.h"
#include "link/link.h"
#include "assets/icons.h"

#define ROWS 5

typedef struct {
    mono_list_t *list;
    char rows[ROWS][40];
    const char *items[ROWS];
} settings_t;

static settings_t s;

static void rebuild(void)
{
    snprintf(s.rows[0], sizeof(s.rows[0]), "Bright %d%%", board_get_brightness());
    snprintf(s.rows[1], sizeof(s.rows[1]), "Volume %d%%", audio_get_volume());
    snprintf(s.rows[2], sizeof(s.rows[2]), "Server %s", link_connected() ? "online" : "offline");
    snprintf(s.rows[3], sizeof(s.rows[3]), "IP %s", link_wifi_connected() ? link_ip() : "none");
    snprintf(s.rows[4], sizeof(s.rows[4]), "FW v%s", FW_VERSION);
    for (int i = 0; i < ROWS; i++) {
        s.items[i] = s.rows[i];
    }
    mono_list_set_items(s.list, s.items, ROWS);
}

static int cycle(int value, const int *steps, int n)
{
    for (int i = 0; i < n; i++) {
        if (steps[i] > value) {
            return steps[i];
        }
    }
    return steps[0];
}

static void on_select(mono_list_t *l, int index, void *ctx)
{
    static const int bright[] = { 30, 60, 80, 100 };
    static const int volume[] = { 40, 55, 70, 85, 100 };
    if (index == 0) {
        board_set_brightness(cycle(board_get_brightness(), bright, 4));
    } else if (index == 1) {
        audio_set_volume(cycle(audio_get_volume(), volume, 5));
    } else {
        return;
    }
    rebuild();
}

static void on_enter(app_t *self, lv_obj_t *root)
{
    memset(&s, 0, sizeof(s));
    lv_obj_t *title = mono_label(root, "SETTINGS", MONO_FONT_SMALL);
    lv_obj_set_pos(title, MONO_PAD, 8);
    s.list = mono_list_create(root, 0, 36, MONO_SCREEN_W, MONO_CONTENT_H - 36);
    mono_list_set_on_select(s.list, on_select, NULL);
    rebuild();
}

static void app_exit(app_t *self)
{
    mono_list_delete(s.list);
    s.list = NULL;
}

static bool on_input(app_t *self, const ui_input_t *ev)
{
    return mono_list_handle_input(s.list, ev);
}

static void on_link_state(app_t *self, bool connected)
{
    rebuild();
}

app_t app_settings = {
    .id = "settings",
    .title = "Settings",
    .icon = &icon_settings,
    .on_enter = on_enter,
    .on_exit = app_exit,
    .on_input = on_input,
    .on_link_state = on_link_state,
};
