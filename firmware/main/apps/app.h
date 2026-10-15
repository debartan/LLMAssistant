#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "lvgl.h"
#include "cJSON.h"
#include "ui/ui_input.h"

/* The device half of an app.
 *
 * Every callback runs on the LVGL thread, so an app may create and modify
 * widgets freely inside them. Messages whose type starts with "<id>." are
 * delivered to on_message with the type already stripped of the prefix.
 */
typedef struct app {
    const char *id;                 /* "chat": launcher id and message prefix */
    const char *title;              /* shown under the launcher tile */
    const lv_image_dsc_t *icon;     /* generated from assets/icons/<id>.txt */

    /* Draw the app into `parent` (a full-screen black container). */
    void (*on_enter)(struct app *self, lv_obj_t *parent);
    /* Release anything not owned by `parent`; the container is deleted after. */
    void (*on_exit)(struct app *self);
    /* Return true if the event was consumed. BACK is handled by the manager
     * when the app returns false. */
    bool (*on_input)(struct app *self, const ui_input_t *ev);
    /* `verb` is the part after "<id>.", `msg` is the full JSON object. */
    void (*on_message)(struct app *self, const char *verb, const cJSON *msg);
    /* Link to the companion server came up or went down. */
    void (*on_link_state)(struct app *self, bool connected);
    /* Called every 100 ms while the app is open, for timers and meters. */
    void (*on_tick)(struct app *self);

    void *state;                    /* private to the app */
} app_t;

/* The launcher order. Defined in app_registry.c; add new apps there. */
extern app_t *const app_registry[];
extern const int app_registry_count;
