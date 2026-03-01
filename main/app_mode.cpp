//  SPDX-FileCopyrightText: 2024 PreviousTube Contributors
//  SPDX-License-Identifier: MIT

#include "app_mode.h"
#include <esp_log.h>

static const char *TAG = "app_mgr";

app_manager &app_manager::get() {
  static app_manager instance;
  return instance;
}

void app_manager::init() {
  ESP_LOGI(TAG, "App manager initialized");
}

void app_manager::register_mode(AppModeId id, app_mode *mode) {
  modes[(int)id] = mode;
  ESP_LOGI(TAG, "Registered mode: %s", app_mode_name(id));
}

void app_manager::switch_to(AppModeId id) {
  if (modes[(int)id] == nullptr) {
    ESP_LOGW(TAG, "Mode %s not registered", app_mode_name(id));
    return;
  }

  if (modes[(int)current_id] != nullptr) {
    modes[(int)current_id]->leave();
  }

  current_id = id;
  ESP_LOGI(TAG, "Switching to mode: %s", app_mode_name(id));

  modes[(int)current_id]->enter();
  modes[(int)current_id]->update();
}

void app_manager::next_mode() {
  int next = ((int)current_id + 1) % (int)AppModeId::NUM_MODES;
  // Skip unregistered modes
  for (int i = 0; i < (int)AppModeId::NUM_MODES; i++) {
    if (modes[next] != nullptr) break;
    next = (next + 1) % (int)AppModeId::NUM_MODES;
  }
  switch_to((AppModeId)next);
}

void app_manager::prev_mode() {
  int prev = ((int)current_id - 1 + (int)AppModeId::NUM_MODES) %
             (int)AppModeId::NUM_MODES;
  for (int i = 0; i < (int)AppModeId::NUM_MODES; i++) {
    if (modes[prev] != nullptr) break;
    prev = (prev - 1 + (int)AppModeId::NUM_MODES) % (int)AppModeId::NUM_MODES;
  }
  switch_to((AppModeId)prev);
}

void app_manager::on_left_tap() {
  if (modes[(int)current_id]) modes[(int)current_id]->on_left_tap();
}
void app_manager::on_middle_tap() {
  if (modes[(int)current_id]) modes[(int)current_id]->on_middle_tap();
}
void app_manager::on_right_tap() {
  if (modes[(int)current_id]) modes[(int)current_id]->on_right_tap();
}
