#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define AUDIO_SAMPLE_RATE   16000
#define AUDIO_FRAME_SAMPLES 320     /* 20 ms */

esp_err_t audio_init(void);

/* Microphone -> link. Frames go straight to the server as binary frames. */
void audio_capture_start(void);
void audio_capture_stop(void);
bool audio_capture_active(void);
int  audio_capture_level(void);     /* 0..100, recent loudness, for meters */

/* Link -> speaker. Called from the WebSocket task. */
void audio_playback_push(const uint8_t *pcm, size_t len);
void audio_playback_flush(void);
bool audio_playback_active(void);

void audio_set_volume(int percent);
int  audio_get_volume(void);
