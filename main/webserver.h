//  SPDX-FileCopyrightText: 2023 Ian Levesque <ian@ianlevesque.org>
//  SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>

using webhook_callback_t = void (*)(const uint8_t *body, size_t length);
using settings_changed_callback_t = void (*)();

void webserver_init(webhook_callback_t webhook_cb,
                    settings_changed_callback_t settings_cb);
