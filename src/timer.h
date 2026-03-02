#pragma once

#include <Arduino.h>
#include "config.h"

enum class Phase : uint8_t { IDLE, GREEN, YELLOW, FINAL, DONE, PAUSED };

struct TimerState {
    Phase phase;
    Phase paused_from;          // which phase we were in before pause
    uint32_t total_seconds;     // green + yellow + final
    uint32_t remaining_seconds; // overall remaining
    uint32_t phase_remaining;   // seconds left in current phase
    uint32_t phase_total;       // total seconds of current phase
};

// Initialize / reset timer to IDLE
void timer_init();

// Start the countdown using the given config
void timer_start(const TimerConfig& cfg);

// Call once per loop iteration. Returns true if display needs update.
bool timer_tick();

// Stop and go back to IDLE
void timer_stop();

// Pause the timer (preserves remaining time)
void timer_pause();

// Resume from pause
void timer_resume();

// Get current state (read-only snapshot)
const TimerState& timer_state();

// Get the phase color
uint16_t phase_color(Phase phase);

// Get the phase message from config
const char* phase_message(Phase phase, const TimerConfig& cfg);
