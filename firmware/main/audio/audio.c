/* Microphone capture and speaker playback through the ES8311 codec.
 *
 * One full-duplex codec device is opened once at 16 kHz. A capture task
 * reads 20 ms frames and ships them to the link; a playback task drains a
 * stream buffer that the link fills with speech from the server.
 */
#include <string.h>
#include <math.h>
#include "audio/audio.h"
#include "link/link.h"
#include "board/board.h"
#include "esp_codec_dev_defaults.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"

static const char *TAG = "audio";

#define FRAME_BYTES         (AUDIO_FRAME_SAMPLES * 2)
#define PLAYBACK_BUF_BYTES  (AUDIO_SAMPLE_RATE * 2 * 4)   /* 4 s of speech */

static esp_codec_dev_handle_t s_codec;
static StreamBufferHandle_t s_playback;
static volatile bool s_capture_enabled;
static volatile bool s_capture_running;
static volatile bool s_playing;
static volatile int s_level;
static int s_volume = CONFIG_ASSISTANT_SPEAKER_VOLUME;
static TaskHandle_t s_capture_task;

/* ----------------------------------------------------------------- codec */

static esp_err_t codec_init(void)
{
    ESP_ERROR_CHECK(bsp_i2c_init());

    const i2s_std_config_t cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK,
            .bclk = BSP_I2S_SCLK,
            .ws = BSP_I2S_LCLK,
            .dout = BSP_I2S_DOUT,
            .din = BSP_I2S_DSIN,
        },
    };
    i2s_chan_handle_t tx = NULL, rx = NULL;
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(CONFIG_BSP_I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx, &rx));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx, &cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx, &cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx));
    ESP_ERROR_CHECK(i2s_channel_enable(rx));

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = CONFIG_BSP_I2S_NUM,
        .tx_handle = tx,
        .rx_handle = rx,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = BSP_I2C_NUM,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = bsp_i2c_get_handle(),
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();

    es8311_codec_cfg_t es_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .pa_pin = BSP_POWER_AMP_IO,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .hw_gain = { .pa_voltage = 5.0, .codec_dac_voltage = 3.3 },
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es_cfg);
    if (!data_if || !ctrl_if || !codec_if) {
        ESP_LOGE(TAG, "codec interfaces failed");
        return ESP_FAIL;
    }

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    s_codec = esp_codec_dev_new(&dev_cfg);
    if (!s_codec) {
        ESP_LOGE(TAG, "codec device failed");
        return ESP_FAIL;
    }

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 1,
        .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0),
        .sample_rate = AUDIO_SAMPLE_RATE,
    };
    int ret = esp_codec_dev_open(s_codec, &fs);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "codec open failed (%d)", ret);
        return ESP_FAIL;
    }
    esp_codec_dev_set_in_gain(s_codec, (float)CONFIG_ASSISTANT_MIC_GAIN_DB);
    esp_codec_dev_set_out_vol(s_codec, s_volume);
    return ESP_OK;
}

/* --------------------------------------------------------------- capture */

static void capture_task(void *arg)
{
    /* Frame = format byte + samples, built in place so no copy is needed. */
    uint8_t *frame = heap_caps_malloc(1 + FRAME_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    frame[0] = LINK_AUDIO_PCM16_16K;
    int16_t *pcm = (int16_t *)(frame + 1);

    for (;;) {
        if (!s_capture_enabled) {
            s_capture_running = false;
            s_level = 0;
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        s_capture_running = true;
        if (esp_codec_dev_read(s_codec, pcm, FRAME_BYTES) != ESP_CODEC_DEV_OK) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        /* Loudness for the on-screen meter: scaled root mean square. */
        uint64_t acc = 0;
        for (int i = 0; i < AUDIO_FRAME_SAMPLES; i++) {
            acc += (int32_t)pcm[i] * pcm[i];
        }
        int rms = (int)sqrt((double)acc / AUDIO_FRAME_SAMPLES);
        int level = rms / 60;   /* ~6000 RMS ≈ full scale on the meter */
        s_level = level > 100 ? 100 : level;

        link_send_audio(pcm, AUDIO_FRAME_SAMPLES);
    }
}

void audio_capture_start(void)
{
    if (!s_codec) {
        return;
    }
    s_capture_enabled = true;
    xTaskNotifyGive(s_capture_task);
}

void audio_capture_stop(void)
{
    s_capture_enabled = false;
}

bool audio_capture_active(void)
{
    return s_capture_running;
}

int audio_capture_level(void)
{
    return s_level;
}

/* -------------------------------------------------------------- playback */

static void playback_task(void *arg)
{
    uint8_t *buf = heap_caps_malloc(FRAME_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    for (;;) {
        size_t n = xStreamBufferReceive(s_playback, buf, FRAME_BYTES, pdMS_TO_TICKS(200));
        if (n == 0) {
            s_playing = false;
            continue;
        }
        s_playing = true;
        esp_codec_dev_write(s_codec, buf, n);
    }
}

void audio_playback_push(const uint8_t *pcm, size_t len)
{
    if (!s_playback) {
        return;
    }
    /* Block briefly so a fast server is throttled by TCP instead of dropped. */
    size_t sent = xStreamBufferSend(s_playback, pcm, len, pdMS_TO_TICKS(500));
    if (sent < len) {
        ESP_LOGW(TAG, "playback buffer full, dropped %u bytes", (unsigned)(len - sent));
    }
}

void audio_playback_flush(void)
{
    if (s_playback) {
        xStreamBufferReset(s_playback);
    }
}

bool audio_playback_active(void)
{
    return s_playing || (s_playback && xStreamBufferBytesAvailable(s_playback) > 0);
}

void audio_set_volume(int percent)
{
    if (percent < 0) {
        percent = 0;
    }
    if (percent > 100) {
        percent = 100;
    }
    s_volume = percent;
    if (s_codec) {
        esp_codec_dev_set_out_vol(s_codec, percent);
    }
}

int audio_get_volume(void)
{
    return s_volume;
}

/* ------------------------------------------------------------------ init */

esp_err_t audio_init(void)
{
    esp_err_t err = codec_init();
    if (err != ESP_OK) {
        return err;
    }
    uint8_t *storage = heap_caps_malloc(PLAYBACK_BUF_BYTES + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    static StaticStreamBuffer_t sb_struct;
    s_playback = xStreamBufferCreateStatic(PLAYBACK_BUF_BYTES, 1, storage, &sb_struct);

    xTaskCreatePinnedToCore(playback_task, "audio_play", 4096, NULL, 6, NULL, 1);
    xTaskCreatePinnedToCore(capture_task, "audio_cap", 4096, NULL, 7, &s_capture_task, 1);
    ESP_LOGI(TAG, "codec ready at %d Hz", AUDIO_SAMPLE_RATE);
    return ESP_OK;
}
