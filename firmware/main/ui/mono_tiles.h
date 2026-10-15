#pragma once
#include <stdbool.h>
#include "lvgl.h"
#include "ui/ui_input.h"
#include "apps/app.h"

/* The launcher: a horizontal strip of large icons. The selected icon sits in
 * a frame at the centre with its title below; swipes glide the strip. */

#define MONO_TILES_MAX 12

typedef struct mono_tiles mono_tiles_t;
typedef void (*mono_tiles_open_cb_t)(int index, void *ctx);

struct mono_tiles {
    lv_obj_t *root;
    lv_obj_t *strip;
    lv_obj_t *icons[MONO_TILES_MAX];
    lv_obj_t *frame;
    lv_obj_t *title;
    lv_obj_t *dots[MONO_TILES_MAX];
    int count;
    int selected;
    app_t *const *apps;
    mono_tiles_open_cb_t on_open;
    void *ctx;
};

mono_tiles_t *mono_tiles_create(lv_obj_t *parent, app_t *const *apps, int count, mono_tiles_open_cb_t on_open, void *ctx);
void mono_tiles_select(mono_tiles_t *t, int index, bool animate);
bool mono_tiles_handle_input(mono_tiles_t *t, const ui_input_t *ev);
/* Replay the entrance animation (used when returning from an app). */
void mono_tiles_show(mono_tiles_t *t);
