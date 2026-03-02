//  SPDX-FileCopyrightText: 2024 PreviousTube Contributors
//  SPDX-License-Identifier: MIT
//
// Speaker driver using I2S with built-in DAC output.
//
// Hardware: LTK8002D class-D amplifier connected to ESP32 DAC channel 1 (GPIO25).
// The stock Nextube firmware drives this via I2S in internal DAC mode using
// the ESP8266Audio library. We replicate this approach using the ESP-IDF
// legacy I2S driver for direct tone synthesis without needing external libs.
//
// I2S internal DAC mode outputs on both DAC channels (GPIO25 + GPIO26),
// but only GPIO25 is wired to the amplifier on the Nextube PCB.

#include "speaker.h"

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

static bool initialized = false;
static uint8_t current_volume = 80; // 0-100

void speaker_init() {
  ESP_LOGI(TAG, "Initializing speaker via I2S internal DAC (GPIO25)");

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
    ESP_LOGE(TAG, "I2S driver install failed: %s, speaker disabled", esp_err_to_name(err));
    return;
  }

  err = i2s_set_dac_mode(I2S_DAC_CHANNEL_BOTH_EN);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "I2S DAC mode failed: %s", esp_err_to_name(err));
    i2s_driver_uninstall(I2S_PORT);
    return;
  }

  // Write silence to initialize
  uint8_t silence[DMA_BUF_LEN * 4];
  memset(silence, 0x80, sizeof(silence)); // 0x80 = mid-scale for unsigned DAC
  size_t written;
  i2s_write(I2S_PORT, silence, sizeof(silence), &written, portMAX_DELAY);

  initialized = true;
}

void speaker_deinit() {
  if (!initialized) return;
  i2s_driver_uninstall(I2S_PORT);
  initialized = false;
}

void speaker_set_volume(uint8_t volume) {
  if (volume > 100) volume = 100;
  current_volume = volume;
}

// Generate and play a sine wave tone
// I2S internal DAC uses unsigned 8-bit samples in the upper byte of 16-bit words
static void generate_tone(uint32_t freq_hz, uint32_t duration_ms) {
  if (!initialized || freq_hz == 0) return;

  uint32_t num_samples = (SAMPLE_RATE * duration_ms) / 1000;
  // I2S DAC mode: each sample is 16-bit, but only upper 8 bits go to DAC
  // Format: [left_high][left_low][right_high][right_low] per frame
  // For internal DAC, the DAC value is taken from the MSB

  const size_t CHUNK = 512; // samples per chunk
  uint16_t buf[CHUNK * 2]; // stereo: L+R per sample

  float phase = 0.0f;
  float phase_inc = (2.0f * M_PI * freq_hz) / SAMPLE_RATE;
  float vol_scale = current_volume / 100.0f;

  // Apply fade envelope to avoid clicks
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

      // Apply fade in/out envelope
      float envelope = 1.0f;
      uint32_t abs_sample = samples_written + i;
      if (abs_sample < fade_samples) {
        envelope = (float)abs_sample / fade_samples;
      } else if (abs_sample > num_samples - fade_samples) {
        envelope = (float)(num_samples - abs_sample) / fade_samples;
      }
      sample *= envelope;

      // Convert to unsigned 8-bit for DAC (0x00-0xFF, 0x80 = silence)
      // Shift to upper byte of 16-bit word for I2S DAC mode
      uint16_t dac_val = (uint16_t)((sample * 127.0f + 128.0f)) << 8;

      // Write same value to both channels (only right/GPIO25 is connected)
      buf[i * 2] = dac_val;     // left
      buf[i * 2 + 1] = dac_val; // right
    }

    size_t written;
    i2s_write(I2S_PORT, buf, chunk_size * 4, &written, portMAX_DELAY);
    samples_written += chunk_size;
  }

  // Write a bit of silence after to let amp settle
  uint16_t silence[64 * 2];
  for (int i = 0; i < 128; i++) silence[i] = 0x8000;
  size_t written;
  i2s_write(I2S_PORT, silence, sizeof(silence), &written, portMAX_DELAY);
}

void speaker_play_tone(uint32_t freq_hz, uint32_t duration_ms) {
  generate_tone(freq_hz, duration_ms);
}

void speaker_stop() {
  if (!initialized) return;
  i2s_zero_dma_buffer(I2S_PORT);
}

void speaker_play_click() {
  // Short high-pitched click for tactile feedback
  generate_tone(3000, 20);
}

void speaker_play_chime() {
  // Two-tone ascending chime: C5 (523Hz) then E5 (659Hz)
  generate_tone(523, 150);
  vTaskDelay(pdMS_TO_TICKS(30));
  generate_tone(659, 200);
}

void speaker_play_startup() {
  // Three-tone ascending startup jingle: C4, E4, G4
  generate_tone(262, 100);
  vTaskDelay(pdMS_TO_TICKS(20));
  generate_tone(330, 100);
  vTaskDelay(pdMS_TO_TICKS(20));
  generate_tone(392, 150);
}
