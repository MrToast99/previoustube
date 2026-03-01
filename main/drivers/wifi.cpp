//  SPDX-FileCopyrightText: 2023 Ian Levesque <ian@ianlevesque.org>
//  SPDX-License-Identifier: MIT

#include "wifi.h"

#include "INIReader.h"
#include <cstring>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <string>

#define RETRY_LIMIT UINT_MAX

static const char *TAG = "wifi";
static wifi_connected_callback_t s_wifi_connected_callback = nullptr;
static bool s_connected = false;
static std::string s_ip_str;

static void event_handler(void *, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
  static unsigned int s_retry_num = 0;

  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    s_connected = false;
    s_ip_str.clear();
    if (s_retry_num < RETRY_LIMIT) {
      esp_wifi_connect();
      s_retry_num++;
      ESP_LOGI(TAG, "Retry connect (%u)", s_retry_num);
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    auto *event = static_cast<ip_event_got_ip_t *>(event_data);
    char buf[16];
    esp_ip4addr_ntoa(&event->ip_info.ip, buf, sizeof(buf));
    s_ip_str = std::string(buf);
    s_connected = true;
    s_retry_num = 0;
    ESP_LOGI(TAG, "Got IP: %s", buf);

    if (s_wifi_connected_callback) s_wifi_connected_callback();
  }
}

void wifi_init(wifi_connected_callback_t callback) {
  s_wifi_connected_callback = callback;

  ESP_ERROR_CHECK(esp_netif_init());
  esp_netif_create_default_wifi_sta();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, nullptr, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, nullptr, nullptr));
}

void wifi_read_credentials_and_connect(const char *filename) {
  try {
    auto data = INIReader(std::string(filename));
    auto ssid = data.GetString("", "ssid", "");
    auto psk = data.GetString("", "psk", "");

    if (ssid.empty()) {
      ESP_LOGW(TAG, "Empty SSID in %s", filename);
      return;
    }

    wifi_config_t wifi_config = {};
    strlcpy((char *)wifi_config.sta.ssid, ssid.c_str(),
            sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, psk.c_str(),
            sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = psk.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_PSK;
    wifi_config.sta.pmf_cfg.capable = true;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_LOGI(TAG, "Connecting to '%s'", (char *)wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_start());
  } catch (const std::exception &e) {
    ESP_LOGE(TAG, "Failed to read credentials: %s", e.what());
  }
}

void wifi_start_ap_provisioning() {
  // Create AP named "nextube-ap-XXXX" like stock firmware
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);

  esp_netif_create_default_wifi_ap();

  wifi_config_t ap_config = {};
  snprintf((char *)ap_config.ap.ssid, sizeof(ap_config.ap.ssid),
           "nextube-ap-%02X%02X", mac[4], mac[5]);
  ap_config.ap.ssid_len = strlen((char *)ap_config.ap.ssid);
  ap_config.ap.channel = 1;
  ap_config.ap.authmode = WIFI_AUTH_OPEN;
  ap_config.ap.max_connection = 4;

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "AP started: %s", (char *)ap_config.ap.ssid);
}

bool wifi_is_connected() { return s_connected; }
std::string wifi_get_ip() { return s_ip_str; }
