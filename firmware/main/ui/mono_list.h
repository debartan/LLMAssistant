#pragma once
#include <stdbool.h>
#include "lvgl.h"
#include "ui/ui_input.h"

/* MonoUI list: rows of text, a selection frame that glides between rows and
 * stretches to the text width, and a thin scrollbar on the right. Rows scroll
 * only when the selection reaches the edge of the visible area. */

#define MONO_LIST_MAX_ITEMS 64

typedef struct mono_list mono_list_t;
typedef void (*mono_list_select_cb_t)(mono_list_t *list, int index, void *ctx);

struct mono_list {
    lv_obj_t *root;
    lv_obj_t *rows_cont;
    lv_obj_t *rows[MONO_LIST_MAX_ITEMS];
    lv_obj_t *frame;
    lv_obj_t *track;
    lv_obj_t *thumb;
    int count;
    int selected;
    int first_visible;
    int width, height, row_h, visible_rows;
    bool wrap;
    mono_list_select_cb_t on_select;
    void *ctx;
};

mono_list_t *mono_list_create(lv_obj_t *parent, int x, int y, int w, int h);
void mono_list_delete(mono_list_t *l);
void mono_list_set_items(mono_list_t *l, const char *const *items, int count);
void mono_list_set_on_select(mono_list_t *l, mono_list_select_cb_t cb, void *ctx);
void mono_list_select(mono_list_t *l, int index, bool animate);
void mono_list_move(mono_list_t *l, int delta);
int  mono_list_selected(const mono_list_t *l);
/* Swipes move the selection; a tap selects the row under the finger and
 * fires on_select. Returns true when the event was used. */
bool mono_list_handle_input(mono_list_t *l, const ui_input_t *ev);
