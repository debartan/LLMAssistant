#pragma once
#include "lvgl.h"

/* Top bar: link state on the left, clock in the middle, battery on the right. */
lv_obj_t *mono_status_create(lv_obj_t *parent);
void mono_status_update(void);     /* call about once a second */
