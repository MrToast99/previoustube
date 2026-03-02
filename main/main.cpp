//  SPDX-FileCopyrightText: 2023 Ian Levesque <ian@ianlevesque.org>
//  SPDX-License-Identifier: MIT

#include <cJSON.h>
#include <cstdlib>
#include <cstring>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif_sntp.h>
#include <esp_psram.h>
#include <esp_spiffs.h>
#include <nvs_flash.h>
#include <sys/stat.h>

#include "app_mode.h"
#include "display_helper.h"
#include "drivers/lcds.h"
#include "drivers/leds.h"
#include "drivers/speaker.h"
#include "drivers/touchpads.h"
#include "drivers/wifi.h"
#include "gui.h"
#include "led_manager.h"
#include "modes.h"
#include "rtc.h"
#include "settings.h"
#include "weather.h"
#include "webserver.h"

#define SPIFFS_MOUNTPOINT_NO_SLASH "/spiffs"
#define SPIFFS_MOUNTPOINT SPIFFS_MOUNTPOINT_NO_SLASH "/"

static const auto TAG = "previoustube";
static const auto SNTP_SERVER = "pool.ntp.org";

ESP_EVENT_DECLARE_BASE(DISPATCH_EVENTS);
enum {
  DISPATCH_EVENT_TIME_CHANGED,
  DISPATCH_EVENT_RTC_TIME_LOADED,
};

// ─── Global mode instances (extern'd by webserver & weather) ──

static clock_mode s_clock_mode;
static countdown_mode s_countdown_mode;
static pomodoro_mode s_pomodoro_mode;
static scoreboard_mode s_scoreboard_mode;
static date_mode s_date_mode;
static temperature_mode s_temperature_mode;

scoreboard_mode *g_scoreboard_mode = &s_scoreboard_mode;
countdown_mode *g_countdown_mode = &s_countdown_mode;
temperature_mode *g_temperature_mode = &s_temperature_mode;

// ─── LED blink infrastructure ───────────────────────────────

bool blinks_enabled = true;

struct blink_user_data {
  size_t led_index;
  uint8_t color_r, color_g, color_b;
  bool _state = false;
};

static void clear_override_callback(lv_timer_t *timer) {
  led_manager::get().clear_override();
  delete static_cast<blink_user_data *>(timer->user_data);
}

static void blink_callback(lv_timer_t *timer) {
  auto *ud = static_cast<blink_user_data *>(timer->user_data);
  if (!ud->_state)
    led_manager::get().override_rgb(ud->led_index, ud->color_r, ud->color_g,
                                    ud->color_b);
  else
    led_manager::get().clear_override();
  ud->_state = !ud->_state;
}

static void blink_led(size_t led_index, uint8_t r, uint8_t g, uint8_t b,
                       int repetitions, int period) {
  auto *ud = new blink_user_data{
      .led_index = led_index, .color_r = r, .color_g = g, .color_b = b};
  int toggles = repetitions * 2;
  int interval = period / toggles;
  int cleanup = interval * toggles + 200;

  auto *bt = lv_timer_create(blink_callback, interval, ud);
  lv_timer_set_repeat_count(bt, toggles);
  lv_timer_ready(bt);

  auto *ct = lv_timer_create(clear_override_callback, cleanup, ud);
  lv_timer_set_repeat_count(ct, 1);
}

// ─── Apply settings to hardware ─────────────────────────────

static void apply_settings() {
  auto &s = settings::get();
  lcds_set_brightness(s.brightness);

  auto &leds = led_manager::get();
  if (s.led_mode == LedMode::OFF) {
    leds.off();
  } else {
    for (int i = 0; i < NUM_LCDS; i++)
      leds.set_rgb(i, s.led_r, s.led_g, s.led_b);
  }

  setenv("TZ", s.timezone.c_str(), 1);
  tzset();

  speaker_set_volume(s.volume);
}

static void on_settings_changed() {
  apply_settings();
  // Refresh current mode display
  auto *mode = app_manager::get().current_mode();
  if (mode) mode->update();
}

// ─── Brightness cycling ─────────────────────────────────────

static uint8_t brightness_idx = 3;
static const uint8_t BRIGHT_LEVELS[] = {25, 64, 128, 192, 255};
static constexpr size_t NUM_LEVELS = sizeof(BRIGHT_LEVELS) / sizeof(BRIGHT_LEVELS[0]);

static void cycle_brightness() {
  brightness_idx = (brightness_idx + 1) % NUM_LEVELS;
  auto &s = settings::get();
  s.brightness = BRIGHT_LEVELS[brightness_idx];
  lcds_set_brightness(s.brightness);
  blink_led(0, 0, 0, 255, brightness_idx + 1, 1000);
}

// ─── Power state toggle ─────────────────────────────────────

static void toggle_power_state() {
  static int state = 0;
  auto &s = settings::get();
  auto &leds = led_manager::get();
  switch (state) {
  case 0:
    leds.off();
    lcds_on();
    break;
  case 1:
    leds.off();
    lcds_off();
    break;
  case 2:
    for (int i = 0; i < NUM_LCDS; i++)
      leds.set_rgb(i, s.led_r, s.led_g, s.led_b);
    lcds_on();
    break;
  }
  state = (state + 1) % 3;
}

// ─── Touch handling ─────────────────────────────────────────

static void button_tapped(touchpad_button_t button) {
  auto &s = settings::get();
  if (s.sound_enabled) speaker_play_click();

  auto &mgr = app_manager::get();

  switch (button) {
  case TOUCHPAD_LEFT_BUTTON:
    // Long-press left = power toggle, short = mode-specific or prev mode
    mgr.on_left_tap();
    break;
  case TOUCHPAD_MIDDLE_BUTTON:
    mgr.on_middle_tap();
    break;
  case TOUCHPAD_RIGHT_BUTTON:
    mgr.on_right_tap();
    break;
  }
}

// ─── Webhook handler ────────────────────────────────────────

static void webhook_handler(const uint8_t *buffer, size_t length) {
  if (!blinks_enabled) return;

  int period = 2500, repetitions = 10;
  uint8_t cr = 0xFF, cg = 0xFF, cb = 0x00;
  size_t led = 5;

  char *json_str = new char[length + 1];
  memcpy(json_str, buffer, length);
  json_str[length] = '\0';

  cJSON *json = cJSON_Parse(json_str);
  if (json) {
    cJSON *color = cJSON_GetObjectItem(json, "color");
    if (color && cJSON_IsArray(color) && cJSON_GetArraySize(color) == 3) {
      cr = (uint8_t)cJSON_GetArrayItem(color, 0)->valueint;
      cg = (uint8_t)cJSON_GetArrayItem(color, 1)->valueint;
      cb = (uint8_t)cJSON_GetArrayItem(color, 2)->valueint;
    }
    cJSON *it;
    if ((it = cJSON_GetObjectItem(json, "led")) && cJSON_IsNumber(it))
      led = (size_t)it->valueint;
    if ((it = cJSON_GetObjectItem(json, "repeat")) && cJSON_IsNumber(it))
      repetitions = it->valueint;
    if ((it = cJSON_GetObjectItem(json, "period")) && cJSON_IsNumber(it))
      period = it->valueint;
    cJSON_Delete(json);
  }
  delete[] json_str;

  if (led >= NUM_LEDS) led = NUM_LEDS - 1;
  blink_led(led, cr, cg, cb, repetitions, period);
}

// ─── Time events ────────────────────────────────────────────

static void ntp_changed_time(struct timeval *tv) {
  ESP_LOGI(TAG, "NTP time set: %lld", tv->tv_sec);
  rtc_persist();
  auto *mode = app_manager::get().current_mode();
  if (mode) mode->update();
}

static void rtc_loaded_time(struct timeval *) {
  auto *mode = app_manager::get().current_mode();
  if (mode) mode->update();
}

static void dispatch_event_handler(void *, esp_event_base_t, int32_t id,
                                   void *event_data) {
  switch (id) {
  case DISPATCH_EVENT_TIME_CHANGED:
    ntp_changed_time(static_cast<struct timeval *>(event_data));
    break;
  case DISPATCH_EVENT_RTC_TIME_LOADED:
    rtc_loaded_time(static_cast<struct timeval *>(event_data));
    break;
  }
}

// ─── SNTP / WiFi ────────────────────────────────────────────

static void sntp_init() {
  esp_sntp_config_t config = {
      .smooth_sync = false,
      .server_from_dhcp = false,
      .wait_for_sync = true,
      .start = true,
      .sync_cb =
          [](struct timeval *tv) IRAM_ATTR {
            esp_event_post(DISPATCH_EVENTS, DISPATCH_EVENT_TIME_CHANGED, tv,
                           sizeof(struct timeval), portMAX_DELAY);
          },
      .renew_servers_after_new_IP = true,
      .ip_event_to_renew = IP_EVENT_STA_GOT_IP,
      .index_of_first_server = 0,
      .num_of_servers = 1,
      .servers = {SNTP_SERVER},
  };
  esp_netif_sntp_init(&config);
}

static void on_wifi_connected() {
  ESP_LOGI(TAG, "Connected to WiFi");
  sntp_init();
  weather_fetch_now();
}

// ─── Init helpers ───────────────────────────────────────────

static void nvs_init() {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
}

static void spiffs_init() {
  esp_vfs_spiffs_conf_t config = {
      .base_path = SPIFFS_MOUNTPOINT_NO_SLASH,
      .partition_label = nullptr,
      .max_files = 5,
      .format_if_mount_failed = true,
  };
  esp_err_t err = esp_vfs_spiffs_register(&config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(err));
  }
}

// ─── Main ───────────────────────────────────────────────────

extern "C" void app_main() {
  ESP_LOGI(TAG, "Starting PreviousTube...");
  ESP_LOGI(TAG, "PSRAM: %u bytes", (unsigned)esp_psram_get_size());

  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_log_level_set("gpio", ESP_LOG_WARN);
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      DISPATCH_EVENTS, ESP_EVENT_ANY_ID, dispatch_event_handler, nullptr,
      nullptr));

  spiffs_init();
  nvs_init();

  // Settings loaded in constructor via NVS
  auto &s = settings::get();

  // Hardware init
  leds_init();
  leds_off();
  lcds_init();
  speaker_init();

  bool got_time = rtc_init();

  wifi_init(on_wifi_connected);
  gui_init();

  // Display helper (sets up LVGL objects on all 6 screens)
  display_helper::get().init();

  touchpads_init(button_tapped, nullptr);

  // Register all modes
  auto &mgr = app_manager::get();
  mgr.init();
  mgr.register_mode(AppModeId::CLOCK, &s_clock_mode);
  mgr.register_mode(AppModeId::COUNTDOWN, &s_countdown_mode);
  mgr.register_mode(AppModeId::POMODORO, &s_pomodoro_mode);
  mgr.register_mode(AppModeId::SCOREBOARD, &s_scoreboard_mode);
  mgr.register_mode(AppModeId::DATE_DISPLAY, &s_date_mode);
  mgr.register_mode(AppModeId::TEMPERATURE, &s_temperature_mode);

  // Apply all settings to hardware
  apply_settings();

  // Start on saved mode (default: CLOCK)
  AppModeId start_mode = (AppModeId)s.active_mode;
  if (start_mode >= AppModeId::NUM_MODES) start_mode = AppModeId::CLOCK;
  mgr.switch_to(start_mode);

  // LEDs on
  if (s.led_mode != LedMode::OFF) {
    auto &leds = led_manager::get();
    for (int i = 0; i < NUM_LCDS; i++)
      leds.set_rgb(i, s.led_r, s.led_g, s.led_b);
  }

  // Web server - start BEFORE wifi connect so AP mode has config UI
  webserver_init(webhook_handler, on_settings_changed);

  // Weather background task
  weather_init();

  // Startup sound
  if (s.sound_enabled) speaker_play_startup();

  // WiFi connect - or start AP if no credentials
  struct stat st {};
  if (stat(SPIFFS_MOUNTPOINT "wifi.txt", &st) == 0) {
    wifi_read_credentials_and_connect(SPIFFS_MOUNTPOINT "wifi.txt");
  } else {
    ESP_LOGW(TAG, "No wifi.txt found, starting AP provisioning mode");
    wifi_start_ap_provisioning();
  }

  // If RTC had a valid time, kick off display immediately
  if (got_time) {
    struct timeval tv {};
    gettimeofday(&tv, nullptr);
    esp_event_post(DISPATCH_EVENTS, DISPATCH_EVENT_RTC_TIME_LOADED, &tv,
                   sizeof(struct timeval), portMAX_DELAY);
  }
}

ESP_EVENT_DEFINE_BASE(DISPATCH_EVENTS);
