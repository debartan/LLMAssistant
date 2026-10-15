#pragma once
#include <stdint.h>

/* Every app sees input through this small vocabulary, the way a MonoUI
 * device driven by a rotary encoder would. Touch gestures and the BOOT
 * button are translated into it by the app manager. */
typedef enum {
    UI_INPUT_TAP,          /* short press and release, x/y valid */
    UI_INPUT_LONG_PRESS,   /* held for ~0.5 s, x/y valid */
    UI_INPUT_SWIPE_UP,
    UI_INPUT_SWIPE_DOWN,
    UI_INPUT_SWIPE_LEFT,
    UI_INPUT_SWIPE_RIGHT,
    UI_INPUT_BACK,         /* BOOT button */
} ui_input_type_t;

typedef struct {
    ui_input_type_t type;
    int16_t x;
    int16_t y;
} ui_input_t;
