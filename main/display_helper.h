//  SPDX-FileCopyrightText: 2024 PreviousTube Contributors
//  SPDX-License-Identifier: MIT
//
// Shared display helper. Manages the 6 LCD screens as a unified character
// display with split-flap animation. Any app mode can call display_set_text()
// to update what's shown.

#pragma once

#include "drivers/lcds.h"
#include "flap_sequence.h"
#include "flapper.h"
#include "gui.h"
#include <array>
#include <lvgl.h>
#include <memory>
#include <string>

// Font sizes available
enum class DisplayFontSize {
  LARGE,  // oswald_100 - main digit
  MEDIUM, // oswald_60  - AM/PM, smaller items
  SMALL,  // oswald_40  - subscript
};

class display_helper {
public:
  static auto get() -> display_helper &;

  // Initialize LVGL objects on all 6 screens
  void init();

  // Set the text for all 6 displays. Pass up to 6 chars.
  // Characters are displayed with split-flap animation.
  // Use '\0' or ' ' for blank, ':' for colon.
  void set_text(const char text[6], bool animate = true);

  // Set a single display's text
  void set_char(size_t lcd_index, const char *text, bool animate = true);

  // Set the 6th display (index 5) to show two-line text like AM/PM
  void set_dual_line(const char *top, const char *bottom, bool animate = true);

  // Clear the 6th display dual-line text
  void clear_dual_line();

  // Set font size for a display
  void set_font(size_t lcd_index, DisplayFontSize size);

  // Get direct access to flapper for advanced animation
  flapper *get_flapper(size_t index) { return flappers[index]; }

  // Force redraw
  void invalidate_all();

  display_helper(const display_helper &) = delete;
  void operator=(const display_helper &) = delete;

private:
  display_helper() = default;

  bool initialized{false};

  std::array<lv_obj_t *, NUM_LCDS> bg_images{};
  std::array<lv_obj_t *, NUM_LCDS> labels{};
  std::array<flapper *, NUM_LCDS> flappers{};
  std::array<std::unique_ptr<flap_sequence>, NUM_LCDS> flap_sequences{};
  std::array<lv_timer_t *, NUM_LCDS> delayed_timers{};
  std::array<char[8], NUM_LCDS> current_text{}; // what's currently displayed

  // 6th screen dual-line labels
  lv_obj_t *dual_top_label{};
  lv_obj_t *dual_bottom_label{};

  void animate_to(size_t index, const std::string &target, uint32_t delay_ms);
};
