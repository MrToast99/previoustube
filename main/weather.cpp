//  SPDX-FileCopyrightText: 2024 PreviousTube Contributors
//  SPDX-License-Identifier: MIT
//
// Background weather fetcher. Hits OpenWeatherMap every 10 minutes.
// Stock firmware used: http://api.openweathermap.org/data/2.5/weather?q=

#include "weather.h"
#include "modes.h"
#include "settings.h"

#include <cJSON.h>
#include <cstring>
#include <esp_http_client.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "weather";
static TaskHandle_t weather_task_handle = nullptr;
static volatile bool fetch_requested = false;

// Global pointer set by main.cpp so we can push data to temperature mode
extern temperature_mode *g_temperature_mode;

static char response_buf[2048];
static int response_len = 0;

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
  switch (evt->event_id) {
  case HTTP_EVENT_ON_DATA:
    if (response_len + evt->data_len < (int)sizeof(response_buf) - 1) {
      memcpy(response_buf + response_len, evt->data, evt->data_len);
      response_len += evt->data_len;
    }
    break;
  default:
    break;
  }
  return ESP_OK;
}

static void do_fetch() {
  auto &s = settings::get();
  if (s.weather_api_key.empty()) {
    ESP_LOGD(TAG, "No API key configured, skipping weather fetch");
    return;
  }

  // Build URL: http://api.openweathermap.org/data/2.5/weather?q=City,CC&appid=KEY&units=metric
  char url[256];
  snprintf(url, sizeof(url),
           "http://api.openweathermap.org/data/2.5/weather?q=%s,%s&appid=%s&units=metric",
           s.weather_city.c_str(), s.weather_country.c_str(),
           s.weather_api_key.c_str());

  ESP_LOGI(TAG, "Fetching weather for %s,%s", s.weather_city.c_str(),
           s.weather_country.c_str());

  response_len = 0;
  memset(response_buf, 0, sizeof(response_buf));

  esp_http_client_config_t config;
  memset(&config, 0, sizeof(config));
  config.url = url;
  config.event_handler = http_event_handler;
  config.timeout_ms = 10000;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  esp_err_t err = esp_http_client_perform(client);

  if (err == ESP_OK && response_len > 0) {
    response_buf[response_len] = '\0';

    cJSON *root = cJSON_Parse(response_buf);
    if (root) {
      // Parse temperature from main.temp
      cJSON *main_obj = cJSON_GetObjectItem(root, "main");
      cJSON *weather_arr = cJSON_GetObjectItem(root, "weather");

      float temp_c = 0;
      int humidity = 0;
      const char *condition = "";

      if (main_obj) {
        cJSON *temp = cJSON_GetObjectItem(main_obj, "temp");
        if (temp) temp_c = (float)temp->valuedouble;
        cJSON *hum = cJSON_GetObjectItem(main_obj, "humidity");
        if (hum) humidity = hum->valueint;
      }
      if (weather_arr && cJSON_IsArray(weather_arr)) {
        cJSON *first = cJSON_GetArrayItem(weather_arr, 0);
        if (first) {
          cJSON *desc = cJSON_GetObjectItem(first, "main");
          if (desc && cJSON_IsString(desc)) condition = desc->valuestring;
        }
      }

      ESP_LOGI(TAG, "Weather: %.1f°C, %d%% humidity, %s", temp_c, humidity,
               condition);

      // Push to temperature mode
      if (g_temperature_mode) {
        g_temperature_mode->set_temperature(temp_c, condition);
        g_temperature_mode->set_humidity(humidity);
      }

      cJSON_Delete(root);
    } else {
      ESP_LOGW(TAG, "Failed to parse weather JSON");
    }
  } else {
    ESP_LOGW(TAG, "Weather fetch failed: %s", esp_err_to_name(err));
  }

  esp_http_client_cleanup(client);
}

static void weather_task(void *) {
  // Wait for WiFi to be up before first fetch
  vTaskDelay(pdMS_TO_TICKS(15000));

  while (true) {
    do_fetch();

    // Wait 10 minutes or until triggered
    for (int i = 0; i < 600; i++) {
      if (fetch_requested) {
        fetch_requested = false;
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }
}

void weather_init() {
  xTaskCreate(weather_task, "weather", 8192, nullptr, 2, &weather_task_handle);
}

void weather_fetch_now() { fetch_requested = true; }