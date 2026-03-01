//  SPDX-FileCopyrightText: 2023 Ian Levesque <ian@ianlevesque.org>
//  SPDX-License-Identifier: MIT

#pragma once

#include <string>

using wifi_connected_callback_t = void (*)();

void wifi_init(wifi_connected_callback_t callback);
void wifi_read_credentials_and_connect(const char *filename);

// Captive portal AP mode for provisioning
void wifi_start_ap_provisioning();
bool wifi_is_connected();

// Get current IP as string (empty if not connected)
std::string wifi_get_ip();
