//  SPDX-FileCopyrightText: 2024 PreviousTube Contributors
//  SPDX-License-Identifier: MIT

#include "modes.h"
#include "app_mode.h"
#include "display_helper.h"
#include "drivers/speaker.h"
#include "settings.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <esp_log.h>

// ======================== Clock Mode ========================

void clock_mode::timer_cb(lv_timer_t *t) {
  static_cast<clock_mode *>(t->user_data)->update();
}

void clock_mode::enter() {
  auto &d = display_helper::get();
  for (int i = 0; i < NUM_LCDS; i++) {
    d.set_font(i, DisplayFontSize::LARGE);
  }
  d.set_font(NUM_LCDS - 1, DisplayFontSize::MEDIUM);

  update_timer = lv_timer_create(timer_cb, 60000, this);
  update();
}

void clock_mode::leave() {
  if (update_timer) {
    lv_timer_del(update_timer);
    update_timer = nullptr;
  }
  display_helper::get().clear_dual_line();
}

void clock_mode::update() {
  time_t now;
  time(&now);
  struct tm ti{};
  localtime_r(&now, &ti);

  auto &s = settings::get();
  bool is_24h = (s.time_format == TimeFormat::HOUR_24);

  char buf[16];
  if (is_24h) {
    strftime(buf, sizeof(buf), "%H:%M", &ti);
  } else {
    strftime(buf, sizeof(buf), "%I:%M%p", &ti);
    if (buf[0] == '0') buf[0] = ' ';
  }

  auto &d = display_helper::get();

  // First 5 chars go to displays 0-4
  char text[6] = {};
  for (int i = 0; i < 5 && buf[i]; i++) {
    text[i] = buf[i];
  }
  text[5] = '\0';
  d.set_text(text, true);

  // AM/PM on 6th display
  if (!is_24h && strlen(buf) > 5) {
    char top[2] = {buf[5], '\0'};
    d.set_dual_line(top, "M", true);
  } else if (is_24h) {
    d.clear_dual_line();
    d.set_char(5, "", false);
  }

  // Hourly chime
  if (s.hourly_chime && s.sound_enabled && ti.tm_min == 0 && ti.tm_sec < 2) {
    speaker_play_chime();
  }

  // Schedule next update precisely
  if (update_timer) {
    int remaining_sec = 60 - ti.tm_sec;
    lv_timer_set_period(update_timer, remaining_sec * 1000);
    lv_timer_reset(update_timer);
  }
}

void clock_mode::on_middle_tap() {
  // Shuffle animation - blank then re-update
  auto &d = display_helper::get();
  for (int i = 0; i < 6; i++) d.set_char(i, "", false);
  d.clear_dual_line();
  update();
}

void clock_mode::on_right_tap() { app_manager::get().next_mode(); }
void clock_mode::on_left_tap() { app_manager::get().prev_mode(); }

// ======================== Countdown Mode ========================

void countdown_mode::tick_cb(lv_timer_t *t) {
  auto *self = static_cast<countdown_mode *>(t->user_data);
  if (self->running && self->remaining > 0) {
    self->remaining--;
    self->render();
    if (self->remaining == 0) {
      self->running = false;
      if (settings::get().sound_enabled) {
        speaker_play_chime();
      }
    }
  }
}

void countdown_mode::enter() {
  tick_timer = lv_timer_create(tick_cb, 1000, this);
  auto &d = display_helper::get();
  d.clear_dual_line();
  for (int i = 0; i < NUM_LCDS; i++) d.set_font(i, DisplayFontSize::LARGE);
  render();
}

void countdown_mode::leave() {
  if (tick_timer) {
    lv_timer_del(tick_timer);
    tick_timer = nullptr;
  }
}

void countdown_mode::update() { render(); }

void countdown_mode::render() {
  int r = remaining;
  int hours = r / 3600;
  int mins = (r % 3600) / 60;
  int secs = r % 60;

  auto &d = display_helper::get();
  char text[16]; // Increased from 7 to prevent format-truncation warning

  if (hours > 0) {
    snprintf(text, sizeof(text), "%02d%02d%02d", hours, mins, secs);
  } else {
    snprintf(text, sizeof(text), "  %02d%02d", mins, secs);
  }
  // Insert colon separators
  char display[6];
  if (hours > 0) {
    display[0] = text[0]; display[1] = text[1];
    display[2] = ':';
    display[3] = text[2]; display[4] = text[3];
    // Show seconds on 6th display dual-line
    char sec_top[16], sec_bot[16]; // Increased buffer
    snprintf(sec_top, sizeof(sec_top), "%d", secs / 10);
    snprintf(sec_bot, sizeof(sec_bot), "%d", secs % 10);
    d.set_dual_line(sec_top, sec_bot, false);
  } else {
    display[0] = text[2]; display[1] = text[3];
    display[2] = ':';
    display[3] = text[4]; display[4] = text[5];
    display[5] = '\0';
    d.clear_dual_line();
    d.set_char(5, running ? "" : "P", false); // P = paused indicator
  }
  d.set_text(display, false);
}

void countdown_mode::on_left_tap() {
  running = !running;
  render();
}

void countdown_mode::on_middle_tap() {
  running = false;
  remaining = total_seconds;
  render();
}

void countdown_mode::on_right_tap() {
  if (!running) {
    // Cycle through preset times: 1, 2, 3, 5, 10, 15, 20, 25, 30, 45, 60 min
    static const int presets[] = {60,   120,  180,  300,  600,
                                  900,  1200, 1500, 1800, 2700, 3600};
    static const int num_presets = sizeof(presets) / sizeof(presets[0]);
    int current_idx = 0;
    for (int i = 0; i < num_presets; i++) {
      if (presets[i] == total_seconds) {
        current_idx = i;
        break;
      }
    }
    current_idx = (current_idx + 1) % num_presets;
    total_seconds = presets[current_idx];
    remaining = total_seconds;
    render();
  } else {
    app_manager::get().next_mode();
  }
}

void countdown_mode::set_seconds(int seconds) {
  total_seconds = seconds;
  remaining = seconds;
  running = false;
  render();
}

// ======================== Pomodoro Mode ========================

void pomodoro_mode::tick_cb(lv_timer_t *t) {
  auto *self = static_cast<pomodoro_mode *>(t->user_data);
  if (self->running && self->remaining > 0) {
    self->remaining--;
    self->render();
    if (self->remaining == 0) {
      self->running = false;
      if (settings::get().sound_enabled) {
        speaker_play_chime();
      }
      // Auto-advance to next phase
      if (self->phase == PomodoroPhase::WORK) {
        self->sessions_completed++;
        if (self->sessions_completed % SESSIONS_BEFORE_LONG_BREAK == 0) {
          self->start_phase(PomodoroPhase::LONG_BREAK);
        } else {
          self->start_phase(PomodoroPhase::SHORT_BREAK);
        }
      } else {
        self->start_phase(PomodoroPhase::WORK);
      }
    }
  }
}

void pomodoro_mode::enter() {
  tick_timer = lv_timer_create(tick_cb, 1000, this);
  auto &d = display_helper::get();
  d.clear_dual_line();
  for (int i = 0; i < NUM_LCDS; i++) d.set_font(i, DisplayFontSize::LARGE);
  if (phase == PomodoroPhase::IDLE) {
    start_phase(PomodoroPhase::WORK);
    running = false; // don't auto-start
  }
  render();
}

void pomodoro_mode::leave() {
  if (tick_timer) {
    lv_timer_del(tick_timer);
    tick_timer = nullptr;
  }
}

void pomodoro_mode::update() { render(); }

void pomodoro_mode::start_phase(PomodoroPhase new_phase) {
  phase = new_phase;
  switch (phase) {
  case PomodoroPhase::WORK:
    remaining = WORK_SECONDS;
    break;
  case PomodoroPhase::SHORT_BREAK:
    remaining = SHORT_BREAK_SECONDS;
    break;
  case PomodoroPhase::LONG_BREAK:
    remaining = LONG_BREAK_SECONDS;
    break;
  default:
    remaining = WORK_SECONDS;
    break;
  }
}

void pomodoro_mode::render() {
  int mins = remaining / 60;
  int secs = remaining % 60;

  auto &d = display_helper::get();
  char text[6];
  text[0] = '0' + mins / 10;
  text[1] = '0' + mins % 10;
  text[2] = ':';
  text[3] = '0' + secs / 10;
  text[4] = '0' + secs % 10;
  text[5] = '\0';

  d.set_text(text, false);

  // Show phase indicator on 6th display
  const char *phase_top = "";
  const char *phase_bot = "";
  switch (phase) {
  case PomodoroPhase::WORK:
    phase_top = "W";
    phase_bot = running ? "" : "P";
    break;
  case PomodoroPhase::SHORT_BREAK:
    phase_top = "S";
    phase_bot = "B";
    break;
  case PomodoroPhase::LONG_BREAK:
    phase_top = "L";
    phase_bot = "B";
    break;
  case PomodoroPhase::IDLE:
    phase_top = "";
    phase_bot = "P";
    break;
  }
  d.set_dual_line(phase_top, phase_bot, false);
}

void pomodoro_mode::on_left_tap() {
  running = !running;
  render();
}

void pomodoro_mode::on_middle_tap() {
  running = false;
  sessions_completed = 0;
  start_phase(PomodoroPhase::WORK);
  render();
}

void pomodoro_mode::on_right_tap() {
  if (!running) {
    // Skip to next phase
    if (phase == PomodoroPhase::WORK) {
      sessions_completed++;
      if (sessions_completed % SESSIONS_BEFORE_LONG_BREAK == 0)
        start_phase(PomodoroPhase::LONG_BREAK);
      else
        start_phase(PomodoroPhase::SHORT_BREAK);
    } else {
      start_phase(PomodoroPhase::WORK);
    }
    render();
  } else {
    app_manager::get().next_mode();
  }
}

// ======================== Scoreboard Mode ========================

void scoreboard_mode::enter() {
  auto &d = display_helper::get();
  d.clear_dual_line();
  for (int i = 0; i < NUM_LCDS; i++) d.set_font(i, DisplayFontSize::LARGE);
  render();
}

void scoreboard_mode::leave() {}

void scoreboard_mode::update() { render(); }

void scoreboard_mode::render() {
  // Format: "LL-RR" or "LL:RR" or " L-R " depending on score sizes
  auto &d = display_helper::get();
  char text[16]; // Increased from 7

  int l = left_score % 1000;  // clamp to 3 digits
  int r = right_score % 1000;

  if (l < 100 && r < 100) {
    // 2-digit scores: " LL:RR" or "0L:0R"
    snprintf(text, sizeof(text), "%2d:%2d ", l, r);
    text[5] = '\0';
    d.clear_dual_line();
  } else {
    // 3-digit scores: "LLL RR" use all 6 displays
    snprintf(text, sizeof(text), "%3d%3d", l, r);
  }

  d.set_text(text, false);
}

void scoreboard_mode::set_scores(int left, int right) {
  left_score = left;
  right_score = right;
  render();
}

void scoreboard_mode::on_left_tap() {
  left_score++;
  if (settings::get().sound_enabled) speaker_play_click();
  render();
}

void scoreboard_mode::on_middle_tap() {
  left_score = 0;
  right_score = 0;
  render();
}

void scoreboard_mode::on_right_tap() {
  right_score++;
  if (settings::get().sound_enabled) speaker_play_click();
  render();
}

// ======================== Date Display Mode ========================

void date_mode::timer_cb(lv_timer_t *t) {
  static_cast<date_mode *>(t->user_data)->update();
}

void date_mode::enter() {
  auto &d = display_helper::get();
  d.clear_dual_line();
  for (int i = 0; i < NUM_LCDS; i++) d.set_font(i, DisplayFontSize::LARGE);
  update_timer = lv_timer_create(timer_cb, 60000, this);
  update();
}

void date_mode::leave() {
  if (update_timer) {
    lv_timer_del(update_timer);
    update_timer = nullptr;
  }
}

void date_mode::update() {
  time_t now;
  time(&now);
  struct tm ti{};
  localtime_r(&now, &ti);

  auto &d = display_helper::get();

  if (!show_day_name) {
    // Show MM.DD.YY
    char text[16]; // Increased from 7
    snprintf(text, sizeof(text), "%02d%02d%02d", ti.tm_mon + 1, ti.tm_mday,
             ti.tm_year % 100);
    char display[6] = {text[0], text[1], text[2], text[3], text[4], text[5]};
    d.set_text(display, true);
    // Show dots as separators using the 3rd and 6th display (visual cue)
  } else {
    // Show day name and day number
    const char *days[] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
    const char *day = days[ti.tm_wday];

    char text[16]; // Increased from 6
    snprintf(text, sizeof(text), "%c%c %2d", day[0], day[1], ti.tm_mday);
    d.set_text(text, true);

    // Show month on 6th display
    char mon_top[16], mon_bot[16]; // Increased buffers
    snprintf(mon_top, sizeof(mon_top), "%d", (ti.tm_mon + 1) / 10);
    snprintf(mon_bot, sizeof(mon_bot), "%d", (ti.tm_mon + 1) % 10);
    if ((ti.tm_mon + 1) < 10) {
      d.set_dual_line("", mon_bot, false);
    } else {
      d.set_dual_line(mon_top, mon_bot, false);
    }
  }
}

void date_mode::on_middle_tap() {
  show_day_name = !show_day_name;
  update();
}

void date_mode::on_right_tap() { app_manager::get().next_mode(); }

// ======================== Temperature Display Mode ========================

void temperature_mode::timer_cb(lv_timer_t *t) {
  static_cast<temperature_mode *>(t->user_data)->update();
}

void temperature_mode::enter() {
  auto &d = display_helper::get();
  d.clear_dual_line();
  for (int i = 0; i < NUM_LCDS; i++) d.set_font(i, DisplayFontSize::LARGE);
  update_timer = lv_timer_create(timer_cb, 300000, this); // 5 min refresh
  render();
}

void temperature_mode::leave() {
  if (update_timer) {
    lv_timer_del(update_timer);
    update_timer = nullptr;
  }
}

void temperature_mode::update() { render(); }

void temperature_mode::render() {
  auto &d = display_helper::get();

  if (!has_data) {
    char text[6] = {'-', '-', ' ', '-', '-', '\0'};
    d.set_text(text, false);
    d.set_dual_line("?", "?", false);
    return;
  }

  float display_temp = use_fahrenheit ? (temperature_c * 9.0f / 5.0f + 32.0f)
                                      : temperature_c;
  int temp_int = (int)(display_temp + 0.5f);
  bool negative = temp_int < 0;
  if (negative) temp_int = -temp_int;

  char text[16]; // Increased from 6
  if (negative) {
    snprintf(text, sizeof(text), "-%3d", temp_int % 1000);
  } else {
    snprintf(text, sizeof(text), " %3d", temp_int % 1000);
  }
  // Pad to 4 chars for displays 0-3
  text[4] = '\0';

  // Show temp on first 4 displays
  char display[6] = {text[0], text[1], text[2], text[3], '\0', '\0'};

  // Show humidity on display 4-5 if available
  if (humidity > 0) {
    char hum[8]; // Increased from 3
    snprintf(hum, sizeof(hum), "%2d", humidity % 100);
    display[4] = hum[0];
    display[5] = '\0';
    d.set_text(display, false);
    // Unit on 6th display
    d.set_dual_line(use_fahrenheit ? "F" : "C", "%", false);
  } else {
    d.set_text(display, false);
    d.set_dual_line(use_fahrenheit ? "F" : "C", "", false);
  }
}

void temperature_mode::set_temperature(float temp_c, const char *condition) {
  temperature_c = temp_c;
  if (condition) weather_condition = condition;
  has_data = true;
  render();
}

void temperature_mode::set_humidity(int h) {
  humidity = h;
  render();
}

void temperature_mode::on_middle_tap() {
  use_fahrenheit = !use_fahrenheit;
  render();
}

void temperature_mode::on_right_tap() { app_manager::get().next_mode(); }