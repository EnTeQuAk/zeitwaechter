#include "display.h"
#include <M5Unified.h>

// Track last state for partial redraws
static Phase last_drawn_phase = Phase::IDLE;
static uint32_t last_drawn_seconds = 0xFFFFFFFF;
static int last_battery_level = -1;
static bool last_charging_state = false;

// Dim a color to ~30% brightness for "elapsed" portion
static uint16_t dim_color(uint16_t c) {
    uint8_t r = ((c >> 11) & 0x1F) / 3;
    uint8_t g = ((c >> 5) & 0x3F) / 3;
    uint8_t b = (c & 0x1F) / 3;
    return (r << 11) | (g << 5) | b;
}

// Bar layout constants
static constexpr int16_t BAR_H = 22;
static constexpr int16_t BAR_BOTTOM_MARGIN = 28; // space for hint text below

// Filled 5-pointed star centered at (cx, cy) with outer radius r.
static void draw_star(int16_t cx, int16_t cy, int16_t r, uint16_t col) {
    constexpr float kPi = 3.14159265f;
    float inner_r = r * 0.38f;

    // 10 vertices alternating outer/inner
    int16_t px[10], py[10];
    for (int i = 0; i < 5; i++) {
        float oa = (i * 72 - 90) * kPi / 180.0f;
        float ia = (i * 72 - 54) * kPi / 180.0f;
        px[i * 2] = cx + static_cast<int16_t>(r * cosf(oa));
        py[i * 2] = cy + static_cast<int16_t>(r * sinf(oa));
        px[i * 2 + 1] = cx + static_cast<int16_t>(inner_r * cosf(ia));
        py[i * 2 + 1] = cy + static_cast<int16_t>(inner_r * sinf(ia));
    }

    // Fill from center to each adjacent vertex pair
    for (int i = 0; i < 10; i++) {
        int j = (i + 1) % 10;
        M5.Display.fillTriangle(cx, cy, px[i], py[i], px[j], py[j], col);
    }
}

// Draw a 3-phase progress bar with a star marker at the current position.
static void draw_phase_bar(const TimerState& ts, const TimerConfig& cfg) {
    int16_t bar_w = g_screen_w - (g_margin * 2);
    int16_t x = g_margin;
    int16_t y = g_screen_h - g_margin - BAR_H - BAR_BOTTOM_MARGIN;

    uint32_t g_sec = static_cast<uint32_t>(cfg.green_minutes) * 60;
    uint32_t y_sec = static_cast<uint32_t>(cfg.yellow_minutes) * 60;
    uint32_t f_sec = static_cast<uint32_t>(cfg.final_minutes) * 60;
    uint32_t total = g_sec + y_sec + f_sec;
    if (total == 0)
        return;

    // Phase widths in pixels (proportional to duration)
    int16_t gw = static_cast<int16_t>(static_cast<int32_t>(bar_w) * g_sec / total);
    int16_t yw = static_cast<int16_t>(static_cast<int32_t>(bar_w) * y_sec / total);
    int16_t fw = bar_w - gw - yw; // remainder to avoid rounding gaps

    uint32_t elapsed = ts.total_seconds - ts.remaining_seconds;

    // Draw all segments in their bright color
    int16_t cx = x;
    struct Seg {
        int16_t w;
        uint16_t col;
    };
    Seg segs[3] = {
        {gw, COL_GREEN},
        {yw, COL_YELLOW},
        {fw, COL_FINAL},
    };
    for (int i = 0; i < 3; i++) {
        if (segs[i].w > 0) {
            M5.Display.fillRect(cx, y, segs[i].w, BAR_H, segs[i].col);
            cx += segs[i].w;
        }
    }

    // Rounded corners
    M5.Display.fillRect(x, y, 2, 2, COL_BG);
    M5.Display.fillRect(x, y + BAR_H - 2, 2, 2, COL_BG);
    M5.Display.fillRect(x + bar_w - 2, y, 2, 2, COL_BG);
    M5.Display.fillRect(x + bar_w - 2, y + BAR_H - 2, 2, 2, COL_BG);

    // Star marker centered on the bar at the current position
    if (elapsed > 0 && elapsed < total) {
        int16_t star_x = x + static_cast<int16_t>(static_cast<int32_t>(bar_w) * elapsed / total);
        int16_t star_y = y + BAR_H / 2;
        draw_star(star_x, star_y, 14, dim_color(phase_color(ts.phase)));
    }
}

// Draw text centered, auto-wrapping at word boundaries if too wide.
// Falls back to shrink-to-fit if even a single word is wider than max_w.
static void draw_text_fitted(const char* text, const lgfx::IFont* font, float base_size, int16_t x,
                             int16_t y, uint16_t color, int16_t max_w) {
    M5.Display.setTextColor(color, COL_BG);
    M5.Display.setTextDatum(middle_center);
    M5.Display.setFont(font);
    M5.Display.setTextSize(base_size);

    // Check if it fits on one line
    if (M5.Display.textWidth(text) <= max_w) {
        M5.Display.drawString(text, x, y);
        return;
    }

    // Try word-wrapping into two lines
    // Find best split point (space closest to middle)
    int len = strlen(text);
    int best_split = -1;
    int mid = len / 2;
    for (int d = 0; d < mid; d++) {
        if (text[mid + d] == ' ') {
            best_split = mid + d;
            break;
        }
        if (text[mid - d] == ' ') {
            best_split = mid - d;
            break;
        }
    }

    if (best_split > 0) {
        char line1[MSG_MAX_LEN];
        char line2[MSG_MAX_LEN];
        strncpy(line1, text, best_split);
        line1[best_split] = '\0';
        strncpy(line2, text + best_split + 1, MSG_MAX_LEN - 1);
        line2[MSG_MAX_LEN - 1] = '\0';

        // Check both lines fit, shrink if needed
        int32_t w1 = M5.Display.textWidth(line1);
        int32_t w2 = M5.Display.textWidth(line2);
        int32_t widest = (w1 > w2) ? w1 : w2;
        if (widest > max_w) {
            float sz = base_size * static_cast<float>(max_w) / static_cast<float>(widest);
            M5.Display.setTextSize(sz);
        }

        int16_t line_h = M5.Display.fontHeight() + 2;
        M5.Display.drawString(line1, x, y - line_h / 2);
        M5.Display.drawString(line2, x, y + line_h / 2);
        return;
    }

    // No space found. Single long word, just shrink to fit.
    float sz =
        base_size * static_cast<float>(max_w) / static_cast<float>(M5.Display.textWidth(text));
    M5.Display.setTextSize(sz);
    M5.Display.drawString(text, x, y);
}

static void draw_text_large(const char* text, int16_t x, int16_t y, uint16_t color,
                            float size = 1.0f) {
    draw_text_fitted(text, &fonts::DejaVu40, size, x, y, color, g_screen_w - g_margin * 2);
}

static void draw_text_medium(const char* text, int16_t x, int16_t y, uint16_t color) {
    draw_text_fitted(text, &fonts::DejaVu18, 1.0f, x, y, color, g_screen_w - g_margin * 2);
}

static void draw_text_small(const char* text, int16_t x, int16_t y, uint16_t color) {
    draw_text_fitted(text, &fonts::DejaVu12, 1.0f, x, y, color, g_screen_w - g_margin * 2);
}

// Battery icon dimensions
static constexpr int16_t BAT_W = 25;
static constexpr int16_t BAT_H = 12;
static constexpr int16_t BAT_TIP_W = 3;

static int16_t bat_x() {
    return g_screen_w - g_margin - BAT_W - BAT_TIP_W;
}
static int16_t bat_y() {
    return g_margin;
}

static void draw_battery_indicator() {
    int level = constrain(M5.Power.getBatteryLevel(), -1, 100);
    bool charging = M5.Power.isCharging();

    int16_t x = bat_x();
    int16_t y = bat_y();

    // Battery outline
    uint16_t outline_col = (level > 20) ? COL_DIM_TEXT : COL_RED;
    M5.Display.drawRect(x, y, BAT_W, BAT_H, outline_col);
    // Battery tip
    M5.Display.fillRect(x + BAT_W, y + 3, BAT_TIP_W, BAT_H - 6, outline_col);

    // Clear interior first so old fill doesn't persist
    M5.Display.fillRect(x + 2, y + 2, BAT_W - 4, BAT_H - 4, COL_BG);

    if (level >= 0) {
        int16_t fill_w = (BAT_W - 4) * level / 100;
        uint16_t fill_col = (level > 50) ? COL_GREEN : (level > 20) ? COL_YELLOW : COL_RED;
        if (fill_w > 0) {
            M5.Display.fillRect(x + 2, y + 2, fill_w, BAT_H - 4, fill_col);
        }
    }

    // Charging indicator
    if (charging) {
        M5.Display.setTextColor(COL_TEXT, COL_BG);
        M5.Display.setTextDatum(middle_center);
        M5.Display.setFont(&fonts::Font0);
        M5.Display.setTextSize(1);
        M5.Display.drawString("+", x + BAT_W / 2, y + BAT_H / 2);
    }
}

void check_battery_update() {
    int level = constrain(M5.Power.getBatteryLevel(), -1, 100);
    bool charging = M5.Power.isCharging();

    if (level != last_battery_level || charging != last_charging_state) {
        draw_battery_indicator();
        last_battery_level = level;
        last_charging_state = charging;
    }
}

// Build a TimerState representing "all time elapsed" for the done screen
static TimerState ts_for_done(const TimerConfig& cfg) {
    TimerState ts{};
    ts.phase = Phase::DONE;
    ts.total_seconds = (cfg.green_minutes + cfg.yellow_minutes + cfg.final_minutes) * 60;
    ts.remaining_seconds = 0;
    return ts;
}

static void format_time(char* buf, size_t len, uint32_t seconds) {
    uint32_t m = seconds / 60;
    uint32_t s = seconds % 60;
    snprintf(buf, len, "%u:%02u", m, s);
}

void display_idle(const TimerConfig& cfg) {
    M5.Display.startWrite();
    M5.Display.fillScreen(COL_BG);
    draw_battery_indicator();

    draw_text_medium("Bereit", g_cx, g_margin + 20, COL_DIM_TEXT);

    uint32_t total = cfg.green_minutes + cfg.yellow_minutes + cfg.final_minutes;
    char buf[16];
    snprintf(buf, sizeof(buf), "%u min", total);
    draw_text_large(buf, g_cx, g_cy - 10, COL_GREEN);

    // Phase breakdown
    char phases[48];
    snprintf(phases, sizeof(phases), "%u + %u + %u", cfg.green_minutes, cfg.yellow_minutes,
             cfg.final_minutes);
    draw_text_medium(phases, g_cx, g_cy + 35, COL_DIM_TEXT);

    // Hint for buttons
    draw_text_small("A=Start  C=Reset", g_cx, g_screen_h - g_margin - 5, COL_DIM_TEXT);

    M5.Display.endWrite();

    // Initialize battery tracking
    last_battery_level = M5.Power.getBatteryLevel();
    last_charging_state = M5.Power.isCharging();

    last_drawn_phase = Phase::IDLE;
    last_drawn_seconds = 0xFFFFFFFF;
}

void display_running(const TimerState& ts, const TimerConfig& cfg) {
    char buf[16];
    format_time(buf, sizeof(buf), ts.remaining_seconds);

    bool phase_changed = (ts.phase != last_drawn_phase);
    bool time_changed = (ts.remaining_seconds != last_drawn_seconds);

    M5.Display.startWrite();

    // Full redraw only on phase change
    if (phase_changed) {
        M5.Display.fillScreen(COL_BG);
        draw_battery_indicator();

        uint16_t col = phase_color(ts.phase);

        // Phase message
        const char* msg = phase_message(ts.phase, cfg);
        draw_text_medium(msg, g_cx, g_margin + 20, col);

        // Hint
        draw_text_small("A/B=Info  C=Reset", g_cx, g_screen_h - g_margin - 5, COL_DIM_TEXT);

        last_drawn_phase = ts.phase;
    }

    // Update time and bar every second
    if (time_changed || phase_changed) {
        uint16_t col = phase_color(ts.phase);
        draw_text_large(buf, g_cx, g_cy - 10, col);
        draw_phase_bar(ts, cfg);
    }

    // Check battery, update if changed
    check_battery_update();

    M5.Display.endWrite();

    last_drawn_seconds = ts.remaining_seconds;
}

void display_done(const TimerConfig& cfg) {
    M5.Display.startWrite();

    if (last_drawn_phase != Phase::DONE) {
        M5.Display.fillScreen(COL_BG);
        draw_battery_indicator();
        draw_text_medium(cfg.final_msg, g_cx, g_margin + 20, COL_FINAL);
        draw_text_large("0:00", g_cx, g_cy - 10, COL_FINAL);

        // All-elapsed phase bar
        TimerState done_ts = ts_for_done(cfg);
        draw_phase_bar(done_ts, cfg);

        draw_text_small("B=OK  C=Reset", g_cx, g_screen_h - g_margin - 5, COL_DIM_TEXT);
    }

    // Check battery state and update if needed
    check_battery_update();

    M5.Display.endWrite();

    last_drawn_phase = Phase::DONE;
    last_drawn_seconds = 0;
}

void display_remaining_message(uint32_t remaining_seconds, const char* phase_msg) {
    M5.Display.startWrite();
    M5.Display.fillScreen(COL_BG);

    uint32_t min = remaining_seconds / 60;
    char buf[48];
    if (min > 0) {
        snprintf(buf, sizeof(buf), "Noch %u Min", min);
    } else {
        snprintf(buf, sizeof(buf), "Noch %us", remaining_seconds);
    }

    draw_text_large(buf, g_cx, g_cy - 20, COL_TEXT);
    draw_text_medium(phase_msg, g_cx, g_cy + 25, COL_DIM_TEXT);

    M5.Display.endWrite();
}

void display_wifi_status(const char* ssid, bool connected, const char* ip) {
    M5.Display.startWrite();
    M5.Display.fillScreen(COL_BG);
    draw_battery_indicator();

    if (connected) {
        draw_text_medium("WiFi OK", g_cx, g_cy - 40, COL_GREEN);
        draw_text_medium(ssid, g_cx, g_cy - 10, COL_TEXT);
        draw_text_medium(ip, g_cx, g_cy + 25, COL_DIM_TEXT);
    } else {
        draw_text_medium("WiFi...", g_cx, g_cy - 20, COL_DIM_TEXT);
        draw_text_medium(ssid, g_cx, g_cy + 20, COL_TEXT);
    }

    M5.Display.endWrite();
}

void display_ap_setup(const char* ip) {
    M5.Display.startWrite();
    M5.Display.fillScreen(COL_BG);
    draw_battery_indicator();

    int16_t y = g_margin;
    draw_text_medium("WiFi Setup", g_cx, y, COL_YELLOW);
    y += 30;
    draw_text_small("Verbinde mit WLAN:", g_cx, y, COL_DIM_TEXT);
    y += 30;
    draw_text_medium("TimeTracker", g_cx, y, COL_TEXT);
    y += 35;
    draw_text_small("Dann oeffne:", g_cx, y, COL_DIM_TEXT);
    y += 35;
    draw_text_medium(ip, g_cx, y, COL_GREEN);

    M5.Display.endWrite();
}
