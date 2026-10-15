#include <string.h>
#include "ui/mono.h"

lv_color_t mono_fg(void)
{
    return lv_color_white();
}

lv_color_t mono_bg(void)
{
    return lv_color_black();
}

void mono_theme_apply(lv_obj_t *screen)
{
    lv_obj_set_style_bg_color(screen, mono_bg(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(screen, mono_fg(), 0);
    lv_obj_set_style_text_font(screen, MONO_FONT_SMALL, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_scrollable(screen, false);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
}

lv_obj_t *mono_container(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_clickable(c, false);
    lv_obj_set_scrollable(c, false);
    lv_obj_set_scrollbar_mode(c, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_text_color(c, mono_fg(), 0);
    return c;
}

lv_obj_t *mono_label(lv_obj_t *parent, const char *text, const lv_font_t *font)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_clickable(l, false);     /* touches go to the screen handler */
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, mono_fg(), 0);
    lv_label_set_text(l, text ? text : "");
    return l;
}

lv_obj_t *mono_paragraph(lv_obj_t *parent, const char *text, const lv_font_t *font, int w)
{
    lv_obj_t *l = mono_label(parent, text, font);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_line_space(l, 4, 0);
    return l;
}

lv_obj_t *mono_frame(lv_obj_t *parent)
{
    lv_obj_t *f = mono_container(parent, 0, 0, 10, 10);
    lv_obj_set_style_border_color(f, mono_fg(), 0);
    lv_obj_set_style_border_opa(f, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(f, 2, 0);
    lv_obj_set_style_radius(f, 6, 0);
    return f;
}

lv_obj_t *mono_fill(lv_obj_t *parent)
{
    lv_obj_t *f = mono_container(parent, 0, 0, 10, 10);
    lv_obj_set_style_bg_color(f, mono_fg(), 0);
    lv_obj_set_style_bg_opa(f, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(f, 2, 0);
    return f;
}

void mono_anim(void *var, lv_anim_exec_xcb_t exec, int32_t from, int32_t to, uint32_t ms, lv_anim_path_cb_t path)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, var);
    lv_anim_set_exec_cb(&a, exec);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, path ? path : lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void exec_x(void *obj, int32_t v) { lv_obj_set_x(obj, v); }
static void exec_y(void *obj, int32_t v) { lv_obj_set_y(obj, v); }
static void exec_w(void *obj, int32_t v) { lv_obj_set_width(obj, v); }
static void exec_h(void *obj, int32_t v) { lv_obj_set_height(obj, v); }
static void exec_opa(void *obj, int32_t v) { lv_obj_set_style_opa(obj, (lv_opa_t)v, 0); }

void mono_anim_x(lv_obj_t *obj, int32_t to, uint32_t ms)
{
    mono_anim(obj, exec_x, lv_obj_get_x(obj), to, ms, NULL);
}

void mono_anim_y(lv_obj_t *obj, int32_t to, uint32_t ms)
{
    mono_anim(obj, exec_y, lv_obj_get_y(obj), to, ms, NULL);
}

void mono_anim_w(lv_obj_t *obj, int32_t to, uint32_t ms)
{
    mono_anim(obj, exec_w, lv_obj_get_width(obj), to, ms, lv_anim_path_overshoot);
}

void mono_anim_h(lv_obj_t *obj, int32_t to, uint32_t ms)
{
    mono_anim(obj, exec_h, lv_obj_get_height(obj), to, ms, NULL);
}

void mono_anim_opa(lv_obj_t *obj, int32_t to, uint32_t ms)
{
    mono_anim(obj, exec_opa, lv_obj_get_style_opa(obj, 0), to, ms, NULL);
}

/* ------------------------------------------------------------------ toast */

static lv_obj_t *s_toast;
static lv_timer_t *s_toast_timer;

static void toast_hide(lv_timer_t *t)
{
    if (s_toast) {
        lv_obj_delete(s_toast);
        s_toast = NULL;
    }
    lv_timer_delete(t);
    s_toast_timer = NULL;
}

void mono_toast(const char *text)
{
    if (s_toast) {
        lv_obj_delete(s_toast);
        s_toast = NULL;
    }
    if (s_toast_timer) {
        lv_timer_delete(s_toast_timer);
        s_toast_timer = NULL;
    }
    lv_obj_t *layer = lv_layer_top();
    int w = mono_text_width(text, MONO_FONT_SMALL) + 2 * MONO_PAD;
    if (w > MONO_SCREEN_W - 2 * MONO_PAD) {
        w = MONO_SCREEN_W - 2 * MONO_PAD;
    }
    s_toast = mono_frame(layer);
    lv_obj_set_style_bg_color(s_toast, mono_bg(), 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, 0);
    lv_obj_set_size(s_toast, w, 40);
    lv_obj_set_pos(s_toast, (MONO_SCREEN_W - w) / 2, MONO_SCREEN_H);
    lv_obj_t *l = mono_paragraph(s_toast, text, MONO_FONT_SMALL, w - 2 * MONO_PAD);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, 0);
    mono_anim_y(s_toast, MONO_SCREEN_H - 56, MONO_ANIM_MS);
    s_toast_timer = lv_timer_create(toast_hide, 2200, NULL);
}

int mono_text_width(const char *text, const lv_font_t *font)
{
    lv_point_t size;
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}
