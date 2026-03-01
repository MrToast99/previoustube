//  SPDX-FileCopyrightText: 2024 PreviousTube Contributors
//  SPDX-License-Identifier: MIT
//
// App mode framework. Each display mode (clock, countdown, pomodoro, etc.)
// inherits from app_mode and is managed by app_manager which handles
// switching, touch input routing, and the LVGL timer loop.

#pragma once

#include "drivers/lcds.h"
#include "gui.h"
#include <array>
#include <cstdint>
#include <lvgl.h>
#include <string>
#include <vector>

// Forward
class app_manager;

// ---------------- Display helper ----------------
// Writes a string across the 6 LCD displays. Each LCD shows one character.
// The flapper animation can be triggered per-display.

struct display_char {
  char ch;        // character to show (or '\0' for blank)
  bool changed;   // true if different from what's currently displayed
};

// ---------------- Base class ----------------

class app_mode {
public:
  virtual ~app_mode() = default;

  // Called once when this mode becomes active
  virtual void enter() {}
  // Called once when leaving this mode
  virtual void leave() {}

  // Called every frame (~every second or on demand)
  virtual void update() = 0;

  // Touch input
  virtual void on_left_tap() {}
  virtual void on_middle_tap() {}
  virtual void on_right_tap() {}

  // Long press (held > 1 second)
  virtual void on_left_long() {}
  virtual void on_middle_long() {}
  virtual void on_right_long() {}

  // Name for web UI / debug
  virtual const char *name() const = 0;
};

// ---------------- Mode IDs ----------------

enum class AppModeId : uint8_t {
  CLOCK = 0,
  COUNTDOWN,
  POMODORO,
  SCOREBOARD,
  DATE_DISPLAY,
  TEMPERATURE,
  NUM_MODES // sentinel
};

inline const char *app_mode_name(AppModeId id) {
  switch (id) {
  case AppModeId::CLOCK:        return "clock";
  case AppModeId::COUNTDOWN:    return "countdown";
  case AppModeId::POMODORO:     return "pomodoro";
  case AppModeId::SCOREBOARD:   return "scoreboard";
  case AppModeId::DATE_DISPLAY: return "date";
  case AppModeId::TEMPERATURE:  return "temperature";
  default:                      return "unknown";
  }
}

// ---------------- Manager ----------------

class app_manager {
public:
  static auto get() -> app_manager &;

  void init();
  void register_mode(AppModeId id, app_mode *mode);
  void switch_to(AppModeId id);
  void next_mode();
  void prev_mode();

  AppModeId current_mode_id() const { return current_id; }
  app_mode *current_mode() const { return modes[(int)current_id]; }

  // Route touch events
  void on_left_tap();
  void on_middle_tap();
  void on_right_tap();

  app_manager(const app_manager &) = delete;
  void operator=(const app_manager &) = delete;

private:
  app_manager() = default;

  std::array<app_mode *, (int)AppModeId::NUM_MODES> modes{};
  AppModeId current_id{AppModeId::CLOCK};
};
