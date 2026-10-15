/* The launcher, in order. To add an app: write apps/app_<id>.c that defines
 * an `app_t app_<id>`, draw assets/icons/<id>.txt, run tools/gen_icons.py,
 * then add the app here. */
#include "apps/app.h"

extern app_t app_chat;
extern app_t app_notes;
extern app_t app_settings;

app_t *const app_registry[] = {
    &app_chat,
    &app_notes,
    &app_settings,
};

const int app_registry_count = sizeof(app_registry) / sizeof(app_registry[0]);
