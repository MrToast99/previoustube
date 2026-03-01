//  SPDX-FileCopyrightText: 2024 PreviousTube Contributors
//  SPDX-License-Identifier: MIT

#pragma once

#include "app_mode.h"
#include "display_helper.h"
#include "settings.h"
#include <lvgl.h>

// ======================== Clock Mode ========================

class clock_mode : public app_mode {
public:
  void enter() override;
  void leave() override;
  void update() override;
  void on_middle_tap() override; // shuffle
  void on_right_tap() override;  // next mode
  void on_left_tap() override;   // prev mode

  const char *name() const override { return "clock"; }

private:
  lv_timer_t *update_timer{};
  static void timer_cb(lv_timer_t *t);
};

// ======================== Countdown Mode ========================

class countdown_mode : public app_mode {
public:
  void enter() override;
  void leave() override;
  void update() override;
  void on_left_tap() override;   // start/pause
  void on_middle_tap() override; // reset
  void on_right_tap() override;  // adjust time

  const char *name() const override { return "countdown"; }

  // Set countdown from API (seconds)
  void set_seconds(int seconds);

private:
  int total_seconds{300}; // default 5 minutes
  int remaining{300};
  bool running{false};
  lv_timer_t *tick_timer{};
  static void tick_cb(lv_timer_t *t);
  void render();
};

// ======================== Pomodoro Mode ========================

enum class PomodoroPhase { WORK, SHORT_BREAK, LONG_BREAK, IDLE };

class pomodoro_mode : public app_mode {
public:
  void enter() override;
  void leave() override;
  void update() override;
  void on_left_tap() override;   // start/pause
  void on_middle_tap() override; // reset
  void on_right_tap() override;  // skip phase

  const char *name() const override { return "pomodoro"; }

private:
  static constexpr int WORK_SECONDS = 25 * 60;
  static constexpr int SHORT_BREAK_SECONDS = 5 * 60;
  static constexpr int LONG_BREAK_SECONDS = 15 * 60;
  static constexpr int SESSIONS_BEFORE_LONG_BREAK = 4;

  PomodoroPhase phase{PomodoroPhase::IDLE};
  int remaining{WORK_SECONDS};
  int sessions_completed{0};
  bool running{false};
  lv_timer_t *tick_timer{};

  static void tick_cb(lv_timer_t *t);
  void start_phase(PomodoroPhase new_phase);
  void render();
};

// ======================== Scoreboard Mode ========================

class scoreboard_mode : public app_mode {
public:
  void enter() override;
  void leave() override;
  void update() override;
  void on_left_tap() override;   // decrement left score
  void on_middle_tap() override; // reset scores
  void on_right_tap() override;  // increment right score

  const char *name() const override { return "scoreboard"; }

  // API methods
  void set_scores(int left, int right);
  void set_left(int score) { left_score = score; render(); }
  void set_right(int score) { right_score = score; render(); }

private:
  int left_score{0};
  int right_score{0};
  void render();
};

// ======================== Date Display Mode ========================

class date_mode : public app_mode {
public:
  void enter() override;
  void leave() override;
  void update() override;
  void on_middle_tap() override; // toggle format
  void on_right_tap() override;  // next mode

  const char *name() const override { return "date"; }

private:
  lv_timer_t *update_timer{};
  bool show_day_name{false}; // toggle between MM.DD.YY and day name
  static void timer_cb(lv_timer_t *t);
};

// ======================== Temperature Display Mode ========================

class temperature_mode : public app_mode {
public:
  void enter() override;
  void leave() override;
  void update() override;
  void on_middle_tap() override; // toggle C/F
  void on_right_tap() override;  // next mode

  const char *name() const override { return "temperature"; }

  // Set from API/weather fetch
  void set_temperature(float temp_c, const char *condition);
  void set_humidity(int humidity_pct);

private:
  float temperature_c{0};
  int humidity{0};
  bool use_fahrenheit{false};
  bool has_data{false};
  std::string weather_condition{"--"};
  lv_timer_t *update_timer{};
  static void timer_cb(lv_timer_t *t);
  void render();
};
