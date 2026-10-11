#include <stdlib.h>
#include "ui/mono.h"
#include "ui/mono_tiles.h"
#include "assets/icons.h"

#define TILE_SPACING    150
#define ICON_Y          96
#define TITLE_Y         (ICON_Y + ICON_SIZE + 36)
#define DOTS_Y          (TITLE_Y + 56)
#define CENTER_X        (MONO_SCREEN_W / 2)

static int strip_x_for(int selected)
{
    return CENTER_X - ICON_SIZE / 2 - selected * TILE_SPACING;
}

static void apply(mono_tiles_t *t, bool animate)
{
    int x = strip_x_for(t->selected);
    if (animate) {
        mono_anim_x(t->strip, x, MONO_ANIM_MS + 60);
    } else {
        lv_obj_set_x(t->strip, x);
    }
    for (int i = 0; i < t->count; i++) {
        int opa = (i == t->selected) ? LV_OPA_COVER : LV_OPA_40;
        if (animate) {
            mono_anim_opa(t->icons[i], opa, MONO_ANIM_MS);
        } else {
            lv_obj_set_style_opa(t->icons[i], opa, 0);
        }
        lv_obj_set_style_opa(t->dots[i], (i == t->selected) ? LV_OPA_COVER : LV_OPA_30, 0);
    }
    lv_label_set_text(t->title, t->apps[t->selected]->title);
    lv_obj_align(t->title, LV_ALIGN_TOP_MID, 0, TITLE_Y);
}

mono_tiles_t *mono_tiles_create(lv_obj_t *parent, app_t *const *apps, int count, mono_tiles_open_cb_t on_open, void *ctx)
{
    mono_tiles_t *t = calloc(1, sizeof(*t));
    if (count > MONO_TILES_MAX) {
        count = MONO_TILES_MAX;
    }
    t->apps = apps;
    t->count = count;
    t->on_open = on_open;
    t->ctx = ctx;
    t->root = mono_container(parent, 0, 0, MONO_SCREEN_W, MONO_CONTENT_H);

    t->strip = mono_container(t->root, 0, ICON_Y, count * TILE_SPACING, ICON_SIZE);
    for (int i = 0; i < count; i++) {
        lv_obj_t *img = lv_image_create(t->strip);
        lv_image_set_src(img, apps[i]->icon);
        lv_obj_set_style_image_recolor(img, mono_fg(), 0);
        lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
        lv_obj_set_pos(img, i * TILE_SPACING, 0);
        lv_obj_set_clickable(img, false);
        t->icons[i] = img;
    }

    t->frame = mono_frame(t->root);
    lv_obj_set_style_radius(t->frame, 14, 0);
    lv_obj_set_size(t->frame, ICON_SIZE + 28, ICON_SIZE + 28);
    lv_obj_set_pos(t->frame, CENTER_X - (ICON_SIZE + 28) / 2, ICON_Y - 14);

    t->title = mono_label(t->root, "", MONO_FONT_LARGE);

    int dots_w = count * 14;
    for (int i = 0; i < count; i++) {
        t->dots[i] = mono_fill(t->root);
        lv_obj_set_size(t->dots[i], 6, 6);
        lv_obj_set_pos(t->dots[i], CENTER_X - dots_w / 2 + i * 14, DOTS_Y);
    }

    apply(t, false);
    return t;
}

void mono_tiles_select(mono_tiles_t *t, int index, bool animate)
{
    if (index < 0) {
        index = 0;
    }
    if (index >= t->count) {
        index = t->count - 1;
    }
    t->selected = index;
    apply(t, animate);
}

void mono_tiles_show(mono_tiles_t *t)
{
    lv_obj_set_hidden(t->root, false);
    /* Slide the strip in from slightly off-centre for a bit of life. */
    lv_obj_set_x(t->strip, strip_x_for(t->selected) + 40);
    lv_obj_set_style_opa(t->frame, LV_OPA_0, 0);
    mono_anim_opa(t->frame, LV_OPA_COVER, MONO_ANIM_MS * 2);
    apply(t, true);
}

bool mono_tiles_handle_input(mono_tiles_t *t, const ui_input_t *ev)
{
    switch (ev->type) {
    case UI_INPUT_SWIPE_LEFT:
        mono_tiles_select(t, t->selected + 1, true);
        return true;
    case UI_INPUT_SWIPE_RIGHT:
        mono_tiles_select(t, t->selected - 1, true);
        return true;
    case UI_INPUT_TAP: {
        /* Tap on a neighbouring icon moves to it; tap on the centre opens. */
        int rel = (ev->x - CENTER_X);
        int step = (rel + (rel >= 0 ? TILE_SPACING / 2 : -TILE_SPACING / 2)) / TILE_SPACING;
        if (step != 0) {
            mono_tiles_select(t, t->selected + step, true);
            return true;
        }
        if (t->on_open) {
            t->on_open(t->selected, t->ctx);
        }
        return true;
    }
    default:
        return false;
    }
}
