//  SPDX-FileCopyrightText: 2024 PreviousTube Contributors
//  SPDX-License-Identifier: MIT

#include "display_helper.h"
#include <algorithm>
#include <cstring>
#include <esp_log.h>

static const char *TAG = "display";

static const lv_color_t TEXT_COLOR = lv_color_hex(0xFCF9D9);

constexpr auto FLIP_SPACING_MS = 700;

static const std::vector<std::string> digits_loop = {
    "", ":", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
static const std::vector<std::string> divider_loop = {"", ":"};

display_helper &display_helper::get() {
  static display_helper instance;
  return instance;
}

void display_helper::init() {
  if (initialized) return;

  for (int i = 0; i < NUM_LCDS; i++) {
    memset(current_text[i], 0, sizeof(current_text[i]));

    lv_disp_set_default(gui_get_display(i));
    lv_obj_t *screen = lv_scr_act();

    lv_obj_t *bg = lv_img_create(screen);
    bg_images[i] = bg;
    lv_img_set_src(bg, "S:/spiffs/split_flap.png");
    lv_obj_set_pos(bg, 0, 0);

    flappers[i] = new flapper(bg);

    lv_obj_set_style_text_color(screen, TEXT_COLOR, LV_PART_MAIN);
    lv_obj_set_style_text_font(screen, &oswald_100, LV_PART_MAIN);

    lv_obj_t *lbl = lv_label_create(bg);
    lv_label_set_text_static(lbl, "");
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -7);
    labels[i] = lbl;

    // Divider image
    lv_obj_t *divider = lv_img_create(bg);
    lv_obj_set_pos(divider, 8, 75);
    lv_img_set_src(divider, "S:/spiffs/split_flap_divider.png");

    delayed_timers[i] = nullptr;
  }

  // Set up 6th display for dual-line mode (AM/PM etc)
  {
    lv_disp_set_default(gui_get_display(NUM_LCDS - 1));
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_text_font(screen, &oswald_60, LV_PART_MAIN);

    // Re-align the main label for the 6th display
    lv_obj_align(labels[NUM_LCDS - 1], LV_ALIGN_CENTER, 0, -7);

    dual_top_label = lv_label_create(bg_images[NUM_LCDS - 1]);
    lv_obj_set_style_text_align(dual_top_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(dual_top_label, LV_ALIGN_CENTER, 0, -44);
    lv_label_set_text_static(dual_top_label, "");
    lv_obj_add_flag(dual_top_label, LV_OBJ_FLAG_HIDDEN);

    dual_bottom_label = lv_label_create(bg_images[NUM_LCDS - 1]);
    lv_obj_set_style_text_align(dual_bottom_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(dual_bottom_label, LV_ALIGN_CENTER, 0, 33);
    lv_label_set_text_static(dual_bottom_label, "");
    lv_obj_add_flag(dual_bottom_label, LV_OBJ_FLAG_HIDDEN);
  }

  initialized = true;
  ESP_LOGI(TAG, "Display helper initialized");
}

void display_helper::set_text(const char text[6], bool animate) {
  uint32_t delay = 0;
  for (int i = 0; i < NUM_LCDS; i++) {
    char ch = (i < 6 && text[i] != '\0') ? text[i] : '\0';
    char buf[2] = {ch == ' ' ? '\0' : ch, '\0'};
    std::string target(buf);

    if (target != std::string(current_text[i])) {
      if (animate) {
        animate_to(i, target, delay);
        delay += FLIP_SPACING_MS;
      } else {
        // Immediate update
        lv_label_set_text(labels[i], target.c_str());
        strncpy(current_text[i], target.c_str(), sizeof(current_text[i]) - 1);
      }
    }
  }
}

void display_helper::set_char(size_t index, const char *text, bool animate) {
  if (index >= NUM_LCDS) return;
  std::string target(text ? text : "");

  if (target != std::string(current_text[index])) {
    if (animate) {
      animate_to(index, target, 0);
    } else {
      lv_label_set_text(labels[index], target.c_str());
      strncpy(current_text[index], target.c_str(),
              sizeof(current_text[index]) - 1);
    }
  }
}

void display_helper::set_dual_line(const char *top, const char *bottom,
                                   bool animate) {
  // Hide the main label on the 6th display
  lv_obj_add_flag(labels[NUM_LCDS - 1], LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(dual_top_label, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(dual_bottom_label, LV_OBJ_FLAG_HIDDEN);

  // Set 6th display font to medium for dual-line
  lv_disp_set_default(gui_get_display(NUM_LCDS - 1));
  lv_obj_t *screen = lv_scr_act();
  lv_obj_set_style_text_font(screen, &oswald_60, LV_PART_MAIN);

  const char *existing_top = lv_label_get_text(dual_top_label);
  const char *existing_bottom = lv_label_get_text(dual_bottom_label);

  bool top_changed = strcmp(existing_top, top) != 0;
  bool bottom_changed = strcmp(existing_bottom, bottom) != 0;

  if (top_changed || bottom_changed) {
    if (animate) {
      flapper *f = flappers[NUM_LCDS - 1];
      f->before();
    }

    lv_label_set_text(dual_top_label, top);
    lv_label_set_text(dual_bottom_label, bottom);

    if (animate) {
      flapper *f = flappers[NUM_LCDS - 1];
      f->after();
      f->start(true);
    }

    strncpy(current_text[NUM_LCDS - 1], top,
            sizeof(current_text[NUM_LCDS - 1]) - 1);
  }
}

void display_helper::clear_dual_line() {
  lv_obj_add_flag(dual_top_label, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(dual_bottom_label, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(labels[NUM_LCDS - 1], LV_OBJ_FLAG_HIDDEN);

  lv_disp_set_default(gui_get_display(NUM_LCDS - 1));
  lv_obj_t *screen = lv_scr_act();
  lv_obj_set_style_text_font(screen, &oswald_100, LV_PART_MAIN);
}

void display_helper::set_font(size_t index, DisplayFontSize size) {
  if (index >= NUM_LCDS) return;

  lv_disp_set_default(gui_get_display(index));
  lv_obj_t *screen = lv_scr_act();

  switch (size) {
  case DisplayFontSize::LARGE:
    lv_obj_set_style_text_font(screen, &oswald_100, LV_PART_MAIN);
    break;
  case DisplayFontSize::MEDIUM:
    lv_obj_set_style_text_font(screen, &oswald_60, LV_PART_MAIN);
    break;
  case DisplayFontSize::SMALL:
    lv_obj_set_style_text_font(screen, &oswald_40, LV_PART_MAIN);
    break;
  }
}

void display_helper::invalidate_all() { gui_invalidate_all_screens(); }

void display_helper::animate_to(size_t index, const std::string &target,
                                uint32_t delay_ms) {
  auto existing_string = std::string(current_text[index]);
  auto &character_loop = (target == ":") ? divider_loop : digits_loop;

  auto start_iter = std::find(character_loop.cbegin(), character_loop.cend(),
                              existing_string);
  auto end_iter =
      std::find(character_loop.cbegin(), character_loop.cend(), target);

  // If the character isn't in the loop, just set it directly
  if (start_iter == character_loop.cend() ||
      end_iter == character_loop.cend()) {
    lv_label_set_text(labels[index], target.c_str());
    strncpy(current_text[index], target.c_str(),
            sizeof(current_text[index]) - 1);
    return;
  }

  std::vector<std::string> values;
  auto iter = start_iter;
  while (true) {
    iter++;
    if (iter == character_loop.cend()) {
      iter = character_loop.cbegin();
    }
    values.push_back(*iter);
    if (iter == end_iter) break;
  }

  // Cancel any existing animation
  if (delayed_timers[index] != nullptr) {
    lv_timer_del(delayed_timers[index]);
    delayed_timers[index] = nullptr;
  }

  // Update current_text tracking when animation completes
  strncpy(current_text[index], target.c_str(),
          sizeof(current_text[index]) - 1);

  size_t idx = index;
  flap_sequences[index] = std::make_unique<flap_sequence>(
      flappers[index],
      [this, idx](const std::string &value) {
        lv_label_set_text(labels[idx], value.c_str());
      },
      values);

  if (delay_ms == 0) {
    flap_sequences[index]->start();
  } else {
    struct timer_data {
      display_helper *self;
      size_t index;
    };
    auto *td = new timer_data{this, index};
    auto *timer = lv_timer_create(
        [](lv_timer_t *t) {
          auto *d = static_cast<timer_data *>(t->user_data);
          if (d->self->flap_sequences[d->index]) {
            d->self->flap_sequences[d->index]->start();
          }
          d->self->delayed_timers[d->index] = nullptr;
          delete d;
        },
        delay_ms, td);
    lv_timer_set_repeat_count(timer, 1);
    delayed_timers[index] = timer;
  }
}
