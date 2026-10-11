#include <stdio.h>
#include "ui/mono.h"
#include "ui/mono_status.h"
#include "board/board.h"
#include "link/link.h"

static lv_obj_t *s_root, *s_link, *s_clock, *s_batt, *s_rule;

lv_obj_t *mono_status_create(lv_obj_t *parent)
{
    s_root = mono_container(parent, 0, 0, MONO_SCREEN_W, MONO_STATUS_H);
    s_link = mono_label(s_root, "----", MONO_FONT_SMALL);
    lv_obj_align(s_link, LV_ALIGN_LEFT_MID, MONO_PAD, 0);
    s_clock = mono_label(s_root, "--:--", MONO_FONT_SMALL);
    lv_obj_align(s_clock, LV_ALIGN_CENTER, 0, 0);
    s_batt = mono_label(s_root, "", MONO_FONT_SMALL);
    lv_obj_align(s_batt, LV_ALIGN_RIGHT_MID, -MONO_PAD, 0);
    s_rule = mono_fill(s_root);
    lv_obj_set_size(s_rule, MONO_SCREEN_W - 2 * MONO_PAD, 1);
    lv_obj_set_pos(s_rule, MONO_PAD, MONO_STATUS_H - 2);
    lv_obj_set_style_opa(s_rule, LV_OPA_40, 0);
    mono_status_update();
    return s_root;
}

void mono_status_update(void)
{
    const char *link = link_connected() ? "LINK" : (link_wifi_connected() ? "WIFI" : "----");
    lv_label_set_text(s_link, link);

    int h, m;
    if (link_local_time(&h, &m)) {
        lv_label_set_text_fmt(s_clock, "%02d:%02d", h, m);
    } else {
        lv_label_set_text(s_clock, "--:--");
    }
    lv_obj_align(s_clock, LV_ALIGN_CENTER, 0, 0);

    int pct = board_battery_percent();
    if (pct < 0) {
        lv_label_set_text(s_batt, board_usb_present() ? "USB" : "");
    } else if (board_battery_charging()) {
        lv_label_set_text_fmt(s_batt, "+%d%%", pct);
    } else {
        lv_label_set_text_fmt(s_batt, "%d%%", pct);
    }
    lv_obj_align(s_batt, LV_ALIGN_RIGHT_MID, -MONO_PAD, 0);
}
