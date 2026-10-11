#include <string.h>
#include "board/board.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

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

static lv_display_t *s_display;
static i2c_master_dev_handle_t s_pmu;
static int s_brightness = CONFIG_ASSISTANT_BRIGHTNESS;

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

esp_err_t board_init(void)
{
    s_display = bsp_display_start();
    if (s_display == NULL) {
        ESP_LOGE(TAG, "display failed to start");
        return ESP_FAIL;
    }
    board_set_brightness(s_brightness);

    gpio_config_t btn = {
        .pin_bit_mask = 1ULL << BOOT_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&btn);

    pmu_init();
    return ESP_OK;
}

lv_display_t *board_display(void)
{
    return s_display;
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
