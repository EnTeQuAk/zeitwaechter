#include "webserver.h"

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <M5Unified.h>
#include "timer.h"

static constexpr uint32_t WIFI_TIMEOUT_MS = 15000;

static WebServer server(80);
static TimerConfig* _cfg = nullptr;
static bool _connected = false;
static bool _ap_mode = false;
static bool _ap_fallback = false;
static uint32_t _wifi_start_ms = 0;
static char _ip_buf[16] = "";
static bool _config_changed = false;
static bool _start_requested = false;
static bool _pause_requested = false;
static bool _resume_requested = false;
static bool _stop_requested = false;
static bool _buttons_locked = false;

// -- HTML: shared head with CSS --
static const char PAGE_HEAD[] PROGMEM = R"(<!DOCTYPE html>
<html lang="de">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Time Tracker</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:system-ui,sans-serif;background:#1a1a2e;color:#eee;padding:16px;max-width:480px;margin:0 auto}
h1{font-size:1.4em;margin-bottom:16px;text-align:center}
.phase{border-radius:12px;padding:16px;margin-bottom:12px}
.phase-green{background:#1b4332}
.phase-yellow{background:#5c4b00}
.phase-final{background:#4a0e4e}
label{display:block;font-size:.85em;margin-bottom:4px;opacity:.8}
input[type=number],input[type=text],input[type=password]{
  width:100%;padding:10px;border:1px solid #555;border-radius:8px;
  background:#111;color:#eee;font-size:1em;margin-bottom:10px}
input[type=number]{width:80px}
.row{display:flex;gap:8px;align-items:center}
.row label{flex:1}
.row input{flex:0 0 80px}
.btn{display:block;width:100%;padding:14px;border:none;border-radius:12px;
  color:#fff;font-size:1.1em;cursor:pointer;margin-top:8px}
.btn:active{opacity:.85}
.wifi{background:#0d1b2a;border-radius:12px;padding:16px;margin-bottom:12px}
.saved{text-align:center;color:#2ec4b6;font-size:1.2em;padding:40px 0}
#phase-box{border:3px solid #555;border-radius:16px;padding:24px;text-align:center;margin-bottom:16px}
#rem{font-size:3em;font-weight:bold;font-variant-numeric:tabular-nums;margin:8px 0}
#msg{font-size:1.1em;opacity:.8}
.bar-bg{background:#222;border-radius:10px;height:14px;margin-bottom:16px;overflow:hidden}
.bar-fill{height:100%;border-radius:10px;transition:width .8s ease}
.ctrl-row{display:flex;gap:8px;margin-bottom:8px}
.ctrl-row .btn{flex:1;margin-top:0}
</style>
</head>
<body>
)";

// -- HTML: control panel (JS-driven, no server-side values) --
static const char CONTROLS_HTML[] PROGMEM = R"html(
<div id="ctrl" style="display:none">
<h1>&#9202; Time Tracker</h1>
<div id="phase-box">
<p id="msg"></p>
<p id="rem">--:--</p>
</div>
<div class="bar-bg"><div id="bar-fill" class="bar-fill" style="width:0"></div></div>
<div class="ctrl-row">
<button id="pause-btn" class="btn" style="background:#f59e0b" onclick="doAction('/pause')">&#9208; Pause</button>
<button id="resume-btn" class="btn" style="background:#10b981;display:none" onclick="doAction('/resume')">&#9654; Weiter</button>
</div>
<button id="stop-btn" class="btn" style="background:#ef4444;display:none" onclick="doAction('/stop')">&#9209; Fertig</button>
<button id="lock-btn" class="btn" style="background:#6366f1" onclick="doAction('/lock')">&#128274; Tasten sperren</button>
<p id="clock" style="text-align:center;margin-top:16px;font-size:1.3em;color:#2ec4b6"></p>
<button class="btn" style="background:#333;margin-top:12px" onclick="showCfg()">&#9881; Einstellungen</button>
</div>
)html";

// -- HTML: JavaScript --
static const char PAGE_SCRIPT[] PROGMEM = R"js(
<script>
const colors={GREEN:'#10b981',YELLOW:'#f59e0b',FINAL:'#e040fb',DONE:'#e040fb',PAUSED:'#888'};
function fmt(s){return Math.floor(s/60)+':'+String(s%60).padStart(2,'0');}
function doAction(u){fetch(u,{method:'POST'});}
function showCfg(){document.getElementById('ctrl').style.display='none';document.getElementById('cfg').style.display='block';updateCfgStop();}
function poll(){
fetch('/status').then(r=>r.json()).then(d=>{
const ctrl=document.getElementById('ctrl'),cfg=document.getElementById('cfg');
if(d.phase==='IDLE'){ctrl.style.display='none';cfg.style.display='block';return;}
if(cfg.style.display!=='block'){ctrl.style.display='block';cfg.style.display='none';}
document.getElementById('rem').textContent=fmt(d.remaining);
document.getElementById('msg').textContent=d.message;
const c=colors[d.phase]||'#888';
document.getElementById('phase-box').style.borderColor=c;
document.getElementById('rem').style.color=c;
const pct=d.total>0?(d.remaining/d.total*100):0;
const bf=document.getElementById('bar-fill');bf.style.width=pct+'%';bf.style.background=c;
const isDone=d.phase==='DONE',isPaused=d.phase==='PAUSED',isRunning=!isDone&&!isPaused;
document.getElementById('pause-btn').style.display=isRunning?'block':'none';
document.getElementById('resume-btn').style.display=isPaused?'block':'none';
document.getElementById('stop-btn').style.display=isDone?'block':'none';
document.getElementById('lock-btn').textContent=d.locked?'\u{1F512} Entsperren':'\u{1F513} Sperren';
}).catch(()=>{});}
function updateCfgStop(){
fetch('/status').then(r=>r.json()).then(d=>{
const b=document.getElementById('cfg-stop');
if(b)b.style.display=(d.phase!=='IDLE')?'block':'none';
}).catch(()=>{});}
setInterval(poll,1000);poll();
setInterval(()=>{const e=document.getElementById('clock');if(e)e.textContent=new Date().toLocaleTimeString('de-DE');},1000);
</script>
)js";

static void handle_root() {
    String html;
    html.reserve(5120);
    html += FPSTR(PAGE_HEAD);

    // Controls panel (JS-driven)
    html += FPSTR(CONTROLS_HTML);

    // Config form
    html += F("<div id='cfg'>");
    html += F("<h1>&#9202; Time Tracker</h1>");
    html += F("<button id='cfg-stop' class='btn' style='background:#ef4444;display:none;"
              "margin-bottom:12px' onclick=\"doAction('/stop');this.style.display='none'\">"
              "&#9209; Timer stoppen</button>");

    // Green phase
    html += F("<div class='phase phase-green'>");
    html += F("<label>&#x1F7E2; Phase 1 — Minuten</label>");
    html += F("<div class='row'><input type='number' name='gm' min='1' max='120' value='");
    html += _cfg->green_minutes;
    html += F("' form='f'></div>");
    html += F("<label>Nachricht</label>");
    html += F("<input type='text' name='gmsg' maxlength='63' value='");
    html += _cfg->green_msg;
    html += F("' form='f'></div>");

    // Yellow phase
    html += F("<div class='phase phase-yellow'>");
    html += F("<label>&#x1F7E1; Phase 2 — Minuten</label>");
    html += F("<div class='row'><input type='number' name='ym' min='1' max='120' value='");
    html += _cfg->yellow_minutes;
    html += F("' form='f'></div>");
    html += F("<label>Nachricht</label>");
    html += F("<input type='text' name='ymsg' maxlength='63' value='");
    html += _cfg->yellow_msg;
    html += F("' form='f'></div>");

    // Final phase
    html += F("<div class='phase phase-final'>");
    html += F("<label>&#x1F7E3; Phase 3 — Minuten</label>");
    html += F("<div class='row'><input type='number' name='fm' min='1' max='120' value='");
    html += _cfg->final_minutes;
    html += F("' form='f'></div>");
    html += F("<label>Nachricht</label>");
    html += F("<input type='text' name='fmsg' maxlength='63' value='");
    html += _cfg->final_msg;
    html += F("' form='f'></div>");

    // Volume
    html += F("<div class='wifi'>");
    html += F("<label>&#x1F50A; Lautst&auml;rke</label>");
    html += F("<input type='range' name='vol' min='0' max='255' value='");
    html += _cfg->volume;
    html += F("' form='f' style='width:100%;accent-color:#4361ee;'>");
    html += F("</div>");

    // WiFi
    html += F("<div class='wifi'>");
    html += F("<label>WiFi SSID</label>");
    html += F("<input type='text' name='ssid' maxlength='32' value='");
    html += _cfg->wifi_ssid;
    html += F("' form='f'>");
    html += F("<label>WiFi Passwort</label>");
    html += F("<input type='password' name='pass' maxlength='63' value='");
    html += _cfg->wifi_pass;
    html += F("' form='f'></div>");

    // Submit
    html += F("<form id='f' method='POST' action='/save'>");
    html += F("<button type='submit' class='btn' style='background:#4361ee'>"
              "&#128190; Speichern</button>");
    html += F("<button type='submit' name='start' value='1' class='btn' "
              "style='background:#10b981'>&#9654; Speichern &amp; Starten</button>");
    html += F("</form>");
    html += F("</div>"); // #cfg

    html += FPSTR(PAGE_SCRIPT);
    html += F("</body></html>");

    server.send(200, "text/html", html);
}

static void handle_save() {
    if (server.hasArg("gm"))
        _cfg->green_minutes = server.arg("gm").toInt();
    if (server.hasArg("ym"))
        _cfg->yellow_minutes = server.arg("ym").toInt();
    if (server.hasArg("fm"))
        _cfg->final_minutes = server.arg("fm").toInt();
    if (server.hasArg("vol"))
        _cfg->volume = constrain(server.arg("vol").toInt(), 0, 255);

    if (server.hasArg("gmsg")) {
        strncpy(_cfg->green_msg, server.arg("gmsg").c_str(), MSG_MAX_LEN - 1);
        _cfg->green_msg[MSG_MAX_LEN - 1] = '\0';
    }
    if (server.hasArg("ymsg")) {
        strncpy(_cfg->yellow_msg, server.arg("ymsg").c_str(), MSG_MAX_LEN - 1);
        _cfg->yellow_msg[MSG_MAX_LEN - 1] = '\0';
    }
    if (server.hasArg("fmsg")) {
        strncpy(_cfg->final_msg, server.arg("fmsg").c_str(), MSG_MAX_LEN - 1);
        _cfg->final_msg[MSG_MAX_LEN - 1] = '\0';
    }
    if (server.hasArg("ssid")) {
        strncpy(_cfg->wifi_ssid, server.arg("ssid").c_str(), SSID_MAX_LEN - 1);
        _cfg->wifi_ssid[SSID_MAX_LEN - 1] = '\0';
    }
    if (server.hasArg("pass")) {
        strncpy(_cfg->wifi_pass, server.arg("pass").c_str(), PASS_MAX_LEN - 1);
        _cfg->wifi_pass[PASS_MAX_LEN - 1] = '\0';
    }

    // Clamp
    if (_cfg->green_minutes < 1)
        _cfg->green_minutes = 1;
    if (_cfg->yellow_minutes < 1)
        _cfg->yellow_minutes = 1;
    if (_cfg->final_minutes < 1)
        _cfg->final_minutes = 1;

    config_save(*_cfg);

    if (_ap_mode) {
        String html;
        html.reserve(400);
        html += FPSTR(PAGE_HEAD);
        html += F("<div class='saved'>&#x2705; Gespeichert!<br><br>");
        html += F("Neustart...<br>Verbinde mit deinem WiFi und &ouml;ffne<br>");
        html += F("<b>zeitwaechter.local</b></div>");
        html += F("</body></html>");
        server.send(200, "text/html", html);
        delay(1000);
        ESP.restart();
        return;
    }

    _config_changed = true;
    if (server.hasArg("start"))
        _start_requested = true;
    else
        _stop_requested = true;

    // Redirect back to main page
    server.sendHeader("Location", "/");
    server.send(303);
}

static void handle_pause() {
    _pause_requested = true;
    server.send(200, "text/plain", "OK");
}

static void handle_resume() {
    _resume_requested = true;
    server.send(200, "text/plain", "OK");
}

static void handle_stop() {
    _stop_requested = true;
    server.send(200, "text/plain", "OK");
}

static void handle_lock() {
    _buttons_locked = !_buttons_locked;
    server.send(200, "text/plain", _buttons_locked ? "LOCKED" : "UNLOCKED");
}

// -- BMP helpers for /screenshot --

static void write_le16(uint8_t* buf, uint16_t v) {
    buf[0] = v & 0xFF;
    buf[1] = (v >> 8) & 0xFF;
}

static void write_le32(uint8_t* buf, uint32_t v) {
    buf[0] = v & 0xFF;
    buf[1] = (v >> 8) & 0xFF;
    buf[2] = (v >> 16) & 0xFF;
    buf[3] = (v >> 24) & 0xFF;
}

static void handle_screenshot() {
    int16_t w = M5.Display.width();
    int16_t h = M5.Display.height();

    uint32_t row_size = static_cast<uint32_t>(w) * 3;
    uint32_t row_stride = (row_size + 3) & ~3u; // pad to 4-byte boundary
    uint32_t img_size = row_stride * h;
    uint32_t file_size = 54 + img_size;

    // BMP file header (14 bytes) + DIB header (40 bytes)
    uint8_t hdr[54] = {};
    hdr[0] = 'B';
    hdr[1] = 'M';
    write_le32(&hdr[2], file_size);
    write_le32(&hdr[10], 54); // pixel data offset
    write_le32(&hdr[14], 40); // DIB header size
    write_le32(&hdr[18], static_cast<uint32_t>(w));
    write_le32(&hdr[22], static_cast<uint32_t>(h));
    write_le16(&hdr[26], 1);  // color planes
    write_le16(&hdr[28], 24); // bits per pixel
    write_le32(&hdr[34], img_size);

    auto* rgb565 = static_cast<uint16_t*>(malloc(w * sizeof(uint16_t)));
    auto* row_buf = static_cast<uint8_t*>(malloc(row_stride));

    if (!rgb565 || !row_buf) {
        free(rgb565);
        free(row_buf);
        server.send(500, "text/plain", "Out of memory");
        return;
    }
    memset(row_buf, 0, row_stride);

    server.setContentLength(file_size);
    server.send(200, "image/bmp", "");
    server.sendContent(reinterpret_cast<const char*>(hdr), 54);

    // BMP stores rows bottom-to-top
    for (int16_t y = h - 1; y >= 0; y--) {
        M5.Display.readRect(0, y, w, 1, rgb565);

        for (int16_t x = 0; x < w; x++) {
            // readRect returns big-endian RGB565, swap for LE extraction
            uint16_t raw = rgb565[x];
            uint16_t px = (raw >> 8) | (raw << 8);
            uint8_t r5 = (px >> 11) & 0x1F;
            uint8_t g6 = (px >> 5) & 0x3F;
            uint8_t b5 = px & 0x1F;
            row_buf[x * 3 + 0] = (b5 << 3) | (b5 >> 2);
            row_buf[x * 3 + 1] = (g6 << 2) | (g6 >> 4);
            row_buf[x * 3 + 2] = (r5 << 3) | (r5 >> 2);
        }

        server.sendContent(reinterpret_cast<const char*>(row_buf), row_stride);
    }

    free(rgb565);
    free(row_buf);
}

static void handle_status() {
    const TimerState& ts = timer_state();

    const char* phase_str;
    switch (ts.phase) {
        case Phase::IDLE: phase_str = "IDLE"; break;
        case Phase::GREEN: phase_str = "GREEN"; break;
        case Phase::YELLOW: phase_str = "YELLOW"; break;
        case Phase::FINAL: phase_str = "FINAL"; break;
        case Phase::DONE: phase_str = "DONE"; break;
        case Phase::PAUSED: phase_str = "PAUSED"; break;
        default: phase_str = "IDLE"; break;
    }

    const char* msg = phase_message(ts.phase, *_cfg);

    int bat_level = M5.Power.getBatteryLevel();
    bool bat_charging = M5.Power.isCharging();
    int bat_voltage = M5.Power.getBatteryVoltage();
    int32_t bat_current = M5.Power.getBatteryCurrent(); // mA, positive=charging
    int16_t vbus_voltage = M5.Power.getVBUSVoltage();
    uint8_t brightness = M5.Display.getBrightness();

    String json;
    json.reserve(256);
    json += F("{\"phase\":\"");
    json += phase_str;
    json += F("\",\"remaining\":");
    json += ts.remaining_seconds;
    json += F(",\"total\":");
    json += ts.total_seconds;
    json += F(",\"locked\":");
    json += _buttons_locked ? F("true") : F("false");
    json += F(",\"message\":\"");
    json += msg;
    json += F("\",\"battery\":");
    json += bat_level;
    json += F(",\"charging\":");
    json += bat_charging ? F("true") : F("false");
    json += F(",\"voltage\":");
    json += bat_voltage;
    json += F(",\"current_ma\":");
    json += bat_current;
    json += F(",\"vbus_mv\":");
    json += vbus_voltage;
    json += F(",\"brightness\":");
    json += brightness;
    json += F("}");

    server.send(200, "application/json", json);
}

static void register_handlers() {
    server.on("/", HTTP_GET, handle_root);
    server.on("/save", HTTP_POST, handle_save);
    server.on("/pause", HTTP_POST, handle_pause);
    server.on("/resume", HTTP_POST, handle_resume);
    server.on("/stop", HTTP_POST, handle_stop);
    server.on("/lock", HTTP_POST, handle_lock);
    server.on("/status", HTTP_GET, handle_status);
    server.on("/screenshot", HTTP_GET, handle_screenshot);
}

void webserver_start(TimerConfig& cfg) {
    _cfg = &cfg;

    if (strlen(cfg.wifi_ssid) == 0) {
        Serial.println("No WiFi configured, starting AP mode");
        WiFi.mode(WIFI_AP);
        WiFi.softAP("TimeTracker", "");
        IPAddress ip = WiFi.softAPIP();
        snprintf(_ip_buf, sizeof(_ip_buf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
        Serial.printf("AP IP: %s\n", _ip_buf);

        MDNS.begin("zeitwaechter");
        Serial.println("mDNS started");

        register_handlers();
        server.begin();
        Serial.println("Web server started on port 80");

        _ap_mode = true;
        _connected = true;
        Serial.printf("AP mode ready: connect to 'TimeTracker' WiFi, then http://%s/\n", _ip_buf);
        return;
    }

    Serial.println("WiFi configured, starting STA mode");
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.wifi_ssid, cfg.wifi_pass);
    _wifi_start_ms = millis();
}

void webserver_loop() {
    if (_ap_mode) {
        if (_connected) {
            server.handleClient();
        }
        return;
    }

    if (_cfg == nullptr || strlen(_cfg->wifi_ssid) == 0) {
        return;
    }

    // Fall back to AP mode if WiFi doesn't connect in time
    if (!_connected && !_ap_mode && millis() - _wifi_start_ms > WIFI_TIMEOUT_MS) {
        Serial.println("WiFi connection timed out, falling back to AP mode");
        WiFi.disconnect(true);
        WiFi.mode(WIFI_AP);
        WiFi.softAP("TimeTracker", "");
        IPAddress ip = WiFi.softAPIP();
        snprintf(_ip_buf, sizeof(_ip_buf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
        Serial.printf("AP fallback IP: %s\n", _ip_buf);

        MDNS.begin("zeitwaechter");
        register_handlers();
        server.begin();

        _ap_mode = true;
        _ap_fallback = true;
        _connected = true;
        return;
    }

    if (!_connected && WiFi.status() == WL_CONNECTED) {
        _connected = true;
        IPAddress ip = WiFi.localIP();
        snprintf(_ip_buf, sizeof(_ip_buf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
        Serial.printf("WiFi connected, IP: %s\n", _ip_buf);

        WiFi.setSleep(true); // modem sleep between beacons, saves ~20-40mA
        MDNS.begin("zeitwaechter");

        register_handlers();
        server.begin();

        Serial.printf("Web server at http://%s/ (zeitwaechter.local)\n", _ip_buf);
    }

    if (_connected) {
        // Detect WiFi disconnect and reconnect
        if (WiFi.status() != WL_CONNECTED) {
            _connected = false;
            Serial.println("WiFi disconnected, reconnecting...");
            WiFi.reconnect();
            return;
        }
        server.handleClient();
        yield();
    }
}

bool webserver_connected() {
    return _connected;
}

const char* webserver_ip() {
    return _ip_buf;
}

bool webserver_config_changed() {
    bool v = _config_changed;
    _config_changed = false;
    return v;
}

bool webserver_start_requested() {
    bool v = _start_requested;
    _start_requested = false;
    return v;
}

bool webserver_pause_requested() {
    bool v = _pause_requested;
    _pause_requested = false;
    return v;
}

bool webserver_resume_requested() {
    bool v = _resume_requested;
    _resume_requested = false;
    return v;
}

bool webserver_stop_requested() {
    bool v = _stop_requested;
    _stop_requested = false;
    return v;
}

bool webserver_buttons_locked() {
    return _buttons_locked;
}

bool webserver_ap_mode() {
    return _ap_mode;
}

bool webserver_ap_fallback() {
    bool v = _ap_fallback;
    _ap_fallback = false;
    return v;
}
