#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "cJSON.h"

#define LINK_AUDIO_PCM16_16K 0x01

esp_err_t link_init(void);
bool link_wifi_connected(void);
bool link_connected(void);                 /* WebSocket is up */
const char *link_ip(void);                 /* "" until an address is assigned */

/* JSON helpers. `obj` is not consumed; the caller still owns it. */
esp_err_t link_send_json(cJSON *obj);
esp_err_t link_send_type(const char *type);                       /* {"type": type} */
esp_err_t link_send_type_str(const char *type, const char *key, const char *value);

/* One binary audio frame: format byte + samples. Safe from any task. */
esp_err_t link_send_audio(const int16_t *pcm, size_t samples);

/* Server-provided wall clock, from sys.hello_ack. */
void link_set_time(int64_t epoch, int tz_offset_min);
bool link_local_time(int *hour, int *minute);
