//  SPDX-FileCopyrightText: 2024 PreviousTube Contributors
//  SPDX-License-Identifier: MIT
//
// Speaker driver using I2S with internal DAC output on GPIO25.
// The stock firmware uses an LTK8002D amplifier connected to DAC1 (GPIO25),
// driven via I2S in built-in DAC mode. This approach is confirmed by
// reverse-engineering the original firmware binary which references
// i2s_set_dac_mode and the ESP8266Audio WAV generator library.

#pragma once

#include <cstdint>

void speaker_init();
void speaker_deinit();

// Tone generation
void speaker_play_tone(uint32_t freq_hz, uint32_t duration_ms);
void speaker_stop();

// Pre-built sounds
void speaker_play_click();    // short tactile feedback sound
void speaker_play_chime();    // hourly chime (two-tone)
void speaker_play_startup();  // startup jingle

// Set volume (0-100)
void speaker_set_volume(uint8_t volume);
