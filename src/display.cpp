#include "display.h"

static void draw_arc(float fraction, uint16_t color) {
    if (fraction <= 0.001f) return;
    int16_t start_angle = -90;
    int16_t end_angle = start_angle + static_cast<int16_t>(fraction * 360.0f);
    M5Dial.Display.fillArc(CX, CY, ARC_R_OUTER, ARC_R_INNER,
                           start_angle, end_angle, color);
}

// M5GFX has multiple font types (GLCDfont, BMPfont, RLEfont, GFXfont).
// Use setFont() directly rather than passing font pointers around.
static void draw_text_large(const char* text, int16_t y, uint16_t color, float size = 1.5f) {
    M5Dial.Display.setTextColor(color, COL_BG);
    M5Dial.Display.setTextDatum(middle_center);
    M5Dial.Display.setFont(&fonts::Font4);
    M5Dial.Display.setTextSize(size);
    M5Dial.Display.drawString(text, CX, y);
}

static void draw_text_medium(const char* text, int16_t y, uint16_t color) {
    M5Dial.Display.setTextColor(color, COL_BG);
    M5Dial.Display.setTextDatum(middle_center);
    M5Dial.Display.setFont(&fonts::Font2);
    M5Dial.Display.setTextSize(1.0f);
    M5Dial.Display.drawString(text, CX, y);
}

static void draw_text_small(const char* text, int16_t y, uint16_t color) {
    M5Dial.Display.setTextColor(color, COL_BG);
    M5Dial.Display.setTextDatum(middle_center);
    M5Dial.Display.setFont(&fonts::Font0);
    M5Dial.Display.setTextSize(1.0f);
    M5Dial.Display.drawString(text, CX, y);
}

static void format_time(char* buf, size_t len, uint32_t seconds) {
    uint32_t m = seconds / 60;
    uint32_t s = seconds % 60;
    snprintf(buf, len, "%u:%02u", m, s);
}

void display_idle(const TimerConfig& cfg) {
    M5Dial.Display.startWrite();
    M5Dial.Display.fillScreen(COL_BG);

    draw_text_medium("Bereit", 35, COL_DIM_TEXT);

    uint32_t total = cfg.green_minutes + cfg.yellow_minutes + cfg.final_minutes;
    char buf[16];
    snprintf(buf, sizeof(buf), "%u min", total);
    draw_text_large(buf, CY - 5, COL_GREEN, 1.8f);

    // Phase breakdown
    char phases[48];
    snprintf(phases, sizeof(phases), "%u + %u + %u",
             cfg.green_minutes, cfg.yellow_minutes, cfg.final_minutes);
    draw_text_medium(phases, CY + 35, COL_DIM_TEXT);

    draw_text_small("timetracker.local", 210, COL_DIM_TEXT);

    M5Dial.Display.endWrite();
}

void display_running(const TimerState& ts, const TimerConfig& cfg) {
    M5Dial.Display.startWrite();
    M5Dial.Display.fillScreen(COL_BG);

    uint16_t col = phase_color(ts.phase);

    // Full arc track
    draw_arc(1.0f, COL_ARC_BG);

    // Remaining fraction of total time
    float fraction = (ts.total_seconds > 0)
        ? static_cast<float>(ts.remaining_seconds) / static_cast<float>(ts.total_seconds)
        : 0.0f;
    draw_arc(fraction, col);

    // Time remaining
    char buf[8];
    format_time(buf, sizeof(buf), ts.remaining_seconds);
    draw_text_large(buf, CY - 10, col);

    // Phase message
    const char* msg = phase_message(ts.phase, cfg);
    draw_text_medium(msg, CY + 30, col);

    M5Dial.Display.endWrite();
}

void display_done(const TimerConfig& cfg) {
    M5Dial.Display.startWrite();
    M5Dial.Display.fillScreen(COL_BG);

    draw_arc(1.0f, COL_FINAL);

    draw_text_large("0:00", CY - 10, COL_FINAL);
    draw_text_medium(cfg.final_msg, CY + 30, COL_FINAL);

    M5Dial.Display.endWrite();
}

void display_remaining_message(uint32_t remaining_seconds, const char* phase_msg) {
    M5Dial.Display.startWrite();
    M5Dial.Display.fillScreen(COL_BG);

    uint32_t min = remaining_seconds / 60;
    char buf[48];
    if (min > 0) {
        snprintf(buf, sizeof(buf), "Noch %u Min", min);
    } else {
        snprintf(buf, sizeof(buf), "Noch %us", remaining_seconds);
    }

    draw_text_large(buf, CY - 15, COL_TEXT, 1.2f);
    draw_text_medium(phase_msg, CY + 25, COL_DIM_TEXT);

    M5Dial.Display.endWrite();
}

void display_wifi_status(const char* ssid, bool connected, const char* ip) {
    M5Dial.Display.startWrite();
    M5Dial.Display.fillScreen(COL_BG);

    if (connected) {
        draw_text_medium("WiFi OK", CY - 30, COL_GREEN);
        draw_text_medium(ssid, CY, COL_TEXT);
        draw_text_medium(ip, CY + 30, COL_DIM_TEXT);
    } else {
        draw_text_medium("WiFi...", CY - 15, COL_DIM_TEXT);
        draw_text_medium(ssid, CY + 15, COL_TEXT);
    }

    M5Dial.Display.endWrite();
}

void display_ap_setup(const char* ip) {
    M5Dial.Display.startWrite();
    M5Dial.Display.fillScreen(COL_BG);

    draw_text_medium("WiFi Setup", CY - 50, COL_YELLOW);
    draw_text_small("Verbinde mit WLAN:", CY - 20, COL_DIM_TEXT);
    draw_text_medium("TimeTracker", CY + 5, COL_TEXT);
    draw_text_small("Dann oeffne:", CY + 35, COL_DIM_TEXT);
    draw_text_medium(ip, CY + 60, COL_GREEN);

    M5Dial.Display.endWrite();
}
