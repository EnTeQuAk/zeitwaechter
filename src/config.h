#pragma once

#include <Arduino.h>
#include <Preferences.h>

// -- Display geometry --
static constexpr int16_t CX = 120;
static constexpr int16_t CY = 120;
static constexpr int16_t ARC_R_OUTER = 115;
static constexpr int16_t ARC_R_INNER = 80;

// -- Colors (RGB565) --
static constexpr uint16_t COL_BG       = 0x0000;  // black
static constexpr uint16_t COL_GREEN    = 0x2EC4;  // softer green
static constexpr uint16_t COL_YELLOW   = 0xFEA0;  // warm yellow/amber
static constexpr uint16_t COL_FINAL    = 0xF81F;  // magenta
static constexpr uint16_t COL_TEXT     = 0xFFFF;
static constexpr uint16_t COL_DIM_TEXT = 0x7BEF;
static constexpr uint16_t COL_ARC_BG   = 0x18E3;  // dark gray track

// -- Buzzer --
static constexpr uint32_t BEEP_FREQ        = 2000;
static constexpr uint32_t BEEP_DURATION_MS = 120;

// Max lengths for string fields (including null terminator)
static constexpr size_t MSG_MAX_LEN  = 64;
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

    // WiFi
    char wifi_ssid[SSID_MAX_LEN];
    char wifi_pass[PASS_MAX_LEN];
};

// Sensible defaults for a morning routine
inline TimerConfig default_config() {
    TimerConfig cfg{};
    cfg.green_minutes  = 15;
    cfg.yellow_minutes = 10;
    cfg.final_minutes  = 5;
    strncpy(cfg.green_msg,  "Alles gut!",        MSG_MAX_LEN - 1);
    strncpy(cfg.yellow_msg, "Mach dich fertig!",  MSG_MAX_LEN - 1);
    strncpy(cfg.final_msg,  "Los gehts!",         MSG_MAX_LEN - 1);
    cfg.wifi_ssid[0] = '\0';
    cfg.wifi_pass[0] = '\0';
    return cfg;
}

void config_load(TimerConfig& cfg);
void config_save(const TimerConfig& cfg);
