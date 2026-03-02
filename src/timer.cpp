#include "timer.h"

static TimerState _state;
static uint32_t _last_tick_ms = 0;

// Phase boundaries (seconds from start where each phase ends)
static uint32_t _green_end;
static uint32_t _yellow_end;

void timer_init() {
    _state.phase = Phase::IDLE;
    _state.total_seconds = 0;
    _state.remaining_seconds = 0;
    _state.phase_remaining = 0;
    _state.phase_total = 0;
}

void timer_start(const TimerConfig& cfg) {
    uint32_t g = static_cast<uint32_t>(cfg.green_minutes) * 60;
    uint32_t y = static_cast<uint32_t>(cfg.yellow_minutes) * 60;
    uint32_t f = static_cast<uint32_t>(cfg.final_minutes) * 60;

    _state.total_seconds = g + y + f;
    _state.remaining_seconds = _state.total_seconds;

    // Boundaries: green ends after g seconds elapsed, etc.
    _green_end = g;
    _yellow_end = g + y;

    _state.phase = Phase::GREEN;
    _state.phase_total = g;
    _state.phase_remaining = g;

    _last_tick_ms = millis();
}

// Determine which phase we're in based on elapsed time
static Phase phase_for_elapsed(uint32_t elapsed) {
    if (elapsed < _green_end)
        return Phase::GREEN;
    if (elapsed < _yellow_end)
        return Phase::YELLOW;
    return Phase::FINAL;
}

bool timer_tick() {
    if (_state.phase == Phase::IDLE || _state.phase == Phase::DONE ||
        _state.phase == Phase::PAUSED) {
        return false;
    }

    uint32_t now = millis();
    if (now - _last_tick_ms < 1000) {
        return false;
    }
    _last_tick_ms += 1000;

    if (_state.remaining_seconds > 0) {
        _state.remaining_seconds--;
    }

    if (_state.remaining_seconds == 0) {
        _state.phase = Phase::DONE;
        _state.phase_remaining = 0;
        return true;
    }

    uint32_t elapsed = _state.total_seconds - _state.remaining_seconds;
    _state.phase = phase_for_elapsed(elapsed);

    // Compute phase-local remaining
    switch (_state.phase) {
        case Phase::GREEN:
            _state.phase_total = _green_end;
            _state.phase_remaining = _green_end - elapsed;
            break;
        case Phase::YELLOW:
            _state.phase_total = _yellow_end - _green_end;
            _state.phase_remaining = _yellow_end - elapsed;
            break;
        case Phase::FINAL:
            _state.phase_total = _state.total_seconds - _yellow_end;
            _state.phase_remaining = _state.total_seconds - elapsed;
            break;
        default: break;
    }

    return true;
}

void timer_pause() {
    if (_state.phase != Phase::IDLE && _state.phase != Phase::DONE &&
        _state.phase != Phase::PAUSED) {
        _state.paused_from = _state.phase;
        _state.phase = Phase::PAUSED;
    }
}

void timer_resume() {
    if (_state.phase == Phase::PAUSED) {
        _state.phase = _state.paused_from;
        _last_tick_ms = millis(); // reset tick timer to avoid jump
    }
}

void timer_stop() {
    _state.phase = Phase::IDLE;
    _state.remaining_seconds = 0;
}

const TimerState& timer_state() {
    return _state;
}

uint16_t phase_color(Phase phase) {
    switch (phase) {
        case Phase::GREEN: return COL_GREEN;
        case Phase::YELLOW: return COL_YELLOW;
        case Phase::FINAL: return COL_FINAL;
        case Phase::DONE: return COL_FINAL;
        case Phase::PAUSED: return COL_YELLOW; // Yellow for paused
        default: return COL_DIM_TEXT;
    }
}

const char* phase_message(Phase phase, const TimerConfig& cfg) {
    switch (phase) {
        case Phase::GREEN: return cfg.green_msg;
        case Phase::YELLOW: return cfg.yellow_msg;
        case Phase::FINAL: return cfg.final_msg;
        case Phase::DONE: return cfg.final_msg;
        case Phase::PAUSED: return "PAUSE";
        default: return "";
    }
}
