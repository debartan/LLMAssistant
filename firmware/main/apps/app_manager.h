#pragma once
#include <stdbool.h>
#include "lvgl.h"
#include "apps/app.h"

void app_manager_init(void);
void app_manager_open(app_t *app);
void app_manager_back(void);
app_t *app_manager_current(void);          /* NULL while on the launcher */

/* Called by the link layer (any task): hands ownership of `json` to the
 * manager, which frees it after dispatch on the LVGL thread. */
void app_manager_post_message(char *json);
void app_manager_post_link_state(bool connected);
