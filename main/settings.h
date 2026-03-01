//  SPDX-FileCopyrightText: 2024 PreviousTube Contributors
//  SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <string>

enum class TimeFormat : uint8_t { HOUR_12 = 0, HOUR_24 = 1 };
enum class LedMode : uint8_t { STATIC = 0, BREATHE = 1, OFF = 2 };

struct settings {
  static auto get() -> settings & {
    static settings instance;
    return instance;
  }

  void load();
  void save();

  // Timezone
  std::string timezone{"EST5EDT,M3.2.0,M11.1.0"};

  // Display
  TimeFormat time_format{TimeFormat::HOUR_12};
  uint8_t brightness{255};
  uint8_t active_mode{0}; // AppModeId persisted

  // LEDs
  uint8_t led_r{228}, led_g{112}, led_b{37};
  LedMode led_mode{LedMode::STATIC};

  // Sound
  bool sound_enabled{true};
  bool hourly_chime{false};
  uint8_t volume{80};

  // Weather
  std::string weather_api_key;
  std::string weather_city{"New York"};
  std::string weather_country{"US"};
  bool weather_fahrenheit{true};

  // Countdown default (seconds)
  int countdown_default{300};

  settings(const settings &) = delete;
  void operator=(const settings &) = delete;

private:
  settings() { load(); }
};
