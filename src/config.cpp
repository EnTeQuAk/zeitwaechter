#include "config.h"
#include <M5Unified.h>

static Preferences prefs;

// Display geometry (runtime-derived)
int16_t g_screen_w = 0;
int16_t g_screen_h = 0;
int16_t g_cx = 0;
int16_t g_cy = 0;
int16_t g_margin = 0;

void setup_display_geometry() {
    g_screen_w = M5.Display.width();
    g_screen_h = M5.Display.height();
    g_cx = g_screen_w / 2;
    g_cy = g_screen_h / 2 - 20;  // slightly above center for better visual balance
    g_margin = 10;  // smaller margin for battery icon
}

void config_load(TimerConfig& cfg) {
    TimerConfig defaults = default_config();
    cfg = defaults;

    prefs.begin("tt", true);  // read-only

    cfg.green_minutes  = prefs.getUShort("green_min", defaults.green_minutes);
    cfg.yellow_minutes = prefs.getUShort("yellow_min", defaults.yellow_minutes);
    cfg.final_minutes  = prefs.getUShort("final_min", defaults.final_minutes);
    cfg.volume         = prefs.getUChar("volume", defaults.volume);

    prefs.getString("green_msg",  cfg.green_msg,  MSG_MAX_LEN);
    prefs.getString("yellow_msg", cfg.yellow_msg, MSG_MAX_LEN);
    prefs.getString("final_msg",  cfg.final_msg,  MSG_MAX_LEN);
    prefs.getString("wifi_ssid",  cfg.wifi_ssid,  SSID_MAX_LEN);
    prefs.getString("wifi_pass",  cfg.wifi_pass,  PASS_MAX_LEN);

    prefs.end();

    // Clamp values
    if (cfg.green_minutes  < 1 || cfg.green_minutes  > 120) cfg.green_minutes  = defaults.green_minutes;
    if (cfg.yellow_minutes < 1 || cfg.yellow_minutes > 120) cfg.yellow_minutes = defaults.yellow_minutes;
    if (cfg.final_minutes  < 1 || cfg.final_minutes  > 120) cfg.final_minutes  = defaults.final_minutes;
}

void config_save(const TimerConfig& cfg) {
    prefs.begin("tt", false);  // read-write

    prefs.putUShort("green_min",  cfg.green_minutes);
    prefs.putUShort("yellow_min", cfg.yellow_minutes);
    prefs.putUShort("final_min",  cfg.final_minutes);
    prefs.putUChar("volume",      cfg.volume);

    prefs.putString("green_msg",  cfg.green_msg);
    prefs.putString("yellow_msg", cfg.yellow_msg);
    prefs.putString("final_msg",  cfg.final_msg);
    prefs.putString("wifi_ssid",  cfg.wifi_ssid);
    prefs.putString("wifi_pass",  cfg.wifi_pass);

    prefs.end();
}
