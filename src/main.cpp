#include <Arduino.h>
#include "M5Dial.h"
#include "config.h"
#include "timer.h"
#include "display.h"
#include "webserver.h"

static constexpr uint32_t LONG_PRESS_MS       = 1500;  // longer to avoid accidental kid resets
static constexpr uint32_t MSG_DISPLAY_MS       = 2000;  // how long "Noch X Min" stays on screen
static constexpr uint32_t BEEP_PAUSE_MS        = 120;
static constexpr uint32_t DONE_BEEP_INTERVAL   = 3000;  // re-beep every 3s when done
static constexpr uint8_t  PHASE_CHANGE_BEEPS   = 3;
static constexpr uint8_t  DONE_BEEPS           = 5;

static TimerConfig cfg;
static bool needs_redraw = true;

// Button state
static uint32_t btn_press_ms = 0;
static bool btn_held = false;

// Message overlay
static bool showing_message = false;
static uint32_t message_end_ms = 0;

// Buzzer queue
static uint8_t  beep_remaining = 0;
static uint32_t beep_next_ms = 0;

// Done state re-beep
static uint32_t done_last_beep_ms = 0;

// Track phase for transition detection
static Phase last_phase = Phase::IDLE;

// WiFi display shown once
static bool wifi_shown = false;
static uint32_t wifi_shown_ms = 0;

static void queue_beeps(uint8_t count) {
    beep_remaining = count * 2;  // on + off pairs
    beep_next_ms = millis();
}

static void process_beeps() {
    if (beep_remaining == 0) return;
    if (millis() < beep_next_ms) return;

    if (beep_remaining % 2 == 0) {
        M5Dial.Speaker.tone(BEEP_FREQ, BEEP_DURATION_MS);
        beep_next_ms = millis() + BEEP_DURATION_MS;
    } else {
        beep_next_ms = millis() + BEEP_PAUSE_MS;
    }
    beep_remaining--;
}

void setup() {
    auto cfg_m5 = M5.config();
    M5Dial.begin(cfg_m5, true, false);  // encoder=true, RFID=false
    M5Dial.Display.setBrightness(60);
    M5Dial.Display.setRotation(0);
    M5Dial.Display.fillScreen(COL_BG);

    // Hold power pin for battery operation
    pinMode(46, OUTPUT);
    digitalWrite(46, HIGH);

    config_load(cfg);
    timer_init();

    webserver_start(cfg);

    needs_redraw = true;
}

void loop() {
    M5Dial.update();
    webserver_loop();

    const TimerState& ts = timer_state();

    // -- WiFi status display (brief, on boot) --
    if (!wifi_shown) {
        if (webserver_ap_mode()) {
            // AP mode — show setup instructions
            display_wifi_status("TimeTracker", true, webserver_ip());
            wifi_shown = true;
            wifi_shown_ms = millis();
            needs_redraw = true;
        } else if (strlen(cfg.wifi_ssid) > 0) {
            if (webserver_connected()) {
                display_wifi_status(cfg.wifi_ssid, true, webserver_ip());
                wifi_shown = true;
                wifi_shown_ms = millis();
                needs_redraw = true;
            } else if (millis() < 10000) {
                // Still connecting, show status
                display_wifi_status(cfg.wifi_ssid, false, "");
                delay(10);
                return;
            } else {
                // Give up showing WiFi status after 10s
                wifi_shown = true;
                wifi_shown_ms = 0;
                needs_redraw = true;
            }
        } else {
            wifi_shown = true;
            wifi_shown_ms = 0;
        }
    }

    // Brief pause to show WiFi IP before continuing
    if (wifi_shown && wifi_shown_ms > 0 && millis() - wifi_shown_ms < 2000) {
        delay(10);
        return;
    }
    if (wifi_shown_ms > 0) {
        wifi_shown_ms = 0;
        needs_redraw = true;
    }

    // -- Web UI triggers --
    if (webserver_config_changed()) {
        config_load(cfg);
    }
    if (webserver_start_requested()) {
        timer_start(cfg);
        last_phase = Phase::GREEN;
        showing_message = false;
        needs_redraw = true;
        queue_beeps(1);
    }

    // -- Timer tick --
    bool timer_updated = timer_tick();
    if (timer_updated) {
        needs_redraw = true;
    }

    // -- Phase transition detection --
    if (ts.phase != last_phase) {
        if (ts.phase == Phase::YELLOW || ts.phase == Phase::FINAL) {
            queue_beeps(PHASE_CHANGE_BEEPS);
        }
        if (ts.phase == Phase::DONE) {
            queue_beeps(DONE_BEEPS);
            done_last_beep_ms = millis();
        }
        last_phase = ts.phase;
        needs_redraw = true;
    }

    // Re-beep periodically when done
    if (ts.phase == Phase::DONE && millis() - done_last_beep_ms > DONE_BEEP_INTERVAL) {
        queue_beeps(2);
        done_last_beep_ms = millis();
    }

    // -- Button input --
    // Child-proof: short press shows remaining time. Long press resets (parent).
    if (M5Dial.BtnA.wasPressed()) {
        btn_press_ms = millis();
        btn_held = true;
    }

    if (btn_held && M5Dial.BtnA.isPressed() && millis() - btn_press_ms >= LONG_PRESS_MS) {
        // Long press detected — reset to idle
        btn_held = false;
        timer_stop();
        last_phase = Phase::IDLE;
        showing_message = false;
        needs_redraw = true;
        queue_beeps(1);
    }

    if (M5Dial.BtnA.wasReleased() && btn_held) {
        btn_held = false;
        uint32_t held = millis() - btn_press_ms;

        if (held < LONG_PRESS_MS) {
            if (ts.phase == Phase::IDLE) {
                // Start timer
                timer_start(cfg);
                last_phase = Phase::GREEN;
                needs_redraw = true;
                queue_beeps(1);
            } else if (ts.phase == Phase::DONE) {
                // Acknowledge — go back to idle
                timer_stop();
                last_phase = Phase::IDLE;
                needs_redraw = true;
            } else {
                // Running — show remaining time message
                const char* msg = phase_message(ts.phase, cfg);
                display_remaining_message(ts.remaining_seconds, msg);
                showing_message = true;
                message_end_ms = millis() + MSG_DISPLAY_MS;
                queue_beeps(1);
                needs_redraw = false;  // we just drew the message
            }
        }
    }

    // Encoder and touch are ignored while running (child-proof).
    // They only work in IDLE for potential future features.

    // -- Message overlay timeout --
    if (showing_message && millis() >= message_end_ms) {
        showing_message = false;
        needs_redraw = true;
    }

    // -- Beeps --
    process_beeps();

    // -- Redraw --
    if (needs_redraw && !showing_message) {
        switch (ts.phase) {
            case Phase::IDLE:
                if (webserver_ap_mode()) {
                    display_ap_setup(webserver_ip());
                } else {
                    display_idle(cfg);
                }
                break;
            case Phase::GREEN:
            case Phase::YELLOW:
            case Phase::FINAL:
                display_running(ts, cfg);
                break;
            case Phase::DONE:
                display_done(cfg);
                break;
        }
        needs_redraw = false;
    }

    delay(10);
}
