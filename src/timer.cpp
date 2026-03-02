#include "timer.h"

static TimerState state_;
static uint32_t last_tick_ms_ = 0;

// Phase boundaries (seconds from start where each phase ends)
static uint32_t green_end_;
static uint32_t yellow_end_;

void timer_init() {
    state_.phase = Phase::IDLE;
    state_.total_seconds = 0;
    state_.remaining_seconds = 0;
    state_.phase_remaining = 0;
    state_.phase_total = 0;
}

void timer_start(const TimerConfig& cfg) {
    uint32_t g = static_cast<uint32_t>(cfg.green_minutes) * 60;
    uint32_t y = static_cast<uint32_t>(cfg.yellow_minutes) * 60;
    uint32_t f = static_cast<uint32_t>(cfg.final_minutes) * 60;

    state_.total_seconds = g + y + f;
    state_.remaining_seconds = state_.total_seconds;

    // Boundaries: green ends after g seconds elapsed, etc.
    green_end_ = g;
    yellow_end_ = g + y;

    state_.phase = Phase::GREEN;
    state_.phase_total = g;
    state_.phase_remaining = g;

    last_tick_ms_ = millis();
}

// Determine which phase we're in based on elapsed time
static Phase phase_for_elapsed(uint32_t elapsed) {
    if (elapsed < green_end_) return Phase::GREEN;
    if (elapsed < yellow_end_) return Phase::YELLOW;
    return Phase::FINAL;
}

bool timer_tick() {
    if (state_.phase == Phase::IDLE || state_.phase == Phase::DONE || state_.phase == Phase::PAUSED) {
        return false;
    }

    uint32_t now = millis();
    if (now - last_tick_ms_ < 1000) {
        return false;
    }
    last_tick_ms_ += 1000;

    if (state_.remaining_seconds > 0) {
        state_.remaining_seconds--;
    }

    if (state_.remaining_seconds == 0) {
        state_.phase = Phase::DONE;
        state_.phase_remaining = 0;
        return true;
    }

    uint32_t elapsed = state_.total_seconds - state_.remaining_seconds;
    state_.phase = phase_for_elapsed(elapsed);

    // Compute phase-local remaining
    switch (state_.phase) {
        case Phase::GREEN:
            state_.phase_total = green_end_;
            state_.phase_remaining = green_end_ - elapsed;
            break;
        case Phase::YELLOW:
            state_.phase_total = yellow_end_ - green_end_;
            state_.phase_remaining = yellow_end_ - elapsed;
            break;
        case Phase::FINAL:
            state_.phase_total = state_.total_seconds - yellow_end_;
            state_.phase_remaining = state_.total_seconds - elapsed;
            break;
        default:
            break;
    }

    return true;
}

void timer_pause() {
    if (state_.phase != Phase::IDLE && state_.phase != Phase::DONE && state_.phase != Phase::PAUSED) {
        state_.paused_from = state_.phase;
        state_.phase = Phase::PAUSED;
    }
}

void timer_resume() {
    if (state_.phase == Phase::PAUSED) {
        state_.phase = state_.paused_from;
        last_tick_ms_ = millis();  // reset tick timer to avoid jump
    }
}

void timer_stop() {
    state_.phase = Phase::IDLE;
    state_.remaining_seconds = 0;
}

const TimerState& timer_state() {
    return state_;
}

uint16_t phase_color(Phase phase) {
    switch (phase) {
        case Phase::GREEN:  return COL_GREEN;
        case Phase::YELLOW: return COL_YELLOW;
        case Phase::FINAL:  return COL_FINAL;
        case Phase::DONE:   return COL_FINAL;
        case Phase::PAUSED: return COL_YELLOW;  // Yellow for paused
        default:            return COL_DIM_TEXT;
    }
}

const char* phase_message(Phase phase, const TimerConfig& cfg) {
    switch (phase) {
        case Phase::GREEN:  return cfg.green_msg;
        case Phase::YELLOW: return cfg.yellow_msg;
        case Phase::FINAL:  return cfg.final_msg;
        case Phase::DONE:   return cfg.final_msg;
        case Phase::PAUSED: return "PAUSE";
        default:            return "";
    }
}
