#pragma once

#include <M5Unified.h>
#include "config.h"
#include "timer.h"

// Draw the idle screen (waiting to start)
void display_idle(const TimerConfig& cfg);

// Draw the running/countdown screen
void display_running(const TimerState& ts, const TimerConfig& cfg);

// Draw the "done" screen
void display_done(const TimerConfig& cfg);

// Flash a message briefly (button press feedback)
void display_remaining_message(uint32_t remaining_seconds, const char* phase_msg);

// Show WiFi status on screen
void display_wifi_status(const char* ssid, bool connected, const char* ip);

// Show AP setup screen (first boot, no WiFi configured)
void display_ap_setup(const char* ip);

// Check and update battery indicator if state changed
void check_battery_update();
