#include <stdlib.h>
#include <string.h>
#include "ui/mono.h"
#include "ui/mono_list.h"

#define ROW_H       44
#define TEXT_X      (MONO_PAD + 6)
#define FRAME_X     MONO_PAD
#define FRAME_PAD   8
#define TRACK_W     4

static int frame_width_for(mono_list_t *l, int index)
{
    if (index < 0 || index >= l->count) {
        return 40;
    }
    int w = mono_text_width(lv_label_get_text(l->rows[index]), MONO_FONT_LARGE) + 2 * FRAME_PAD + 4;
    int max = l->width - FRAME_X - TRACK_W - 2 * MONO_PAD;
    return w > max ? max : w;
}

static void layout_scrollbar(mono_list_t *l, bool animate)
{
    if (l->count <= l->visible_rows) {
        lv_obj_set_hidden(l->track, true);
        lv_obj_set_hidden(l->thumb, true);
        return;
    }
    lv_obj_set_hidden(l->track, false);
    lv_obj_set_hidden(l->thumb, false);
    int track_h = l->height;
    int thumb_h = track_h * l->visible_rows / l->count;
    if (thumb_h < 12) {
        thumb_h = 12;
    }
    int thumb_y = (track_h - thumb_h) * l->first_visible / (l->count - l->visible_rows);
    lv_obj_set_height(l->thumb, thumb_h);
    if (animate) {
        mono_anim_y(l->thumb, thumb_y, MONO_ANIM_MS);
    } else {
        lv_obj_set_y(l->thumb, thumb_y);
    }
}

static void apply_selection(mono_list_t *l, bool animate)
{
    if (l->count == 0) {
        lv_obj_set_hidden(l->frame, true);
        return;
    }
    lv_obj_set_hidden(l->frame, false);

    if (l->selected < l->first_visible) {
        l->first_visible = l->selected;
    } else if (l->selected >= l->first_visible + l->visible_rows) {
        l->first_visible = l->selected - l->visible_rows + 1;
    }
    int rows_y = -l->first_visible * l->row_h;
    int frame_y = (l->selected - l->first_visible) * l->row_h + 2;
    int frame_w = frame_width_for(l, l->selected);

    if (animate) {
        mono_anim_y(l->rows_cont, rows_y, MONO_ANIM_MS);
        mono_anim_y(l->frame, frame_y, MONO_ANIM_MS);
        mono_anim_w(l->frame, frame_w, MONO_ANIM_MS + 80);
    } else {
        lv_obj_set_y(l->rows_cont, rows_y);
        lv_obj_set_y(l->frame, frame_y);
        lv_obj_set_width(l->frame, frame_w);
    }
    layout_scrollbar(l, animate);
}

mono_list_t *mono_list_create(lv_obj_t *parent, int x, int y, int w, int h)
{
    mono_list_t *l = calloc(1, sizeof(*l));
    l->width = w;
    l->height = h;
    l->row_h = ROW_H;
    l->visible_rows = h / ROW_H;
    if (l->visible_rows < 1) {
        l->visible_rows = 1;
    }
    l->root = mono_container(parent, x, y, w, h);
    lv_obj_set_style_clip_corner(l->root, true, 0);
    l->rows_cont = mono_container(l->root, 0, 0, w, ROW_H);
    l->frame = mono_frame(l->root);
    lv_obj_set_pos(l->frame, FRAME_X, 2);
    lv_obj_set_size(l->frame, 40, ROW_H - 4);
    l->track = mono_fill(l->root);
    lv_obj_set_style_opa(l->track, LV_OPA_30, 0);
    lv_obj_set_pos(l->track, w - TRACK_W - 2, 0);
    lv_obj_set_size(l->track, TRACK_W, h);
    l->thumb = mono_fill(l->root);
    lv_obj_set_pos(l->thumb, w - TRACK_W - 2, 0);
    lv_obj_set_size(l->thumb, TRACK_W, 12);
    apply_selection(l, false);
    return l;
}

void mono_list_delete(mono_list_t *l)
{
    if (!l) {
        return;
    }
    lv_anim_delete(l->rows_cont, NULL);
    lv_anim_delete(l->frame, NULL);
    lv_anim_delete(l->thumb, NULL);
    lv_obj_delete(l->root);
    free(l);
}

void mono_list_set_items(mono_list_t *l, const char *const *items, int count)
{
    if (count > MONO_LIST_MAX_ITEMS) {
        count = MONO_LIST_MAX_ITEMS;
    }
    lv_obj_clean(l->rows_cont);
    lv_obj_set_height(l->rows_cont, (count ? count : 1) * l->row_h);
    for (int i = 0; i < count; i++) {
        lv_obj_t *row = mono_label(l->rows_cont, items[i], MONO_FONT_LARGE);
        lv_label_set_long_mode(row, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(row, l->width - TEXT_X - TRACK_W - 2 * MONO_PAD);
        lv_obj_set_pos(row, TEXT_X, i * l->row_h + (l->row_h - 32) / 2);
        l->rows[i] = row;
    }
    l->count = count;
    if (l->selected >= count) {
        l->selected = count ? count - 1 : 0;
    }
    if (l->first_visible > l->selected) {
        l->first_visible = l->selected;
    }
    apply_selection(l, false);
}

void mono_list_set_on_select(mono_list_t *l, mono_list_select_cb_t cb, void *ctx)
{
    l->on_select = cb;
    l->ctx = ctx;
}

void mono_list_select(mono_list_t *l, int index, bool animate)
{
    if (l->count == 0) {
        return;
    }
    if (index < 0) {
        index = l->wrap ? l->count - 1 : 0;
    }
    if (index >= l->count) {
        index = l->wrap ? 0 : l->count - 1;
    }
    l->selected = index;
    apply_selection(l, animate);
}

void mono_list_move(mono_list_t *l, int delta)
{
    mono_list_select(l, l->selected + delta, true);
}

int mono_list_selected(const mono_list_t *l)
{
    return l->selected;
}

bool mono_list_handle_input(mono_list_t *l, const ui_input_t *ev)
{
    switch (ev->type) {
    case UI_INPUT_SWIPE_UP:
        mono_list_move(l, +1);
        return true;
    case UI_INPUT_SWIPE_DOWN:
        mono_list_move(l, -1);
        return true;
    case UI_INPUT_TAP: {
        if (l->count == 0) {
            return false;
        }
        /* ev->y is a screen coordinate; convert to the list's own space. */
        lv_area_t area;
        lv_obj_get_coords(l->root, &area);
        int local_y = ev->y - area.y1;
        if (local_y < 0 || local_y >= l->height) {
            return false;
        }
        int row = l->first_visible + local_y / l->row_h;
        if (row < 0 || row >= l->count) {
            return false;
        }
        if (row != l->selected) {
            mono_list_select(l, row, true);
        }
        if (l->on_select) {
            l->on_select(l, l->selected, l->ctx);
        }
        return true;
    }
    default:
        return false;
    }
}
