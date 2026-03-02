//  SPDX-FileCopyrightText: 2024 PreviousTube Contributors
//  SPDX-License-Identifier: MIT
//
// Speaker driver using I2S with built-in DAC output.
//
// Hardware: LTK8002D class-D amplifier connected to ESP32 DAC channel 1 (GPIO25).
// The stock Nextube firmware used ESP8266Audio which initialized I2S lazily
// (only when playing audio). We replicate this: I2S is started on-demand
// and stopped after playback to keep GPIO25 silent and prevent boot chirps.

#include "speaker.h"

#include <driver/dac.h>
#include <driver/i2s.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>
#include <string.h>

static const char *TAG = "speaker";

static constexpr i2s_port_t I2S_PORT = I2S_NUM_0;
static constexpr int SAMPLE_RATE = 44100;
static constexpr int DMA_BUF_COUNT = 4;
static constexpr int DMA_BUF_LEN = 256;

static bool hw_active = false;
static bool enabled = true;
static uint8_t current_volume = 80; // 0-100

// Start I2S DAC hardware — called before each playback
static bool hw_start() {
  if (hw_active) return true;

  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN),
      .sample_rate = SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_MSB,
      .intr_alloc_flags = 0,
      .dma_buf_count = DMA_BUF_COUNT,
      .dma_buf_len = DMA_BUF_LEN,
      .use_apll = false,
      .tx_desc_auto_clear = true,
  };

  esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, nullptr);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "I2S install failed: %s", esp_err_to_name(err));
    return false;
  }

  err = i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "DAC mode failed: %s", esp_err_to_name(err));
    i2s_driver_uninstall(I2S_PORT);
    return false;
  }

  // Pre-fill with silence to prevent pop
  uint8_t silence[DMA_BUF_LEN * 4];
  memset(silence, 0x80, sizeof(silence));
  size_t written;
  i2s_write(I2S_PORT, silence, sizeof(silence), &written, portMAX_DELAY);

  hw_active = true;
  return true;
}

// Stop I2S DAC hardware — called after playback to keep GPIO25 quiet
static void hw_stop() {
  if (!hw_active) return;

  // Drain with silence to prevent pop on shutdown
  uint16_t silence[128];
  for (int i = 0; i < 128; i++) silence[i] = 0x8000;
  size_t written;
  i2s_write(I2S_PORT, silence, sizeof(silence), &written, portMAX_DELAY);

  i2s_zero_dma_buffer(I2S_PORT);
  i2s_driver_uninstall(I2S_PORT);

  // Explicitly disable DAC output to tri-state GPIO25
  dac_output_disable(DAC_CHANNEL_1); // GPIO25
  dac_output_disable(DAC_CHANNEL_2); // GPIO26

  hw_active = false;
}

void speaker_init() {
  // Lazy init — do nothing at boot. I2S starts on first playback.
  ESP_LOGI(TAG, "Speaker ready (lazy I2S init)");
  enabled = true;
}

void speaker_deinit() {
  hw_stop();
  enabled = false;
}

void speaker_set_volume(uint8_t volume) {
  if (volume > 100) volume = 100;
  current_volume = volume;
}

static void generate_tone(uint32_t freq_hz, uint32_t duration_ms) {
  if (!enabled || freq_hz == 0 || current_volume == 0) return;
  if (!hw_start()) return;

  uint32_t num_samples = (SAMPLE_RATE * duration_ms) / 1000;

  const size_t CHUNK = 512;
  uint16_t buf[CHUNK * 2];

  float phase = 0.0f;
  float phase_inc = (2.0f * M_PI * freq_hz) / SAMPLE_RATE;
  float vol_scale = current_volume / 100.0f;

  uint32_t fade_samples = SAMPLE_RATE / 200; // 5ms fade
  if (fade_samples > num_samples / 2) fade_samples = num_samples / 2;

  uint32_t samples_written = 0;
  while (samples_written < num_samples) {
    uint32_t chunk_size = num_samples - samples_written;
    if (chunk_size > CHUNK) chunk_size = CHUNK;

    for (uint32_t i = 0; i < chunk_size; i++) {
      float sample = sinf(phase) * vol_scale;
      phase += phase_inc;
      if (phase >= 2.0f * M_PI) phase -= 2.0f * M_PI;

      float envelope = 1.0f;
      uint32_t abs_sample = samples_written + i;
      if (abs_sample < fade_samples) {
        envelope = (float)abs_sample / fade_samples;
      } else if (abs_sample > num_samples - fade_samples) {
        envelope = (float)(num_samples - abs_sample) / fade_samples;
      }
      sample *= envelope;

      uint16_t dac_val = (uint16_t)((sample * 127.0f + 128.0f)) << 8;
      buf[i * 2] = dac_val;
      buf[i * 2 + 1] = dac_val;
    }

    size_t written;
    i2s_write(I2S_PORT, buf, chunk_size * 4, &written, portMAX_DELAY);
    samples_written += chunk_size;
  }
}

void speaker_play_tone(uint32_t freq_hz, uint32_t duration_ms) {
  generate_tone(freq_hz, duration_ms);
  hw_stop();
}

void speaker_stop() {
  hw_stop();
}

void speaker_play_click() {
  generate_tone(3000, 20);
  hw_stop();
}

void speaker_play_chime() {
  generate_tone(523, 150);
  vTaskDelay(pdMS_TO_TICKS(30));
  generate_tone(659, 200);
  hw_stop();
}

void speaker_play_startup() {
  generate_tone(262, 100);
  vTaskDelay(pdMS_TO_TICKS(20));
  generate_tone(330, 100);
  vTaskDelay(pdMS_TO_TICKS(20));
  generate_tone(392, 150);
  hw_stop();
}
