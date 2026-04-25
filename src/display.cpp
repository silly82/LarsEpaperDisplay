#include "display.h"
#include "config.h"
#include <epd_driver.h>
#include <firasans.h>   // GFXfont FiraSans – liegt in LilyGo-EPD47/src/
#include <string.h>
#include <stdio.h>
#include <esp_heap_caps.h>

// ─────────────────────────────────────────────────────────────────────────────
// Framebuffer
//
// 4-bit Graustufen → 2 Pixel pro Byte → EPD_WIDTH * EPD_HEIGHT / 2 Bytes.
// Muss in PSRAM liegen (zu gross für internen RAM).
// Farben: 0x00 = schwarz, 0xFF = weiss, Zwischenwerte = Graustufen.
// ─────────────────────────────────────────────────────────────────────────────
static uint8_t *fb = nullptr;

// ── Layout-Konstanten ─────────────────────────────────────────────────────────
static const int COL_LEFT = 20;    // linker Rand
static const int COL_MID  = 490;   // Mittellinie (Solar | Verbrauch)

// ── Interne Hilfsfunktionen ───────────────────────────────────────────────────

// Ganzen Framebuffer auf Weiss setzen
static void fb_clear() {
    memset(fb, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
}

// Rechteck im Framebuffer auf Weiss setzen (vor Partial-Update)
static void fb_clear_rect(int x, int y, int w, int h) {
    epd_fill_rect(x, y, w, h, 0xFF, fb);
}

// Text schreiben; cursor_x/y sind die Baseline-Position
static void draw_text(const char *text, int x, int y) {
    int32_t cx = x, cy = y;
    writeln((GFXfont *)&FiraSans, text, &cx, &cy, fb);
}

// ── Kleiner Text (ca. halbe FiraSans-Größe via 2:1-Downscale) ────────────────
// x = linker Rand, baseline_y = Baseline im Haupt-FB (wie draw_text).
// Nur nicht-weisse Pixel werden geschrieben → mehrere Aufrufe überlappen nicht.
static inline uint8_t px_get(const uint8_t *buf, int x, int y) {
    uint32_t i = (uint32_t)y * (EPD_WIDTH / 2) + (uint32_t)(x / 2);
    return (x & 1) ? (buf[i] >> 4) : (buf[i] & 0x0F);
}
static inline void px_set(uint8_t *buf, int x, int y, uint8_t v) {
    uint32_t i = (uint32_t)y * (EPD_WIDTH / 2) + (uint32_t)(x / 2);
    if (x & 1) buf[i] = (buf[i] & 0x0F) | ((v & 0x0F) << 4);
    else        buf[i] = (buf[i] & 0xF0) | (v & 0x0F);
}

static void draw_text_small(const char *text, int dest_x, int baseline_y) {
    // FiraSans: ascender≈39, descender≈12, advance_y≈50
    // Wir rendern in ein Temp-Puffer (volle Breite, 70 Zeilen) und skalieren 2:1
    // ins Haupt-FB. Ergebnis: Glyphen ~halb so gross (~25 px Höhe).
    const int SRCTH = 70;
    const int SRC_BL = 50;   // Baseline im Temp-Puffer
    int y_top = baseline_y - SRC_BL / 2;

    uint8_t *tmp = (uint8_t *)heap_caps_malloc((EPD_WIDTH / 2) * SRCTH,
                                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!tmp) return;
    memset(tmp, 0xFF, (EPD_WIDTH / 2) * SRCTH);

    int32_t cx = 0, cy = SRC_BL;
    writeln((GFXfont *)&FiraSans, text, &cx, &cy, tmp);

    for (int sy = 0; sy + 1 < SRCTH; sy += 2) {
        int dy = y_top + sy / 2;
        if (dy < 0 || dy >= EPD_HEIGHT) continue;
        for (int sx = 0; sx + 1 < EPD_WIDTH; sx += 2) {
            int dx = dest_x + sx / 2;
            if (dx >= EPD_WIDTH) break;
            uint32_t v = (uint32_t)px_get(tmp, sx,   sy)
                       + (uint32_t)px_get(tmp, sx+1, sy)
                       + (uint32_t)px_get(tmp, sx,   sy+1)
                       + (uint32_t)px_get(tmp, sx+1, sy+1);
            uint8_t avg = (uint8_t)(v / 4);
            if (avg < 0xF)
                px_set(fb, dx, dy, avg);
        }
    }
    heap_caps_free(tmp);
}

static void draw_hline(int x, int y, int len) {
    epd_draw_hline(x, y, len, 0x00, fb);
}

// SOC-Fortschrittsbalken zeichnen
// x,y = oben-links, w = Gesamtbreite, h = Höhe, soc = 0..100 (-1 = unbekannt)
static void draw_soc_bar(int x, int y, int w, int h, float soc) {
    epd_draw_rect(x, y, w, h, 0x00, fb);   // Rahmen
    if (soc >= 0.0f) {
        int fill = (int)(soc / 100.0f * (float)(w - 4));
        epd_fill_rect(x + 2, y + 2, fill, h - 4, 0x00, fb);
    }
}

// Float → String mit einer Nachkommastelle; "-1" wird als "--" dargestellt
static void ftoa1(float v, char *buf, size_t len) {
    if (v < 0.0f) snprintf(buf, len, "--");
    else           snprintf(buf, len, "%.1f", (double)v);
}

// ── Rendering ─────────────────────────────────────────────────────────────────

// Datenbereiche (Abschnitte 1+2) in den Framebuffer zeichnen.
// Wird sowohl vom Full-Refresh als auch vom Partial-Update aufgerufen.
static void render_data(const VictronData &d) {
    char buf[32];

    // ── Abschnitt 1: Akku (y 0..220) ────────────────────────────────────────

    draw_text("AKKU", COL_LEFT, 60);

    // Spannung rechtsbündig neben dem Label
    ftoa1(d.voltage, buf, sizeof(buf));
    strncat(buf, " V", sizeof(buf) - strlen(buf) - 1);
    draw_text(buf, COL_MID + 120, 60);

    // SOC-Balken: 840 px breit, 60 px hoch
    draw_soc_bar(COL_LEFT, 80, EPD_WIDTH - 40, 60, d.soc);

    // SOC-Prozentzahl über dem rechten Balkenende
    if (d.soc >= 0.0f) snprintf(buf, sizeof(buf), "%.0f %%", (double)d.soc);
    else                snprintf(buf, sizeof(buf), "-- %%");
    draw_text(buf, EPD_WIDTH - 140, 75);

    // Strom unterhalb des Balkens
    ftoa1(d.current, buf, sizeof(buf));
    strncat(buf, " A", sizeof(buf) - strlen(buf) - 1);
    draw_text(buf, COL_LEFT, 185);

    // ── Trennlinie ───────────────────────────────────────────────────────────
    draw_hline(COL_LEFT, 200, EPD_WIDTH - 40);

    // ── Abschnitt 2: Solar / Verbrauch (y 200..340) ──────────────────────────

    draw_text("SOLAR", COL_LEFT, 255);
    ftoa1(d.solar_w, buf, sizeof(buf));
    strncat(buf, " W", sizeof(buf) - strlen(buf) - 1);
    draw_text(buf, COL_LEFT, 315);

    // Vertikale Mittellinie zwischen Solar und Verbrauch
    epd_draw_vline(COL_MID, 200, 140, 0x00, fb);

    draw_text("VERBRAUCH", COL_MID + 20, 255);
    ftoa1(d.load_w, buf, sizeof(buf));
    strncat(buf, " W", sizeof(buf) - strlen(buf) - 1);
    draw_text(buf, COL_MID + 20, 315);

    // ── Trennlinie ───────────────────────────────────────────────────────────
    draw_hline(COL_LEFT, 340, EPD_WIDTH - 40);

    // ── Abschnitt 3: Temperaturen (y 340..400) ───────────────────────────────
    // 4 Spalten à ~230 px: Aus · Inn · Kuehl · Schrank
    // \xb0 = ° in Latin-1 (wie bei writeln erwartet)
    struct { float val; const char *label; int x; } temps[] = {
        { d.temp_aussen,  "Aus", 20  },
        { d.temp_innen,   "Inn", 250 },
        { d.temp_fridge,  "Khl", 480 },
        { d.temp_cabinet, "Ger", 710 },
    };
    for (auto &t : temps) {
        char tmp[16];
        ftoa1(t.val, tmp, sizeof(tmp));
        snprintf(buf, sizeof(buf), "%s %s\xb0""C", t.label, tmp);
        draw_text(buf, t.x, 382);
    }
}

static const char *relay_st_txt(int8_t v) {
    if (v == 1) return "AN";
    if (v == 0) return "aus";
    return "--";
}

// Status + unteres Menue (Abschnitt 3, y 400..540)
static void render_status(bool mqtt_ok, const char *ip, int menu_sel, const int8_t *relay_st) {
    char buf[32];

    draw_hline(COL_LEFT, 400, EPD_WIDTH - 40);

    snprintf(buf, sizeof(buf), "WiFi: %s", ip ? ip : "--");
    draw_text_small(buf, COL_LEFT, 430);

    draw_text_small(mqtt_ok ? "MQTT: OK" : "MQTT: --", COL_MID + 120, 430);

    // Menueleiste: Relais zeigen Istzustand (MQTT state)
    const int y0   = 458;
    const int h    = 56;
    const int gap  = 6;
    const int n    = MENU_ITEM_COUNT;
    const int cell = (EPD_WIDTH - 2 * COL_LEFT - (n - 1) * gap) / n;
    const int x0   = COL_LEFT;

    for (int i = 0; i < n; i++) {
        int x = x0 + i * (cell + gap);
        if (i == menu_sel) {
            epd_draw_rect(x, y0, cell, h, 0x00, fb);
            epd_draw_rect(x + 2, y0 + 2, cell - 4, h - 4, 0x00, fb);
        } else {
            epd_draw_rect(x, y0, cell, h, 0x99, fb);
        }
        if (i < RELAY_STATE_COUNT) {
            snprintf(buf, sizeof(buf), "R%d %s", i + 1, relay_st_txt(relay_st[i]));
        } else {
            strncpy(buf, "Bild neu", sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
        }
        draw_text(buf, x + 6, y0 + 38);
    }
}

// ── EPD-Ausgabe-Hilfsfunktionen ───────────────────────────────────────────────

// Unteren Bildschirmteil ab STATUS_TOP nur diesen Ausschnitt aus dem FB zum Panel schicken
static constexpr int32_t STATUS_TOP = 400;

static void epd_push_region(Rect_t area) {
    uint8_t *ptr = fb + (size_t)area.y * (EPD_WIDTH / 2) + (size_t)area.x / 2;
    epd_poweron();
    epd_draw_grayscale_image(area, ptr);
    epd_poweroff();
}

// Framebuffer auf das Display schreiben (ohne vorheriges Clear → schnell)
static void epd_push(bool with_clear) {
    epd_poweron();
    if (with_clear) epd_clear();
    epd_draw_grayscale_image(epd_full_screen(), fb);
    epd_poweroff();
}

// ── Öffentliche API ───────────────────────────────────────────────────────────

void display_init() {
    epd_init();
    // PSRAM-Allokation: ps_calloc statt malloc → initialisiert auf 0x00 (schwarz)
    fb = (uint8_t *)ps_calloc(EPD_WIDTH * EPD_HEIGHT / 2, sizeof(uint8_t));
    fb_clear();   // Framebuffer auf Weiss setzen
}

void display_boot_msg(const char *line1, const char *line2) {
    fb_clear();
    draw_text(line1, 40, 260);
    if (line2) draw_text(line2, 40, 320);
    epd_push(/*with_clear=*/true);
}

void display_full_refresh(const VictronData &d, bool mqtt_ok, const char *ip, int menu_sel,
                          const int8_t *relay_st) {
    fb_clear();
    render_data(d);
    render_status(mqtt_ok, ip, menu_sel, relay_st);
    epd_push(/*with_clear=*/true);
}

void display_menu_strip_update(bool mqtt_ok, const char *ip, int menu_sel,
                               const int8_t *relay_st) {
    fb_clear_rect(0, STATUS_TOP, EPD_WIDTH, EPD_HEIGHT - STATUS_TOP);
    render_status(mqtt_ok, ip, menu_sel, relay_st);
    Rect_t area = { 0, STATUS_TOP, EPD_WIDTH, EPD_HEIGHT - STATUS_TOP };
    epd_push_region(area);
}

void display_prov_screen() {
    fb_clear();
    draw_text("SETUP-MODUS", COL_LEFT, 160);
    draw_text("Mit WLAN verbinden:", COL_LEFT, 240);
    draw_text("  " AP_SSID, COL_LEFT, 295);
    draw_text("  Passwort: " AP_PASSWORD, COL_LEFT, 345);
    draw_text("Browser: http://192.168.4.1", COL_LEFT, 430);
    epd_push(/*with_clear=*/true);
}
