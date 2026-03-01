#include "config.h"

static Preferences prefs;

void config_load(TimerConfig& cfg) {
    TimerConfig defaults = default_config();
    cfg = defaults;

    prefs.begin("tt", true);  // read-only

    cfg.green_minutes  = prefs.getUShort("green_min", defaults.green_minutes);
    cfg.yellow_minutes = prefs.getUShort("yellow_min", defaults.yellow_minutes);
    cfg.final_minutes  = prefs.getUShort("final_min", defaults.final_minutes);

    prefs.getString("green_msg",  cfg.green_msg,  MSG_MAX_LEN);
    prefs.getString("yellow_msg", cfg.yellow_msg, MSG_MAX_LEN);
    prefs.getString("final_msg",  cfg.final_msg,  MSG_MAX_LEN);
    prefs.getString("wifi_ssid",  cfg.wifi_ssid,  SSID_MAX_LEN);
    prefs.getString("wifi_pass",  cfg.wifi_pass,  PASS_MAX_LEN);

    prefs.end();
}

void config_save(const TimerConfig& cfg) {
    prefs.begin("tt", false);  // read-write

    prefs.putUShort("green_min",  cfg.green_minutes);
    prefs.putUShort("yellow_min", cfg.yellow_minutes);
    prefs.putUShort("final_min",  cfg.final_minutes);

    prefs.putString("green_msg",  cfg.green_msg);
    prefs.putString("yellow_msg", cfg.yellow_msg);
    prefs.putString("final_msg",  cfg.final_msg);
    prefs.putString("wifi_ssid",  cfg.wifi_ssid);
    prefs.putString("wifi_pass",  cfg.wifi_pass);

    prefs.end();
}
