#include "webserver.h"

#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>

static WebServer server(80);
static TimerConfig* cfg_ = nullptr;
static bool connected_ = false;
static bool ap_mode_ = false;
static char ip_buf_[16] = "";
static bool config_changed_ = false;
static bool start_requested_ = false;

// -- HTML template --
// Embedded as a raw string. Substitutions done via snprintf.

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
button{
  display:block;width:100%;padding:14px;border:none;border-radius:12px;
  background:#4361ee;color:#fff;font-size:1.1em;cursor:pointer;margin-top:8px}
button:active{background:#3a56d4}
.wifi{background:#0d1b2a;border-radius:12px;padding:16px;margin-bottom:12px}
.saved{text-align:center;color:#2ec4b6;font-size:1.2em;padding:40px 0}
</style>
</head>
<body>
)";

static void handle_root() {
    // Build page with current values
    String html;
    html.reserve(3000);
    html += FPSTR(PAGE_HEAD);
    html += F("<h1>&#9202; Time Tracker</h1>");

    // Green phase
    html += F("<div class='phase phase-green'>");
    html += F("<label>&#x1F7E2; Phase 1 — Minuten</label>");
    html += F("<div class='row'><input type='number' name='gm' min='1' max='120' value='");
    html += cfg_->green_minutes;
    html += F("' form='f'></div>");
    html += F("<label>Nachricht</label>");
    html += F("<input type='text' name='gmsg' maxlength='63' value='");
    html += cfg_->green_msg;
    html += F("' form='f'></div>");

    // Yellow phase
    html += F("<div class='phase phase-yellow'>");
    html += F("<label>&#x1F7E1; Phase 2 — Minuten</label>");
    html += F("<div class='row'><input type='number' name='ym' min='1' max='120' value='");
    html += cfg_->yellow_minutes;
    html += F("' form='f'></div>");
    html += F("<label>Nachricht</label>");
    html += F("<input type='text' name='ymsg' maxlength='63' value='");
    html += cfg_->yellow_msg;
    html += F("' form='f'></div>");

    // Final phase
    html += F("<div class='phase phase-final'>");
    html += F("<label>&#x1F7E3; Phase 3 — Minuten</label>");
    html += F("<div class='row'><input type='number' name='fm' min='1' max='120' value='");
    html += cfg_->final_minutes;
    html += F("' form='f'></div>");
    html += F("<label>Nachricht</label>");
    html += F("<input type='text' name='fmsg' maxlength='63' value='");
    html += cfg_->final_msg;
    html += F("' form='f'></div>");

    // WiFi
    html += F("<div class='wifi'>");
    html += F("<label>WiFi SSID</label>");
    html += F("<input type='text' name='ssid' maxlength='32' value='");
    html += cfg_->wifi_ssid;
    html += F("' form='f'>");
    html += F("<label>WiFi Passwort</label>");
    html += F("<input type='password' name='pass' maxlength='63' value='");
    html += cfg_->wifi_pass;
    html += F("' form='f'></div>");

    // Start button
    html += F("<form id='f' method='POST' action='/save'>");
    html += F("<button type='submit'>Speichern &amp; Timer starten</button>");
    html += F("</form>");

    html += F("</body></html>");

    server.send(200, "text/html", html);
}

static void handle_save() {
    if (server.hasArg("gm"))   cfg_->green_minutes  = server.arg("gm").toInt();
    if (server.hasArg("ym"))   cfg_->yellow_minutes = server.arg("ym").toInt();
    if (server.hasArg("fm"))   cfg_->final_minutes  = server.arg("fm").toInt();
    if (server.hasArg("gmsg")) strncpy(cfg_->green_msg,  server.arg("gmsg").c_str(), MSG_MAX_LEN - 1);
    if (server.hasArg("ymsg")) strncpy(cfg_->yellow_msg, server.arg("ymsg").c_str(), MSG_MAX_LEN - 1);
    if (server.hasArg("fmsg")) strncpy(cfg_->final_msg,  server.arg("fmsg").c_str(), MSG_MAX_LEN - 1);
    if (server.hasArg("ssid")) strncpy(cfg_->wifi_ssid,  server.arg("ssid").c_str(), SSID_MAX_LEN - 1);
    if (server.hasArg("pass")) strncpy(cfg_->wifi_pass,  server.arg("pass").c_str(), PASS_MAX_LEN - 1);

    // Clamp
    if (cfg_->green_minutes  < 1) cfg_->green_minutes  = 1;
    if (cfg_->yellow_minutes < 1) cfg_->yellow_minutes = 1;
    if (cfg_->final_minutes  < 1) cfg_->final_minutes  = 1;

    config_save(*cfg_);

    if (ap_mode_) {
        // WiFi credentials were just configured via AP — reboot to connect
        String html;
        html.reserve(400);
        html += FPSTR(PAGE_HEAD);
        html += F("<div class='saved'>&#x2705; Gespeichert!<br><br>");
        html += F("Neustart...<br>Verbinde mit deinem WiFi und &ouml;ffne<br>");
        html += F("<b>timetracker.local</b></div>");
        html += F("</body></html>");
        server.send(200, "text/html", html);
        delay(1000);
        ESP.restart();
        return;
    }

    config_changed_ = true;
    start_requested_ = true;

    String html;
    html.reserve(600);
    html += FPSTR(PAGE_HEAD);
    html += F("<div class='saved'>&#x2705; Gespeichert!<br><br>");
    html += F("Timer startet...<br><br>");
    html += F("<a href='/' style='color:#4361ee'>Zur&uuml;ck</a></div>");
    html += F("<script>setTimeout(()=>location='/',3000)</script>");
    html += F("</body></html>");
    server.send(200, "text/html", html);
}

void webserver_start(TimerConfig& cfg) {
    cfg_ = &cfg;

    if (strlen(cfg.wifi_ssid) == 0) {
        // No WiFi configured — start AP for initial setup
        WiFi.mode(WIFI_AP);
        WiFi.softAP("TimeTracker", "");
        IPAddress ip = WiFi.softAPIP();
        snprintf(ip_buf_, sizeof(ip_buf_), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);

        MDNS.begin("timetracker");
        server.on("/", HTTP_GET, handle_root);
        server.on("/save", HTTP_POST, handle_save);
        server.begin();

        ap_mode_ = true;
        connected_ = true;
        Serial.printf("AP mode: connect to 'TimeTracker' WiFi, then http://%s/\n", ip_buf_);
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.wifi_ssid, cfg.wifi_pass);

    // Non-blocking: we check in webserver_loop()
}

void webserver_loop() {
    if (cfg_ == nullptr || strlen(cfg_->wifi_ssid) == 0) return;

    if (!connected_ && WiFi.status() == WL_CONNECTED) {
        connected_ = true;
        IPAddress ip = WiFi.localIP();
        snprintf(ip_buf_, sizeof(ip_buf_), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);

        MDNS.begin("timetracker");

        server.on("/", HTTP_GET, handle_root);
        server.on("/save", HTTP_POST, handle_save);
        server.begin();

        Serial.printf("Web server at http://%s/ (timetracker.local)\n", ip_buf_);
    }

    if (connected_) {
        server.handleClient();
    }
}

bool webserver_connected() {
    return connected_;
}

const char* webserver_ip() {
    return ip_buf_;
}

bool webserver_config_changed() {
    bool v = config_changed_;
    config_changed_ = false;
    return v;
}

bool webserver_start_requested() {
    bool v = start_requested_;
    start_requested_ = false;
    return v;
}

bool webserver_ap_mode() {
    return ap_mode_;
}
