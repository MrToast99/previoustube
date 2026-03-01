# PreviousTube Firmware

Alternative open-source firmware for the [Rotrics Nextube](https://rotrics.com/products/nextube) nixie-style clock. Built with ESP-IDF and LVGL.

## Features

**Display Modes** — Cycle with left/right touch, control with middle touch:

| Mode | Description | Controls |
|------|-------------|----------|
| Clock | 12h/24h time with split-flap animation | Mid=shuffle, R=next mode |
| Countdown | Configurable countdown timer (1-60 min presets) | L=start/pause, Mid=reset, R=adjust time |
| Pomodoro | 25/5/15 minute work/break cycles | L=start/pause, Mid=reset, R=skip phase |
| Scoreboard | Two-team score display (0-999) | L=+left, Mid=reset, R=+right |
| Date | Date display (MM.DD.YY / day name) | Mid=toggle format, R=next mode |
| Temperature | Weather temp + humidity (requires API key) | Mid=toggle C/F, R=next mode |

**Hardware Support:**

| Component | Chip | Status | Notes |
|-----------|------|--------|-------|
| 6x LCD | ST7735 80x160 | Working | SPI with PWM brightness (25kHz fade) |
| LEDs | WS2812 x6 | Working | RGB under each tube |
| Touch pads | 3x capacitive | Working | Left/middle/right with click feedback |
| Speaker | LTK8002D amp | Working | I2S internal DAC (GPIO25), tone synth |
| RTC | PCF8563 | Working | Battery-backed time |
| WiFi | ESP32 | Working | STA + AP provisioning mode |
| PSRAM | 8MB | Working | SPIRAM for image buffers |

**Additional Features:**
- Web configuration UI with tabbed settings interface
- REST API for all modes and settings
- OTA firmware updates via web UI
- Persistent settings (NVS flash)
- OpenWeatherMap integration
- Webhook LED notifications (Home Assistant compatible)
- Hourly chime with volume control
- Startup jingle
- Captive portal WiFi provisioning
- SNTP time sync with timezone support

## Web UI

Connect to the device IP in a browser. The interface has tabs for:
- **Display** — Mode switching, scoreboard/countdown control, brightness, LED color
- **Time** — Timezone selection (15+ zones), 12h/24h format, hourly chime
- **Sound** — Enable/disable, volume slider
- **Weather** — OpenWeatherMap API key, city, country, C/F preference
- **System** — IP, uptime, heap, firmware version, OTA update, WiFi config

## REST API

All endpoints accept/return JSON.

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/ping` | GET | Health check |
| `/api/system` | GET | IP, uptime, heap, version |
| `/api/settings` | GET | All settings as JSON |
| `/api/settings` | POST | Update settings |
| `/api/mode` | POST | Switch display mode `{"mode": 0-5}` |
| `/api/scoreboard` | POST | Set scores `{"left": N, "right": N}` |
| `/api/countdown` | POST | Set timer `{"seconds": N}` |
| `/api/weather/fetch` | POST | Trigger weather refresh |
| `/api/firmwareVersion` | GET | Firmware version + IDF version |
| `/api/update_firmware` | POST | OTA update (binary body) |
| `/api/wifi` | POST | Set WiFi creds `{"ssid": "...", "psk": "..."}` |
| `/api/wifi/ap` | POST | Start AP provisioning mode |
| `/api/reset` | POST | Reboot device |
| `/webhook` | POST | LED notification blink |

### Webhook Format

```json
{
  "color": [255, 0, 0],
  "led": 5,
  "repeat": 10,
  "period": 2500
}
```

### Settings JSON

```json
{
  "timezone": "EST5EDT,M3.2.0,M11.1.0",
  "time_format": 0,
  "brightness": 255,
  "active_mode": 0,
  "led_r": 228, "led_g": 112, "led_b": 37,
  "sound_enabled": true,
  "hourly_chime": false,
  "volume": 80,
  "weather_api_key": "",
  "weather_city": "New York",
  "weather_country": "US",
  "weather_fahrenheit": true
}
```

## WiFi Setup

1. **File method:** Create `spiffs/wifi.txt`:
   ```ini
   ssid = YourNetwork
   psk = YourPassword
   ```

2. **AP provisioning:** If no WiFi configured, access the web UI at `http://10.10.10.1` on the `nextube-ap-XXXX` network and enter credentials.

3. **Web UI:** Use the System tab to change WiFi credentials (triggers reboot).

## Building

Requires [ESP-IDF v5.1+](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/).

```bash
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## OTA Updates

Upload a `.bin` file through the web UI System tab, or via API:

```bash
curl -X POST --data-binary @firmware.bin http://DEVICE_IP/api/update_firmware
```

## Architecture

```
main.cpp            - App entry, hardware init, touch routing
app_mode.h/cpp      - Mode manager framework
modes.h/cpp         - All 6 display modes
display_helper.h/cpp - Shared 6-LCD character display with animation
settings.h/cpp      - NVS-persisted configuration
webserver.h/cpp     - REST API + web config UI + OTA
weather.h/cpp       - OpenWeatherMap background fetcher
drivers/
  lcds.h/cpp        - ST7735 SPI + PWM backlight
  leds.h/cpp        - WS2812 LED strip
  speaker.h/cpp     - I2S internal DAC tone synthesis
  touchpads.h/cpp   - Capacitive touch input
  wifi.h/cpp        - WiFi STA + AP provisioning
```

## Partition Table

Uses custom OTA-capable partition layout (16MB flash):

| Partition | Type | Size |
|-----------|------|------|
| nvs | data | 24KB |
| otadata | data | 8KB |
| phy_init | data | 4KB |
| ota_0 | app | 2MB |
| ota_1 | app | 2MB |
| spiffs | data | ~12MB |

## Credits

- Original hardware by [Rotrics](https://rotrics.com)
- Original open-source firmware by [PreviousTube contributors](https://github.com/previoustube/previoustube)
- Split-flap animation and LVGL integration by Ian Levesque
- Stock firmware reverse-engineering for hardware documentation

## License

MIT
