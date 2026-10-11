/* Boot sequence: storage -> board (display, touch, power) -> audio codec ->
 * user interface -> network. The UI comes up before Wi-Fi so the device is
 * usable within a couple of seconds of power-on. */
#include "esp_log.h"
#include "nvs_flash.h"
#include "board/board.h"
#include "audio/audio.h"
#include "link/link.h"
#include "apps/app_manager.h"

static const char *TAG = "main";

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(board_init());

    if (audio_init() != ESP_OK) {
        ESP_LOGE(TAG, "audio unavailable; continuing without it");
    }

    bsp_display_lock(0);
    app_manager_init();
    bsp_display_unlock();

    ESP_ERROR_CHECK(link_init());
    ESP_LOGI(TAG, "ready");
}
