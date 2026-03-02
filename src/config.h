#pragma once

#include <Arduino.h>
#include <Preferences.h>

// -- Display geometry (runtime-derived for auto-centering) --
// These are set after M5.begin() based on actual screen dimensions
extern int16_t g_screen_w;
extern int16_t g_screen_h;
extern int16_t g_cx;     // center x
extern int16_t g_cy;     // center y (slightly above true center)
extern int16_t g_margin; // margin from edges

// Call this after M5.begin() to calculate positions
void setup_display_geometry();

// -- Colors (RGB565) --
static constexpr uint16_t COL_BG = 0x0000;     // black
static constexpr uint16_t COL_GREEN = 0x2EC4;  // softer green
static constexpr uint16_t COL_YELLOW = 0xFEA0; // warm yellow/amber
static constexpr uint16_t COL_RED = 0xF800;    // red for low battery
static constexpr uint16_t COL_FINAL = 0xF81F;  // magenta
static constexpr uint16_t COL_TEXT = 0xFFFF;
static constexpr uint16_t COL_DIM_TEXT = 0x7BEF;


// -- Buzzer --
static constexpr uint32_t BEEP_FREQ = 2000;
static constexpr uint32_t BEEP_DURATION_MS = 120;
static constexpr uint8_t DEFAULT_VOLUME = 128; // 0-255

// Max lengths for string fields (including null terminator)
static constexpr size_t MSG_MAX_LEN = 64;
static constexpr size_t SSID_MAX_LEN = 33;
static constexpr size_t PASS_MAX_LEN = 64;

struct TimerConfig {
    // Phase durations in minutes
    uint16_t green_minutes;
    uint16_t yellow_minutes;
    uint16_t final_minutes;

    // Messages shown on screen per phase
    char green_msg[MSG_MAX_LEN];
    char yellow_msg[MSG_MAX_LEN];
    char final_msg[MSG_MAX_LEN];

    // Speaker volume (0-255)
    uint8_t volume;

    // WiFi
    char wifi_ssid[SSID_MAX_LEN];
    char wifi_pass[PASS_MAX_LEN];
};

// Sensible defaults for a morning routine
inline TimerConfig default_config() {
    TimerConfig cfg{};
    cfg.green_minutes = 15;
    cfg.yellow_minutes = 10;
    cfg.final_minutes = 5;
    cfg.volume = DEFAULT_VOLUME;
    strncpy(cfg.green_msg, "Alles gut!", MSG_MAX_LEN - 1);
    strncpy(cfg.yellow_msg, "Mach dich fertig!", MSG_MAX_LEN - 1);
    strncpy(cfg.final_msg, "Los gehts!", MSG_MAX_LEN - 1);
    // WiFi left empty, configure via AP mode on first boot
    cfg.wifi_ssid[0] = '\0';
    cfg.wifi_pass[0] = '\0';
    return cfg;
}

void config_load(TimerConfig& cfg);
void config_save(const TimerConfig& cfg);
