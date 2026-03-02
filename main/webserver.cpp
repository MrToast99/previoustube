//  SPDX-FileCopyrightText: 2023 Ian Levesque <ian@ianlevesque.org>
//  SPDX-License-Identifier: MIT

#include "webserver.h"
#include "app_mode.h"
#include "drivers/wifi.h"
#include "modes.h"
#include "settings.h"
#include "weather.h"
#include "esp_timer.h"

#include <cJSON.h>
#include <cstring>
#include <esp_event.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_spiffs.h>
#include <sys/param.h>

ESP_EVENT_DECLARE_BASE(WEBSERVER_EVENTS);
enum { WEBSERVER_EVENT_WEBHOOK };

static const char *TAG = "webserver";
static const size_t MAX_BODY = 2048;
static webhook_callback_t s_webhook_cb = nullptr;
static settings_changed_callback_t s_settings_cb = nullptr;

// Global mode pointers set in main.cpp
extern scoreboard_mode *g_scoreboard_mode;
extern countdown_mode *g_countdown_mode;
extern temperature_mode *g_temperature_mode;

// ─── Helpers ────────────────────────────────────────────────

static int recv_body(httpd_req_t *r, char *buf, size_t max) {
  size_t n = MIN(r->content_len, max - 1);
  int ret = httpd_req_recv(r, buf, n);
  if (ret <= 0) return ret;
  buf[ret] = '\0';
  return ret;
}

#define JSON_RESP(req, json_obj)                                               \
  do {                                                                         \
    const char *_s = cJSON_PrintUnformatted(json_obj);                         \
    httpd_resp_set_type(req, "application/json");                              \
    httpd_resp_send(req, _s, HTTPD_RESP_USE_STRLEN);                           \
    free((void *)_s);                                                          \
    cJSON_Delete(json_obj);                                                    \
  } while (0)

// ─── Config page HTML ───────────────────────────────────────

static const char CONFIG_PAGE[] = R"html(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>PreviousTube</title><style>
*{box-sizing:border-box}body{font-family:system-ui,-apple-system,sans-serif;max-width:560px;margin:0 auto;
padding:16px;background:#111;color:#ddd}h1{color:#fca311;margin:0 0 8px}h2{color:#aaa;font-size:.9em;
margin:20px 0 8px;text-transform:uppercase;letter-spacing:1px}
label{display:block;margin:8px 0 3px;font-size:.85em;color:#aaa}
input,select{width:100%;padding:7px;background:#1a1a2e;border:1px solid #333;color:#eee;border-radius:4px}
input[type=range]{padding:2px}input[type=checkbox]{width:auto}
.row{display:flex;gap:8px;align-items:center}.row input{width:60px}
.tabs{display:flex;gap:0;margin:12px 0}.tab{flex:1;padding:8px;text-align:center;background:#1a1a2e;
border:1px solid #333;cursor:pointer;font-size:.85em;color:#888}.tab.active{background:#263238;color:#fca311;
border-bottom:2px solid #fca311}.panel{display:none}.panel.active{display:block}
button{background:#fca311;color:#000;border:none;padding:10px;width:100%;font-size:1em;cursor:pointer;
border-radius:4px;margin-top:12px}button:hover{background:#e09200}
.status{margin:8px 0;padding:8px;border-radius:4px;display:none;font-size:.9em}
.ok{background:#1b4332;color:#95d5b2;display:block}.err{background:#4a1010;color:#f4a0a0;display:block}
.mode-btn{display:inline-block;padding:6px 12px;margin:3px;background:#1a1a2e;border:1px solid #444;
border-radius:4px;cursor:pointer;color:#ccc;font-size:.85em}
.mode-btn.active{background:#263238;border-color:#fca311;color:#fca311}
.info{font-size:.8em;color:#666;margin-top:16px}
</style></head><body>
<h1>PreviousTube</h1>
<div class="tabs"><div class="tab active" onclick="stab(0)">Display</div><div class="tab" onclick="stab(1)">Time</div>
<div class="tab" onclick="stab(2)">Sound</div><div class="tab" onclick="stab(3)">Weather</div>
<div class="tab" onclick="stab(4)">System</div></div>

<div class="panel active" id="p0">
<h2>Mode</h2><div id="modes"></div>
<h2>Scoreboard</h2>
<div class="row"><label>Left</label><input type="number" id="sl" min="0" value="0">
<label>Right</label><input type="number" id="sr" min="0" value="0">
<button style="width:auto;margin:0" onclick="setScore()">Set</button></div>
<h2>Countdown</h2>
<div class="row"><label>Minutes</label><input type="number" id="cdm" min="1" max="999" value="5">
<button style="width:auto;margin:0" onclick="setCd()">Set</button></div>
<h2>Brightness</h2><input type="range" id="bright" min="10" max="255" value="255" oninput="saveSoon()">
<h2>LED Color</h2>
<div class="row">R<input type="number" id="lr" min="0" max="255" value="228" oninput="saveSoon()">
G<input type="number" id="lg" min="0" max="255" value="112" oninput="saveSoon()">
B<input type="number" id="lb" min="0" max="255" value="37" oninput="saveSoon()"></div>
</div>

<div class="panel" id="p1">
<label>Timezone</label><select id="tz">
<option value="EST5EDT,M3.2.0,M11.1.0">US Eastern</option>
<option value="CST6CDT,M3.2.0,M11.1.0">US Central</option>
<option value="MST7MDT,M3.2.0,M11.1.0">US Mountain</option>
<option value="PST8PDT,M3.2.0,M11.1.0">US Pacific</option>
<option value="AST4ADT,M3.2.0,M11.1.0">US Atlantic</option>
<option value="AKST9AKDT,M3.2.0,M11.1.0">US Alaska</option>
<option value="HST10">US Hawaii</option>
<option value="GMT0BST,M3.5.0/1,M10.5.0">UK</option>
<option value="CET-1CEST,M3.5.0,M10.5.0/3">Central Europe</option>
<option value="EET-2EEST,M3.5.0/3,M10.5.0/4">Eastern Europe</option>
<option value="JST-9">Japan</option>
<option value="KST-9">Korea</option>
<option value="CST-8">China</option>
<option value="IST-5:30">India</option>
<option value="AEST-10AEDT,M10.1.0,M4.1.0/3">Australia Eastern</option>
<option value="NZST-12NZDT,M9.5.0,M4.1.0/3">New Zealand</option>
</select>
<label>Time Format</label><select id="fmt"><option value="0">12-hour</option><option value="1">24-hour</option></select>
<label><input type="checkbox" id="chime"> Hourly chime</label>
</div>

<div class="panel" id="p2">
<label><input type="checkbox" id="snd" checked> Sound enabled</label>
<label>Volume</label><input type="range" id="vol" min="0" max="100" value="80">
</div>

<div class="panel" id="p3">
<label>OpenWeatherMap API Key</label><input type="text" id="wk" placeholder="Get free key at openweathermap.org">
<label>City</label><input type="text" id="wc" value="New York">
<label>Country Code</label><input type="text" id="wcc" value="US" maxlength="2">
<label><input type="checkbox" id="wf" checked> Show Fahrenheit</label>
<button onclick="fetchWeather()">Fetch Now</button>
</div>

<div class="panel" id="p4">
<div class="info" id="sysinfo"></div>
<h2>Firmware Update</h2>
<input type="file" id="fw" accept=".bin">
<button onclick="doOta()">Upload Firmware</button>
<div class="status" id="otast"></div>
<h2>WiFi</h2>
<label>SSID</label><input type="text" id="wssid">
<label>Password</label><input type="password" id="wpsk">
<button onclick="setWifi()">Connect</button>
<button onclick="startAp()">Start AP Mode</button>
</div>

<button onclick="saveAll()">Save Settings</button>
<div class="status" id="st"></div>

<script>
const $=id=>document.getElementById(id);
const modes=['clock','countdown','pomodoro','scoreboard','date','temperature'];
let saveTimer;
function stab(n){document.querySelectorAll('.tab').forEach((t,i)=>{t.classList.toggle('active',i==n)});
document.querySelectorAll('.panel').forEach((p,i)=>{p.classList.toggle('active',i==n)})}
function saveSoon(){clearTimeout(saveTimer);saveTimer=setTimeout(saveAll,1500)}
async function load(){try{const r=await(await fetch('/api/settings')).json();
$('tz').value=r.timezone||'';$('fmt').value=r.time_format||0;$('bright').value=r.brightness||255;
$('lr').value=r.led_r||0;$('lg').value=r.led_g||0;$('lb').value=r.led_b||0;
$('snd').checked=r.sound_enabled;$('chime').checked=r.hourly_chime;$('vol').value=r.volume||80;
$('wk').value=r.weather_api_key||'';$('wc').value=r.weather_city||'';$('wcc').value=r.weather_country||'';
$('wf').checked=r.weather_fahrenheit;
const md=$('modes');md.innerHTML='';
modes.forEach((m,i)=>{const b=document.createElement('span');b.className='mode-btn'+(i==r.active_mode?' active':'');
b.textContent=m;b.onclick=()=>switchMode(i);md.appendChild(b)});
const s=await(await fetch('/api/system')).json();$('sysinfo').innerHTML=
'IP: '+s.ip+'<br>Uptime: '+Math.floor(s.uptime/60)+'m<br>Heap: '+s.free_heap+'<br>PSRAM: '+s.free_psram+
'<br>Version: '+s.version;
}catch(e){console.error(e)}}
async function saveAll(){const st=$('st');try{await fetch('/api/settings',{method:'POST',
headers:{'Content-Type':'application/json'},body:JSON.stringify({
timezone:$('tz').value,time_format:+$('fmt').value,brightness:+$('bright').value,
led_r:+$('lr').value,led_g:+$('lg').value,led_b:+$('lb').value,
sound_enabled:$('snd').checked,hourly_chime:$('chime').checked,volume:+$('vol').value,
weather_api_key:$('wk').value,weather_city:$('wc').value,weather_country:$('wcc').value,
weather_fahrenheit:$('wf').checked})});st.className='status ok';st.textContent='Saved!';
setTimeout(()=>st.style.display='none',2000)}catch(e){st.className='status err';st.textContent=e.message}}
async function switchMode(i){await fetch('/api/mode',{method:'POST',headers:{'Content-Type':'application/json'},
body:JSON.stringify({mode:i})});load()}
async function setScore(){await fetch('/api/scoreboard',{method:'POST',headers:{'Content-Type':'application/json'},
body:JSON.stringify({left:+$('sl').value,right:+$('sr').value})})}
async function setCd(){await fetch('/api/countdown',{method:'POST',headers:{'Content-Type':'application/json'},
body:JSON.stringify({seconds:+$('cdm').value*60})})}
async function fetchWeather(){await fetch('/api/weather/fetch',{method:'POST'});$('st').className='status ok';
$('st').textContent='Fetching...';setTimeout(()=>$('st').style.display='none',3000)}
async function doOta(){const f=$('fw').files[0];if(!f)return;const ost=$('otast');
ost.className='status ok';ost.textContent='Uploading...';
try{const r=await fetch('/api/update_firmware',{method:'POST',body:f});
const j=await r.json();ost.textContent=j.status=='ok'?'Success! Rebooting...':'Error: '+j.error;
ost.className=j.status=='ok'?'status ok':'status err'}catch(e){ost.className='status err';ost.textContent=e.message}}
async function setWifi(){await fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},
body:JSON.stringify({ssid:$('wssid').value,psk:$('wpsk').value})});
$('st').className='status ok';$('st').textContent='Connecting...'}
async function startAp(){await fetch('/api/wifi/ap',{method:'POST'});
$('st').className='status ok';$('st').textContent='AP mode started'}
load();
</script></body></html>)html";

// ─── Handlers ───────────────────────────────────────────────

static esp_err_t h_root(httpd_req_t *r) {
  httpd_resp_set_type(r, "text/html");
  httpd_resp_send(r, CONFIG_PAGE, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t h_ping(httpd_req_t *r) {
  httpd_resp_send(r, "OK", 2);
  return ESP_OK;
}

static esp_err_t h_system(httpd_req_t *r) {
  cJSON *j = cJSON_CreateObject();
  cJSON_AddStringToObject(j, "ip", wifi_get_ip().c_str());
  cJSON_AddNumberToObject(j, "uptime", esp_timer_get_time() / 1000000);
  cJSON_AddNumberToObject(j, "free_heap", esp_get_free_heap_size());
  cJSON_AddNumberToObject(j, "free_psram", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  const esp_app_desc_t *app = esp_app_get_description();
  cJSON_AddStringToObject(j, "version", app->version);
  cJSON_AddStringToObject(j, "project", app->project_name);
  JSON_RESP(r, j);
  return ESP_OK;
}

static esp_err_t h_settings_get(httpd_req_t *r) {
  auto &s = settings::get();
  cJSON *j = cJSON_CreateObject();
  cJSON_AddStringToObject(j, "timezone", s.timezone.c_str());
  cJSON_AddNumberToObject(j, "time_format", (int)s.time_format);
  cJSON_AddNumberToObject(j, "brightness", s.brightness);
  cJSON_AddNumberToObject(j, "active_mode", s.active_mode);
  cJSON_AddNumberToObject(j, "led_r", s.led_r);
  cJSON_AddNumberToObject(j, "led_g", s.led_g);
  cJSON_AddNumberToObject(j, "led_b", s.led_b);
  cJSON_AddBoolToObject(j, "sound_enabled", s.sound_enabled);
  cJSON_AddBoolToObject(j, "hourly_chime", s.hourly_chime);
  cJSON_AddNumberToObject(j, "volume", s.volume);
  cJSON_AddStringToObject(j, "weather_api_key", s.weather_api_key.c_str());
  cJSON_AddStringToObject(j, "weather_city", s.weather_city.c_str());
  cJSON_AddStringToObject(j, "weather_country", s.weather_country.c_str());
  cJSON_AddBoolToObject(j, "weather_fahrenheit", s.weather_fahrenheit);
  JSON_RESP(r, j);
  return ESP_OK;
}

static esp_err_t h_settings_post(httpd_req_t *r) {
  char buf[MAX_BODY];
  if (recv_body(r, buf, sizeof(buf)) <= 0) {
    httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "No body");
    return ESP_FAIL;
  }
  cJSON *j = cJSON_Parse(buf);
  if (!j) { httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "Bad JSON"); return ESP_FAIL; }

  auto &s = settings::get();
  cJSON *it;
  if ((it = cJSON_GetObjectItem(j, "timezone")) && cJSON_IsString(it)) {
    s.timezone = it->valuestring;
    setenv("TZ", s.timezone.c_str(), 1); tzset();
  }
  if ((it = cJSON_GetObjectItem(j, "time_format")) && cJSON_IsNumber(it)) s.time_format = (TimeFormat)it->valueint;
  if ((it = cJSON_GetObjectItem(j, "brightness")) && cJSON_IsNumber(it)) s.brightness = it->valueint;
  if ((it = cJSON_GetObjectItem(j, "led_r")) && cJSON_IsNumber(it)) s.led_r = it->valueint;
  if ((it = cJSON_GetObjectItem(j, "led_g")) && cJSON_IsNumber(it)) s.led_g = it->valueint;
  if ((it = cJSON_GetObjectItem(j, "led_b")) && cJSON_IsNumber(it)) s.led_b = it->valueint;
  if ((it = cJSON_GetObjectItem(j, "sound_enabled")) && cJSON_IsBool(it)) s.sound_enabled = cJSON_IsTrue(it);
  if ((it = cJSON_GetObjectItem(j, "hourly_chime")) && cJSON_IsBool(it)) s.hourly_chime = cJSON_IsTrue(it);
  if ((it = cJSON_GetObjectItem(j, "volume")) && cJSON_IsNumber(it)) s.volume = it->valueint;
  if ((it = cJSON_GetObjectItem(j, "weather_api_key")) && cJSON_IsString(it)) s.weather_api_key = it->valuestring;
  if ((it = cJSON_GetObjectItem(j, "weather_city")) && cJSON_IsString(it)) s.weather_city = it->valuestring;
  if ((it = cJSON_GetObjectItem(j, "weather_country")) && cJSON_IsString(it)) s.weather_country = it->valuestring;
  if ((it = cJSON_GetObjectItem(j, "weather_fahrenheit")) && cJSON_IsBool(it)) s.weather_fahrenheit = cJSON_IsTrue(it);
  cJSON_Delete(j);

  s.save();
  if (s_settings_cb) s_settings_cb();

  httpd_resp_set_type(r, "application/json");
  httpd_resp_send(r, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t h_mode(httpd_req_t *r) {
  char buf[128];
  if (recv_body(r, buf, sizeof(buf)) <= 0) return ESP_FAIL;
  cJSON *j = cJSON_Parse(buf);
  if (!j) return ESP_FAIL;
  cJSON *m = cJSON_GetObjectItem(j, "mode");
  if (m && cJSON_IsNumber(m)) {
    int id = m->valueint;
    if (id >= 0 && id < (int)AppModeId::NUM_MODES) {
      app_manager::get().switch_to((AppModeId)id);
      settings::get().active_mode = id;
      settings::get().save();
    }
  }
  cJSON_Delete(j);
  httpd_resp_set_type(r, "application/json");
  httpd_resp_send(r, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t h_scoreboard(httpd_req_t *r) {
  char buf[128];
  if (recv_body(r, buf, sizeof(buf)) <= 0) return ESP_FAIL;
  cJSON *j = cJSON_Parse(buf);
  if (!j) return ESP_FAIL;
  int left = 0, right = 0;
  cJSON *it;
  if ((it = cJSON_GetObjectItem(j, "left")) && cJSON_IsNumber(it)) left = it->valueint;
  if ((it = cJSON_GetObjectItem(j, "right")) && cJSON_IsNumber(it)) right = it->valueint;
  cJSON_Delete(j);
  if (g_scoreboard_mode) g_scoreboard_mode->set_scores(left, right);
  // Auto-switch to scoreboard mode
  app_manager::get().switch_to(AppModeId::SCOREBOARD);
  httpd_resp_set_type(r, "application/json");
  httpd_resp_send(r, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t h_countdown(httpd_req_t *r) {
  char buf[128];
  if (recv_body(r, buf, sizeof(buf)) <= 0) return ESP_FAIL;
  cJSON *j = cJSON_Parse(buf);
  if (!j) return ESP_FAIL;
  cJSON *it = cJSON_GetObjectItem(j, "seconds");
  if (it && cJSON_IsNumber(it) && g_countdown_mode) {
    g_countdown_mode->set_seconds(it->valueint);
  }
  cJSON_Delete(j);
  app_manager::get().switch_to(AppModeId::COUNTDOWN);
  httpd_resp_set_type(r, "application/json");
  httpd_resp_send(r, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t h_weather_fetch(httpd_req_t *r) {
  weather_fetch_now();
  httpd_resp_set_type(r, "application/json");
  httpd_resp_send(r, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t h_version(httpd_req_t *r) {
  const esp_app_desc_t *app = esp_app_get_description();
  cJSON *j = cJSON_CreateObject();
  cJSON_AddStringToObject(j, "version", app->version);
  cJSON_AddStringToObject(j, "idf", app->idf_ver);
  JSON_RESP(r, j);
  return ESP_OK;
}

// ─── OTA Update ─────────────────────────────────────────────

static esp_err_t h_ota(httpd_req_t *r) {
  ESP_LOGI(TAG, "OTA update started, size=%d", r->content_len);

  const esp_partition_t *update = esp_ota_get_next_update_partition(nullptr);
  if (!update) {
    httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
    return ESP_FAIL;
  }

  esp_ota_handle_t handle;
  esp_err_t err = esp_ota_begin(update, OTA_WITH_SEQUENTIAL_WRITES, &handle);
  if (err != ESP_OK) {
    httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
    return ESP_FAIL;
  }

  char buf[1024];
  int remaining = r->content_len;
  while (remaining > 0) {
    int n = httpd_req_recv(r, buf, MIN(remaining, (int)sizeof(buf)));
    if (n <= 0) {
      esp_ota_abort(handle);
      httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive error");
      return ESP_FAIL;
    }
    err = esp_ota_write(handle, buf, n);
    if (err != ESP_OK) {
      esp_ota_abort(handle);
      httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "Write error");
      return ESP_FAIL;
    }
    remaining -= n;
  }

  err = esp_ota_end(handle);
  if (err != ESP_OK) {
    httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA end failed");
    return ESP_FAIL;
  }

  err = esp_ota_set_boot_partition(update);
  if (err != ESP_OK) {
    httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot failed");
    return ESP_FAIL;
  }

  httpd_resp_set_type(r, "application/json");
  httpd_resp_send(r, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);

  ESP_LOGI(TAG, "OTA complete, rebooting in 2s...");
  vTaskDelay(pdMS_TO_TICKS(2000));
  esp_restart();
  return ESP_OK;
}

// ─── WiFi config ────────────────────────────────────────────

static esp_err_t h_wifi_set(httpd_req_t *r) {
  char buf[256];
  if (recv_body(r, buf, sizeof(buf)) <= 0) return ESP_FAIL;
  cJSON *j = cJSON_Parse(buf);
  if (!j) return ESP_FAIL;
  const char *ssid = "", *psk = "";
  cJSON *it;
  if ((it = cJSON_GetObjectItem(j, "ssid")) && cJSON_IsString(it)) ssid = it->valuestring;
  if ((it = cJSON_GetObjectItem(j, "psk")) && cJSON_IsString(it)) psk = it->valuestring;

  // Write wifi.txt to SPIFFS
  FILE *f = fopen("/spiffs/wifi.txt", "w");
  if (f) {
    fprintf(f, "ssid = %s\npsk = %s\n", ssid, psk);
    fclose(f);
    ESP_LOGI(TAG, "WiFi credentials saved, rebooting...");
  }
  cJSON_Delete(j);

  httpd_resp_set_type(r, "application/json");
  httpd_resp_send(r, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);

  vTaskDelay(pdMS_TO_TICKS(1000));
  esp_restart();
  return ESP_OK;
}

static esp_err_t h_wifi_ap(httpd_req_t *r) {
  wifi_start_ap_provisioning();
  httpd_resp_set_type(r, "application/json");
  httpd_resp_send(r, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t h_reset(httpd_req_t *r) {
  httpd_resp_set_type(r, "application/json");
  httpd_resp_send(r, "{\"status\":\"ok\"}", HTTPD_RESP_USE_STRLEN);
  vTaskDelay(pdMS_TO_TICKS(500));
  esp_restart();
  return ESP_OK;
}

// ─── Webhook ────────────────────────────────────────────────

struct sized_event_data { uint8_t content[256]; size_t length; };

static esp_err_t h_webhook(httpd_req_t *r) {
  sized_event_data ev{};
  size_t n = MIN(r->content_len, sizeof(ev.content));
  int ret = httpd_req_recv(r, (char *)ev.content, n);
  if (ret <= 0) { httpd_resp_send_408(r); return ESP_FAIL; }
  ev.length = n;
  esp_event_post(WEBSERVER_EVENTS, WEBSERVER_EVENT_WEBHOOK, &ev, sizeof(ev), portMAX_DELAY);
  httpd_resp_send(r, "OK", 2);
  return ESP_OK;
}

static void webhook_event_handler(void *, esp_event_base_t, int32_t, void *data) {
  auto *d = static_cast<const sized_event_data *>(data);
  if (s_webhook_cb) s_webhook_cb(d->content, d->length);
}

// ─── Captive portal redirect ────────────────────────────────

static esp_err_t h_captive(httpd_req_t *r) {
  httpd_resp_set_status(r, "302 Found");
  httpd_resp_set_hdr(r, "Location", "http://10.10.10.1/");
  httpd_resp_send(r, nullptr, 0);
  return ESP_OK;
}

// ─── Registration ───────────────────────────────────────────

#define REG(uri_, method_, handler_)                                           \
  { .uri = uri_, .method = method_, .handler = handler_ }

void webserver_init(webhook_callback_t webhook_cb,
                    settings_changed_callback_t settings_cb) {
  s_webhook_cb = webhook_cb;
  s_settings_cb = settings_cb;

  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.max_uri_handlers = 20;
  cfg.stack_size = 12288;
  cfg.uri_match_fn = httpd_uri_match_wildcard;

  httpd_handle_t srv = nullptr;
  ESP_ERROR_CHECK(httpd_start(&srv, &cfg));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WEBSERVER_EVENTS, WEBSERVER_EVENT_WEBHOOK, webhook_event_handler,
      nullptr, nullptr));

  static const httpd_uri_t uris[] = {
      REG("/", HTTP_GET, h_root),
      REG("/api/ping", HTTP_GET, h_ping),
      REG("/api/system", HTTP_GET, h_system),
      REG("/api/settings", HTTP_GET, h_settings_get),
      REG("/api/settings", HTTP_POST, h_settings_post),
      REG("/api/mode", HTTP_POST, h_mode),
      REG("/api/scoreboard", HTTP_POST, h_scoreboard),
      REG("/api/countdown", HTTP_POST, h_countdown),
      REG("/api/weather/fetch", HTTP_POST, h_weather_fetch),
      REG("/api/firmwareVersion", HTTP_GET, h_version),
      REG("/api/update_firmware", HTTP_POST, h_ota),
      REG("/api/wifi", HTTP_POST, h_wifi_set),
      REG("/api/wifi/ap", HTTP_POST, h_wifi_ap),
      REG("/api/reset", HTTP_POST, h_reset),
      REG("/webhook", HTTP_POST, h_webhook),
      REG("/generate_204", HTTP_GET, h_captive),     // Android captive check
      REG("/hotspot-detect*", HTTP_GET, h_captive),   // Apple captive check
      REG("/connecttest*", HTTP_GET, h_captive),      // Windows captive check
  };
  for (auto &u : uris) httpd_register_uri_handler(srv, &u);

  ESP_LOGI(TAG, "Web server started with %d endpoints", (int)(sizeof(uris)/sizeof(uris[0])));
}

ESP_EVENT_DEFINE_BASE(WEBSERVER_EVENTS);
