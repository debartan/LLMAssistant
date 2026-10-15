#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"

#define FW_VERSION "0.1.0"

esp_err_t board_init(void);               /* display, touch, I2C, power chip, button */
lv_display_t *board_display(void);
bool board_touch_available(void);     /* false when the touch controller did not answer at boot */

void board_set_brightness(int percent);
int  board_get_brightness(void);

/* Battery, read from the AXP2101 power management chip. */
int  board_battery_percent(void);          /* -1 if unknown */
bool board_battery_charging(void);
bool board_usb_present(void);

/* BOOT button (GPIO 0), active low. */
bool board_button_pressed(void);
/* PWR button, read through the IO expander (a 6 s hold powers the board off). */
bool board_pwr_button_pressed(void);
