#include <string.h>
#include "board/board.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "bsp/display.h"
#include "bsp/touch.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board";

#define BOOT_BUTTON_GPIO        GPIO_NUM_0

/* AXP2101 power management chip (register map from the XPowersLib driver). */
#define AXP2101_ADDR            0x34
#define AXP2101_REG_STATUS1     0x00    /* bit 3: battery present, bit 5: VBUS good */
#define AXP2101_REG_STATUS2     0x01    /* bits 7:5 == 1: charging; bit 3 == 0 && VBUS good: USB in */
#define AXP2101_REG_IC_TYPE     0x03    /* reads 0x4A */
#define AXP2101_REG_ADC_CTRL    0x30    /* bit 0: enable battery voltage ADC */
#define AXP2101_REG_BAT_PERCENT 0xA4
#define AXP2101_CHIP_ID         0x4A

/* Touch controllers fitted to the two board revisions. */
#define TOUCH_ADDR_FT3168       0x38
#define TOUCH_ADDR_CST820       0x15

/* TCA9554 IO expander. Measured on the board: the factory firmware leaves
 * EXIO0, 1, 2 and 7 as outputs driven high; EXIO2 is the touch controller's
 * reset, EXIO4 reads the PWR button (high = pressed), EXIO7 is the SD card
 * chip select. */
#define TCA9554_ADDR            0x20
#define TCA9554_REG_INPUT       0x00
#define TCA9554_REG_OUTPUT      0x01
#define TCA9554_REG_CONFIG      0x03    /* 1 = input */
#define EXIO_TOUCH_RESET        2
#define EXIO_PWR_BUTTON         4

static lv_display_t *s_display;
static i2c_master_dev_handle_t s_pmu;
static int s_brightness = CONFIG_ASSISTANT_BRIGHTNESS;
static bool s_touch_ok;
static i2c_master_dev_handle_t s_exp;

/* ------------------------------------------------------------------ PMU */

static esp_err_t pmu_read(uint8_t reg, uint8_t *val)
{
    if (!s_pmu) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit_receive(s_pmu, &reg, 1, val, 1, 50);
}

static esp_err_t pmu_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_pmu, buf, sizeof(buf), 50);
}

static void pmu_init(void)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_ADDR,
        .scl_speed_hz = CONFIG_BSP_I2C_CLK_SPEED_HZ,
    };
    if (i2c_master_bus_add_device(bsp_i2c_get_handle(), &cfg, &s_pmu) != ESP_OK) {
        ESP_LOGW(TAG, "AXP2101 not attached; battery readings disabled");
        s_pmu = NULL;
        return;
    }
    uint8_t id = 0;
    if (pmu_read(AXP2101_REG_IC_TYPE, &id) != ESP_OK || id != AXP2101_CHIP_ID) {
        ESP_LOGW(TAG, "AXP2101 not found (id=0x%02x)", id);
        i2c_master_bus_rm_device(s_pmu);
        s_pmu = NULL;
        return;
    }
    uint8_t adc = 0;
    if (pmu_read(AXP2101_REG_ADC_CTRL, &adc) == ESP_OK) {
        pmu_write(AXP2101_REG_ADC_CTRL, adc | 0x01);
    }
    ESP_LOGI(TAG, "AXP2101 ready, battery %d%%", board_battery_percent());
}

/* Diagnostics for bring-up: which I2C addresses answer, and what the PMU's
 * rails look like. Logged once at boot, before the display is started. */
static void i2c_scan(void)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (!bus) {
        ESP_LOGE(TAG, "i2c bus unavailable");
        return;
    }
    char found[160] = "";
    size_t n = 0;
    for (int a = 0x08; a <= 0x77 && n < sizeof(found) - 8; a++) {
        if (i2c_master_probe(bus, a, 50) == ESP_OK) {
            n += snprintf(found + n, sizeof(found) - n, " 0x%02x", a);
        }
    }
    ESP_LOGI(TAG, "i2c devices:%s", n ? found : " none");
}

static void pmu_dump(void)
{
    static const uint8_t regs[] = { 0x00, 0x01, 0x03, 0x80, 0x82, 0x90, 0x91,
                                    0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A };
    char line[200] = "";
    size_t n = 0;
    for (size_t i = 0; i < sizeof(regs) && n < sizeof(line) - 12; i++) {
        uint8_t v = 0;
        if (pmu_read(regs[i], &v) == ESP_OK) {
            n += snprintf(line + n, sizeof(line) - n, " %02x=%02x", regs[i], v);
        }
    }
    ESP_LOGI(TAG, "AXP2101 regs:%s", line);
}

/* ------------------------------------------------------------- expander */

static esp_err_t exp_read(uint8_t reg, uint8_t *val)
{
    if (!s_exp) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit_receive(s_exp, &reg, 1, val, 1, 50);
}

static esp_err_t exp_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return s_exp ? i2c_master_transmit(s_exp, buf, sizeof(buf), 50) : ESP_ERR_INVALID_STATE;
}

static void expander_init(void)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TCA9554_ADDR,
        .scl_speed_hz = CONFIG_BSP_I2C_CLK_SPEED_HZ,
    };
    if (i2c_master_bus_add_device(bsp_i2c_get_handle(), &cfg, &s_exp) != ESP_OK) {
        s_exp = NULL;
        ESP_LOGW(TAG, "TCA9554 not attached");
        return;
    }
    uint8_t in = 0, out = 0, dir = 0;
    if (exp_read(TCA9554_REG_INPUT, &in) != ESP_OK || exp_read(TCA9554_REG_OUTPUT, &out) != ESP_OK ||
        exp_read(TCA9554_REG_CONFIG, &dir) != ESP_OK) {
        ESP_LOGW(TAG, "TCA9554 not answering");
        i2c_master_bus_rm_device(s_exp);
        s_exp = NULL;
        return;
    }
    ESP_LOGI(TAG, "TCA9554 input=%02x output=%02x config=%02x", in, out, dir);
}

static bool touch_answers(i2c_master_bus_handle_t bus)
{
    return i2c_master_probe(bus, TOUCH_ADDR_FT3168, 20) == ESP_OK ||
           i2c_master_probe(bus, TOUCH_ADDR_CST820, 20) == ESP_OK;
}

/* Pulse one expander pin low for 20 ms and leave it as it was found: an
 * output goes back high, an input is released to its external pull-up. */
static void exp_pulse_low(uint8_t pin)
{
    uint8_t out = 0, dir = 0;
    if (exp_read(TCA9554_REG_OUTPUT, &out) != ESP_OK || exp_read(TCA9554_REG_CONFIG, &dir) != ESP_OK) {
        return;
    }
    uint8_t bit = 1u << pin;
    exp_write(TCA9554_REG_OUTPUT, out & ~bit);
    exp_write(TCA9554_REG_CONFIG, dir & ~bit);
    vTaskDelay(pdMS_TO_TICKS(20));
    exp_write(TCA9554_REG_CONFIG, dir);
    exp_write(TCA9554_REG_OUTPUT, out | bit);
}

/* The touch controller's reset is behind the expander, and the controller
 * stays silent after a chip reset that interrupted a read until it is reset
 * itself. Reset it, then fall back to trying the other pins. */
static bool touch_reset_via_expander(void)
{
    static const uint8_t candidates[] = { EXIO_TOUCH_RESET, 1, 0, 6, 3, 5, 7 };
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    for (size_t i = 0; i < sizeof(candidates); i++) {
        exp_pulse_low(candidates[i]);
        vTaskDelay(pdMS_TO_TICKS(250));
        if (touch_answers(bus)) {
            if (candidates[i] != EXIO_TOUCH_RESET) {
                ESP_LOGW(TAG, "touch controller answered after pulsing EXIO%d, not EXIO%d",
                         candidates[i], EXIO_TOUCH_RESET);
            } else {
                ESP_LOGI(TAG, "touch controller reset through EXIO%d", EXIO_TOUCH_RESET);
            }
            return true;
        }
    }
    return false;
}

/* -------------------------------------------------------------- display */

/* The touch controller can be silent for a while after a chip reset that
 * interrupted a read. Give it a moment. */
static bool touch_wait_ready(void)
{
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    for (int i = 0; i < 8; i++) {
        if (touch_answers(bus)) {
            ESP_LOGI(TAG, "touch controller ready after %d ms", i * 25);
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(25));
    }
    if (touch_reset_via_expander()) {
        return true;
    }
    ESP_LOGW(TAG, "touch controller not answering; continuing without touch");
    return false;
}

/* The panel wants even-aligned update areas (same as the BSP's rounder). */
static void rounder_event_cb(lv_event_t *e)
{
    lv_area_t *area = lv_event_get_param(e);
    area->x1 &= ~1;
    area->y1 &= ~1;
    area->x2 |= 1;
    area->y2 |= 1;
}

/* Same bring-up as bsp_display_start(), using the BSP's public pieces, but a
 * touch controller that does not answer is not fatal: the BSP's own version
 * aborts the boot in that case. */
static lv_display_t *display_start(void)
{
    const lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    if (lvgl_port_init(&port_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "lvgl port init failed");
        return NULL;
    }

    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_io_handle_t io = NULL;
    bsp_display_config_t disp_config = { 0 };
    if (bsp_display_new(&disp_config, &panel, &io) != ESP_OK) {
        ESP_LOGE(TAG, "panel init failed");
        return NULL;
    }

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io,
        .panel_handle = panel,
        .buffer_size = BSP_LCD_H_RES * LVGL_BUFFER_HEIGHT,
        .monochrome = false,
        .hres = BSP_LCD_H_RES,
        .vres = BSP_LCD_V_RES,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
        .flags = { .sw_rotate = true, .buff_dma = false, .swap_bytes = true },
    };
    const lvgl_port_display_rgb_cfg_t rgb_cfg = { .flags = { .bb_mode = 0, .avoid_tearing = false } };
    lv_display_t *disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    if (!disp) {
        ESP_LOGE(TAG, "lvgl display add failed");
        return NULL;
    }
    lv_display_add_event_cb(disp, rounder_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);

    if (s_touch_ok) {
        esp_lcd_touch_handle_t tp = NULL;
        if (bsp_touch_new(NULL, &tp) == ESP_OK && tp) {
            const lvgl_port_touch_cfg_t touch_cfg = { .disp = disp, .handle = tp };
            s_touch_ok = lvgl_port_add_touch(&touch_cfg) != NULL;
        } else {
            s_touch_ok = false;
        }
    }
    return disp;
}

esp_err_t board_init(void)
{
    i2c_scan();
    pmu_init();
    pmu_dump();
    expander_init();
    s_touch_ok = touch_wait_ready();

    s_display = display_start();
    if (s_display == NULL) {
        ESP_LOGE(TAG, "display failed to start");
        return ESP_FAIL;
    }
    if (!s_touch_ok) {
        ESP_LOGW(TAG, "running without touch; use the buttons");
    }
    board_set_brightness(s_brightness);

    gpio_config_t btn = {
        .pin_bit_mask = 1ULL << BOOT_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&btn);
    return ESP_OK;
}

lv_display_t *board_display(void)
{
    return s_display;
}

bool board_touch_available(void)
{
    return s_touch_ok;
}

void board_set_brightness(int percent)
{
    if (percent < 5) {
        percent = 5;
    }
    if (percent > 100) {
        percent = 100;
    }
    s_brightness = percent;
    bsp_display_brightness_set(percent);
}

int board_get_brightness(void)
{
    return s_brightness;
}

int board_battery_percent(void)
{
    uint8_t st = 0, pct = 0;
    if (pmu_read(AXP2101_REG_STATUS1, &st) != ESP_OK || !(st & (1 << 3))) {
        return -1;
    }
    if (pmu_read(AXP2101_REG_BAT_PERCENT, &pct) != ESP_OK) {
        return -1;
    }
    return pct > 100 ? 100 : pct;
}

bool board_battery_charging(void)
{
    uint8_t st = 0;
    return pmu_read(AXP2101_REG_STATUS2, &st) == ESP_OK && (st >> 5) == 0x01;
}

bool board_usb_present(void)
{
    uint8_t st1 = 0, st2 = 0;
    if (pmu_read(AXP2101_REG_STATUS1, &st1) != ESP_OK || pmu_read(AXP2101_REG_STATUS2, &st2) != ESP_OK) {
        return false;
    }
    return (st1 & (1 << 5)) && !(st2 & (1 << 3));
}

bool board_button_pressed(void)
{
    return gpio_get_level(BOOT_BUTTON_GPIO) == 0;
}

bool board_pwr_button_pressed(void)
{
    uint8_t in = 0;
    return exp_read(TCA9554_REG_INPUT, &in) == ESP_OK && (in & (1u << EXIO_PWR_BUTTON));
}
