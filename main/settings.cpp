//  SPDX-FileCopyrightText: 2024 PreviousTube Contributors
//  SPDX-License-Identifier: MIT

#include "settings.h"
#include <cstring>
#include <esp_log.h>
#include <nvs.h>
#include <nvs_flash.h>

static const char *TAG = "settings";
static const char *NS = "ptube";

static void nvs_get_string(nvs_handle_t h, const char *key, std::string &out) {
  size_t len = 0;
  if (nvs_get_str(h, key, nullptr, &len) == ESP_OK && len > 0) {
    char *buf = new char[len];
    nvs_get_str(h, key, buf, &len);
    out = std::string(buf);
    delete[] buf;
  }
}

void settings::load() {
  nvs_handle_t h;
  if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return;

  nvs_get_string(h, "tz", timezone);
  uint8_t v8;
  if (nvs_get_u8(h, "time_fmt", &v8) == ESP_OK) time_format = (TimeFormat)v8;
  nvs_get_u8(h, "bright", &brightness);
  nvs_get_u8(h, "mode", &active_mode);
  nvs_get_u8(h, "led_r", &led_r);
  nvs_get_u8(h, "led_g", &led_g);
  nvs_get_u8(h, "led_b", &led_b);
  if (nvs_get_u8(h, "led_mode", &v8) == ESP_OK) led_mode = (LedMode)v8;
  if (nvs_get_u8(h, "sound", &v8) == ESP_OK) sound_enabled = v8;
  if (nvs_get_u8(h, "chime", &v8) == ESP_OK) hourly_chime = v8;
  nvs_get_u8(h, "volume", &volume);
  nvs_get_string(h, "w_key", weather_api_key);
  nvs_get_string(h, "w_city", weather_city);
  nvs_get_string(h, "w_ctry", weather_country);
  if (nvs_get_u8(h, "w_fahr", &v8) == ESP_OK) weather_fahrenheit = v8;
  int32_t i32;
  if (nvs_get_i32(h, "cd_def", &i32) == ESP_OK) countdown_default = i32;

  nvs_close(h);
  ESP_LOGI(TAG, "Loaded (tz=%s bright=%d mode=%d)", timezone.c_str(),
           brightness, active_mode);
}

void settings::save() {
  nvs_handle_t h;
  if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;

  nvs_set_str(h, "tz", timezone.c_str());
  nvs_set_u8(h, "time_fmt", (uint8_t)time_format);
  nvs_set_u8(h, "bright", brightness);
  nvs_set_u8(h, "mode", active_mode);
  nvs_set_u8(h, "led_r", led_r);
  nvs_set_u8(h, "led_g", led_g);
  nvs_set_u8(h, "led_b", led_b);
  nvs_set_u8(h, "led_mode", (uint8_t)led_mode);
  nvs_set_u8(h, "sound", sound_enabled ? 1 : 0);
  nvs_set_u8(h, "chime", hourly_chime ? 1 : 0);
  nvs_set_u8(h, "volume", volume);
  nvs_set_str(h, "w_key", weather_api_key.c_str());
  nvs_set_str(h, "w_city", weather_city.c_str());
  nvs_set_str(h, "w_ctry", weather_country.c_str());
  nvs_set_u8(h, "w_fahr", weather_fahrenheit ? 1 : 0);
  nvs_set_i32(h, "cd_def", countdown_default);

  nvs_commit(h);
  nvs_close(h);
  ESP_LOGI(TAG, "Saved");
}
