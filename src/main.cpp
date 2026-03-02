#include <Arduino.h>
#include <M5Unified.h>
#include "config.h"
#include "timer.h"
#include "display.h"
#include "webserver.h"

static constexpr uint32_t LONG_PRESS_MS = 1500;
static constexpr uint32_t MSG_DISPLAY_MS = 2000;
static constexpr uint32_t BEEP_PAUSE_MS = 120;
static constexpr uint32_t DONE_BEEP_INTERVAL = 3000;
static constexpr uint8_t PHASE_CHANGE_BEEPS = 3;
static constexpr uint8_t DONE_BEEPS = 5;
static constexpr uint8_t BRIGHT_ACTIVE = 80;
static constexpr uint8_t BRIGHT_DIM = 15;
static constexpr uint32_t DIM_AFTER_MS = 30000; // 30s

static TimerConfig cfg;
static bool needs_redraw = true;

// Button state for long press detection on BtnC
static uint32_t btnC_press_ms = 0;
static bool btnC_held = false;

// Message overlay
static bool showing_message = false;
static uint32_t message_end_ms = 0;

// Buzzer queue
static uint8_t beep_remaining = 0;
static uint32_t beep_next_ms = 0;

// Done state re-beep
static uint32_t done_last_beep_ms = 0;

// Track phase for transition detection
static Phase last_phase = Phase::IDLE;

// WiFi display shown once
static bool wifi_shown = false;
static uint32_t wifi_shown_ms = 0;

// Auto-dim
static uint32_t last_activity_ms = 0;
static bool display_dimmed = false;

static void wake_display() {
    last_activity_ms = millis();
    if (display_dimmed) {
        M5.Display.setBrightness(BRIGHT_ACTIVE);
        display_dimmed = false;
    }
}

static void queue_beeps(uint8_t count) {
    beep_remaining = count * 2;
    beep_next_ms = millis();
}

static void process_beeps() {
    if (beep_remaining == 0)
        return;
    if (millis() < beep_next_ms)
        return;

    if (beep_remaining % 2 == 0) {
        M5.Speaker.tone(BEEP_FREQ, BEEP_DURATION_MS);
        beep_next_ms = millis() + BEEP_DURATION_MS;
    } else {
        beep_next_ms = millis() + BEEP_PAUSE_MS;
    }
    beep_remaining--;
}

void setup() {
    setCpuFrequencyMhz(80); // 80MHz is plenty for timer + webserver
    btStop();               // disable Bluetooth radio, saves ~10-15mA
    Serial.begin(115200);
    delay(100);
    Serial.println("\n\n=== Kids Time Tracker starting ===");

    M5.begin();
    Serial.println("M5.begin() done");

    M5.Display.setBrightness(BRIGHT_ACTIVE);
    M5.Display.setRotation(1); // 90° rotation = portrait mode

    // Derive display geometry from actual screen dimensions
    setup_display_geometry();
    Serial.printf("Display: %dx%d, center: %d,%d\n", g_screen_w, g_screen_h, g_cx, g_cy);

    M5.Display.fillScreen(COL_BG);
    Serial.println("Display init done");

    config_load(cfg);
    M5.Speaker.setVolume(cfg.volume);
    Serial.printf("Config loaded, volume: %u\n", cfg.volume);

    timer_init();
    Serial.println("Timer init done");

    webserver_start(cfg);
    Serial.println("Webserver start called");

    last_activity_ms = millis();
    needs_redraw = true;
}

void loop() {
    M5.update();
    webserver_loop();

    const TimerState& ts = timer_state();

    // -- WiFi status display (brief, on boot) --
    if (!wifi_shown) {
        if (webserver_ap_mode()) {
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
                display_wifi_status(cfg.wifi_ssid, false, "");
                delay(10);
                return;
            } else {
                wifi_shown = true;
                wifi_shown_ms = 0;
                needs_redraw = true;
            }
        } else {
            wifi_shown = true;
            wifi_shown_ms = 0;
        }
    }

    // Brief pause to show WiFi IP
    if (wifi_shown && wifi_shown_ms > 0 && millis() - wifi_shown_ms < 2000) {
        delay(10);
        return;
    }
    if (wifi_shown_ms > 0) {
        wifi_shown_ms = 0;
        needs_redraw = true;
    }

    // -- AP fallback (WiFi timed out) --
    if (webserver_ap_fallback()) {
        needs_redraw = true;
        wake_display();
    }

    // -- Web UI triggers --
    if (webserver_config_changed()) {
        config_load(cfg);
        M5.Speaker.setVolume(cfg.volume);
        needs_redraw = true;
    }
    if (webserver_start_requested()) {
        timer_start(cfg);
        last_phase = Phase::GREEN;
        showing_message = false;
        needs_redraw = true;
        wake_display();
        queue_beeps(1);
    }
    if (webserver_pause_requested()) {
        timer_pause();
        needs_redraw = true;
        wake_display();
        queue_beeps(2);
    }
    if (webserver_resume_requested()) {
        timer_resume();
        needs_redraw = true;
        wake_display();
        queue_beeps(1);
    }
    if (webserver_stop_requested()) {
        timer_stop();
        last_phase = Phase::IDLE;
        showing_message = false;
        needs_redraw = true;
        beep_remaining = 0;
        wake_display();
        queue_beeps(1);
    }

    // -- Timer tick --
    bool timer_updated = timer_tick();
    if (timer_updated) {
        needs_redraw = true;
    }

    // -- Phase transition detection --
    if (ts.phase != last_phase) {
        wake_display();
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

    // -- Button handling (disabled when locked via web UI) --
    if (!webserver_buttons_locked()) {
        // Any button press wakes the display
        if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed() || M5.BtnC.wasPressed()) {
            wake_display();
        }

        // BtnA (left): Start (idle) / Info (running)
        if (M5.BtnA.wasPressed()) {
            if (ts.phase == Phase::IDLE) {
                timer_start(cfg);
                last_phase = Phase::GREEN;
                needs_redraw = true;
                queue_beeps(1);
            } else if (ts.phase == Phase::GREEN || ts.phase == Phase::YELLOW ||
                       ts.phase == Phase::FINAL) {
                const char* msg = phase_message(ts.phase, cfg);
                display_remaining_message(ts.remaining_seconds, msg);
                showing_message = true;
                message_end_ms = millis() + MSG_DISPLAY_MS;
                queue_beeps(1);
                needs_redraw = false;
            }
        }

        // BtnB (middle): Acknowledge done / Info (running)
        if (M5.BtnB.wasPressed()) {
            if (ts.phase == Phase::DONE) {
                timer_stop();
                last_phase = Phase::IDLE;
                needs_redraw = true;
                beep_remaining = 0;
                queue_beeps(1);
            } else if (ts.phase == Phase::GREEN || ts.phase == Phase::YELLOW ||
                       ts.phase == Phase::FINAL) {
                const char* msg = phase_message(ts.phase, cfg);
                display_remaining_message(ts.remaining_seconds, msg);
                showing_message = true;
                message_end_ms = millis() + MSG_DISPLAY_MS;
                queue_beeps(1);
                needs_redraw = false;
            }
        }

        // BtnC: Long press for reset (child-proof)
        if (M5.BtnC.wasPressed()) {
            btnC_press_ms = millis();
            btnC_held = true;
        }

        if (btnC_held && M5.BtnC.isPressed() && millis() - btnC_press_ms >= LONG_PRESS_MS) {
            btnC_held = false;
            timer_stop();
            last_phase = Phase::IDLE;
            showing_message = false;
            needs_redraw = true;
            queue_beeps(1);
        }

        if (M5.BtnC.wasReleased() && btnC_held) {
            btnC_held = false;
            // Short press on C does nothing (child-proof)
        }
    }

    // -- Message overlay timeout --
    if (showing_message && millis() >= message_end_ms) {
        showing_message = false;
        needs_redraw = true;
    }

    // -- Beeps --
    process_beeps();

    // -- Battery indicator update (throttled, I2C read is expensive) --
    static uint32_t last_bat_check_ms = 0;
    if (millis() - last_bat_check_ms > 60000) {
        check_battery_update();
        last_bat_check_ms = millis();
    }

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
            case Phase::PAUSED: display_running(ts, cfg); break;
            case Phase::DONE: display_done(cfg); break;
        }
        needs_redraw = false;
    }

    // -- Auto-dim after inactivity (only when idle) --
    if (!display_dimmed && ts.phase == Phase::IDLE && millis() - last_activity_ms > DIM_AFTER_MS) {
        M5.Display.setBrightness(BRIGHT_DIM);
        display_dimmed = true;
    }

    delay(10);
}
