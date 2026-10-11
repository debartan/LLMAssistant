#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

/* MonoUI look: black background, white one-bit graphics, pixel fonts, and
 * eased animations for every movement. These helpers keep every screen in
 * the same style and keep LVGL styling out of the apps. */

#define MONO_SCREEN_W   368
#define MONO_SCREEN_H   448
#define MONO_STATUS_H   28
#define MONO_CONTENT_H  (MONO_SCREEN_H - MONO_STATUS_H)
#define MONO_ANIM_MS    220
#define MONO_PAD        12

extern const lv_font_t mono_font_32;        /* 16x32 pixel font, generated */
#define MONO_FONT_LARGE (&mono_font_32)
#define MONO_FONT_SMALL (&lv_font_unscii_16)
#define MONO_FONT_TINY  (&lv_font_unscii_8)

lv_color_t mono_fg(void);
lv_color_t mono_bg(void);

/* Paint the screen black and set default text style. */
void mono_theme_apply(lv_obj_t *screen);

/* A transparent, padding-free container that does not catch touches, so the
 * screen-level gesture handler sees everything. Use it for all layout. */
lv_obj_t *mono_container(lv_obj_t *parent, int x, int y, int w, int h);
/* A label in the given font; width LV_SIZE_CONTENT unless you set it. */
lv_obj_t *mono_label(lv_obj_t *parent, const char *text, const lv_font_t *font);
/* A label that wraps inside a fixed width. */
lv_obj_t *mono_paragraph(lv_obj_t *parent, const char *text, const lv_font_t *font, int w);
/* Rounded 2 px white frame: the MonoUI selection box. */
lv_obj_t *mono_frame(lv_obj_t *parent);
/* Solid white rectangle: scrollbar thumbs, meters, separators. */
lv_obj_t *mono_fill(lv_obj_t *parent);

/* Eased animations. A new animation on the same (var, exec) pair replaces the
 * running one, so interrupted moves blend instead of jumping. */
void mono_anim(void *var, lv_anim_exec_xcb_t exec, int32_t from, int32_t to, uint32_t ms, lv_anim_path_cb_t path);
void mono_anim_x(lv_obj_t *obj, int32_t to, uint32_t ms);
void mono_anim_y(lv_obj_t *obj, int32_t to, uint32_t ms);
void mono_anim_w(lv_obj_t *obj, int32_t to, uint32_t ms);
void mono_anim_h(lv_obj_t *obj, int32_t to, uint32_t ms);
void mono_anim_opa(lv_obj_t *obj, int32_t to, uint32_t ms);

/* Brief message at the bottom of the screen. */
void mono_toast(const char *text);

/* Width in pixels of `text` rendered in `font`. */
int mono_text_width(const char *text, const lv_font_t *font);
