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
static uint8_t *s_small_text_buf = nullptr;

// ── Layout-Konstanten ─────────────────────────────────────────────────────────
static const int COL_LEFT = 30;    // linker Rand (mehr Padding)
static const int COL_MID  = 490;   // Mittellinie (Solar | Verbrauch)
static const int CARD_RADIUS = 8;  // Radius für abgerundete Ecken
static const int SHADOW_OFFSET = 3; // Schatten-Versatz

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
    const int SRCTH = 70;
    const int SRC_BL = 50;
    int y_top = baseline_y - SRC_BL / 2;

    if (!s_small_text_buf) return;
    memset(s_small_text_buf, 0xFF, (EPD_WIDTH / 2) * SRCTH);

    int32_t cx = 0, cy = SRC_BL;
    writeln((GFXfont *)&FiraSans, text, &cx, &cy, s_small_text_buf);

    for (int sy = 0; sy + 1 < SRCTH; sy += 2) {
        int dy = y_top + sy / 2;
        if (dy < 0 || dy >= EPD_HEIGHT) continue;
        for (int sx = 0; sx + 1 < EPD_WIDTH; sx += 2) {
            int dx = dest_x + sx / 2;
            if (dx >= EPD_WIDTH) break;
            uint32_t v = (uint32_t)px_get(s_small_text_buf, sx,   sy)
                       + (uint32_t)px_get(s_small_text_buf, sx+1, sy)
                       + (uint32_t)px_get(s_small_text_buf, sx,   sy+1)
                       + (uint32_t)px_get(s_small_text_buf, sx+1, sy+1);
            uint8_t avg = (uint8_t)(v / 4);
            if (avg < 0xF)
                px_set(fb, dx, dy, avg);
        }
    }
}

static void draw_hline(int x, int y, int len) {
    epd_draw_hline(x, y, len, 0x00, fb);
}

// Abgerundetes Rechteck mit Schatten (fancy UI)
static void draw_card(int x, int y, int w, int h, uint8_t fill_color = 0xFF, bool with_shadow = true) {
    // Schatten zeichnen (leicht versetzt)
    if (with_shadow) {
        epd_fill_rect(x + SHADOW_OFFSET, y + SHADOW_OFFSET, w, h, 0xCC, fb);
    }
    
    // Hauptkarte
    epd_fill_rect(x, y, w, h, fill_color, fb);
    epd_draw_rect(x, y, w, h, 0x88, fb);
    
    // Abgerundete Ecken simulieren (vereinfacht)
    // Oben links
    epd_fill_rect(x, y, CARD_RADIUS, CARD_RADIUS, fill_color, fb);
    epd_draw_circle(x + CARD_RADIUS, y + CARD_RADIUS, CARD_RADIUS, 0x88, fb);
    
    // Oben rechts  
    epd_fill_rect(x + w - CARD_RADIUS, y, CARD_RADIUS, CARD_RADIUS, fill_color, fb);
    epd_draw_circle(x + w - CARD_RADIUS, y + CARD_RADIUS, CARD_RADIUS, 0x88, fb);
    
    // Unten links
    epd_fill_rect(x, y + h - CARD_RADIUS, CARD_RADIUS, CARD_RADIUS, fill_color, fb);
    epd_draw_circle(x + CARD_RADIUS, y + h - CARD_RADIUS, CARD_RADIUS, 0x88, fb);
    
    // Unten rechts
    epd_fill_rect(x + w - CARD_RADIUS, y + h - CARD_RADIUS, CARD_RADIUS, CARD_RADIUS, fill_color, fb);
    epd_draw_circle(x + w - CARD_RADIUS, y + h - CARD_RADIUS, CARD_RADIUS, 0x88, fb);
}

// Gradient-Balken für SOC (fancy)
static void draw_gradient_bar(int x, int y, int w, int h, float percentage) {
    // Hintergrund
    draw_card(x, y, w, h, 0xF0, true);
    
    if (percentage >= 0.0f) {
        int fill_w = (int)(percentage / 100.0f * (float)(w - 8));
        
        // Gradient-Effekt durch verschiedene Graustufen
        for (int i = 0; i < fill_w; i += 4) {
            int segment_w = (i + 4 <= fill_w) ? 4 : (fill_w - i);
            uint8_t shade = 0x20 + (uint8_t)((float)i / (float)fill_w * 0x60);
            epd_fill_rect(x + 4 + i, y + 4, segment_w, h - 8, shade, fb);
        }
        
        // Glanz-Effekt oben
        epd_fill_rect(x + 4, y + 4, fill_w, 2, 0x10, fb);
    }
}

// Icon-ähnliche Symbole zeichnen
static void draw_battery_icon(int x, int y, float soc) {
    const int w = 40, h = 20;
    
    // Batterie-Umriss
    epd_draw_rect(x, y, w, h, 0x00, fb);
    epd_fill_rect(x + w, y + 4, 4, h - 8, 0x00, fb); // Plus-Pol
    
    // Füllstand
    if (soc >= 0.0f) {
        int fill = (int)(soc / 100.0f * (float)(w - 4));
        uint8_t color = (soc > 20.0f) ? 0x00 : 0x44; // Rot bei niedrigem SOC
        epd_fill_rect(x + 2, y + 2, fill, h - 4, color, fb);
    }
}

static void draw_solar_icon(int x, int y) {
    // Vereinfachte Sonne
    const int r = 12;
    epd_draw_circle(x + r, y + r, r, 0x00, fb);
    epd_fill_circle(x + r, y + r, r - 2, 0x44, fb);
    
    // Strahlen
    for (int i = 0; i < 8; i++) {
        float angle = i * M_PI / 4.0f;
        int x1 = x + r + (int)((r + 4) * cos(angle));
        int y1 = y + r + (int)((r + 4) * sin(angle));
        int x2 = x + r + (int)((r + 8) * cos(angle));
        int y2 = y + r + (int)((r + 8) * sin(angle));
        epd_draw_line(x1, y1, x2, y2, 0x00, fb);
    }
}

static void draw_load_icon(int x, int y) {
    // Vereinfachtes Haus
    const int w = 24, h = 20;
    
    // Hauswände
    epd_draw_rect(x, y + 8, w, h - 8, 0x00, fb);
    
    // Dach (Dreieck)
    epd_draw_line(x, y + 8, x + w/2, y, 0x00, fb);
    epd_draw_line(x + w/2, y, x + w, y + 8, 0x00, fb);
    
    // Tür
    epd_draw_rect(x + w/2 - 3, y + 12, 6, 8, 0x00, fb);
}

// SOC-Fortschrittsbalken zeichnen (fancy version)
// x,y = oben-links, w = Gesamtbreite, h = Höhe, soc = 0..100 (-1 = unbekannt)
static void draw_soc_bar(int x, int y, int w, int h, float soc) {
    draw_gradient_bar(x, y, w, h, soc);
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

    // ── Abschnitt 1: Akku-Karte (y 20..200) ─────────────────────────────────
    
    const int battery_card_y = 20;
    const int battery_card_h = 160;
    draw_card(COL_LEFT, battery_card_y, EPD_WIDTH - 60, battery_card_h, 0xF8, true);
    
    // Batterie-Icon und Titel
    draw_battery_icon(COL_LEFT + 20, battery_card_y + 20, d.soc);
    draw_text("AKKU", COL_LEFT + 80, battery_card_y + 40);

    // Spannung rechtsbündig
    ftoa1(d.voltage, buf, sizeof(buf));
    strncat(buf, " V", sizeof(buf) - strlen(buf) - 1);
    draw_text(buf, EPD_WIDTH - 180, battery_card_y + 40);

    // SOC-Balken mit Fancy-Gradient
    draw_soc_bar(COL_LEFT + 20, battery_card_y + 60, EPD_WIDTH - 120, 50, d.soc);

    // SOC-Prozentzahl über dem Balken
    if (d.soc >= 0.0f) snprintf(buf, sizeof(buf), "%.0f %%", (double)d.soc);
    else                snprintf(buf, sizeof(buf), "-- %%");
    draw_text(buf, EPD_WIDTH - 180, battery_card_y + 85);

    // Strom unterhalb des Balkens
    ftoa1(d.current, buf, sizeof(buf));
    strncat(buf, " A", sizeof(buf) - strlen(buf) - 1);
    draw_text(buf, COL_LEFT + 20, battery_card_y + 140);

    // ── Abschnitt 2: Solar/Verbrauch-Karten (y 200..340) ────────────────────
    
    const int power_card_y = 200;
    const int power_card_h = 120;
    const int card_gap = 20;
    const int card_w = (EPD_WIDTH - 60 - card_gap) / 2;
    
    // Solar-Karte
    draw_card(COL_LEFT, power_card_y, card_w, power_card_h, 0xF5, true);
    draw_solar_icon(COL_LEFT + 20, power_card_y + 20);
    draw_text("SOLAR", COL_LEFT + 60, power_card_y + 40);
    ftoa1(d.solar_w, buf, sizeof(buf));
    strncat(buf, " W", sizeof(buf) - strlen(buf) - 1);
    draw_text(buf, COL_LEFT + 20, power_card_y + 80);
    
    // Verbrauch-Karte
    const int load_card_x = COL_LEFT + card_w + card_gap;
    draw_card(load_card_x, power_card_y, card_w, power_card_h, 0xF5, true);
    draw_load_icon(load_card_x + 20, power_card_y + 20);
    draw_text("VERBRAUCH", load_card_x + 60, power_card_y + 40);
    ftoa1(d.load_w, buf, sizeof(buf));
    strncat(buf, " W", sizeof(buf) - strlen(buf) - 1);
    draw_text(buf, load_card_x + 20, power_card_y + 80);

    // ── Abschnitt 3: Temperatur-Karten (y=340 bis y=400) ───────────────────
    
    const int temp_card_y = 340;
    const int temp_card_h = 60;
    const int temp_gap = 8;
    const int temp_card_w = (EPD_WIDTH - 60 - 3 * temp_gap) / 4;
    
    struct { float val; const char *name; uint8_t color; } temps[] = {
        { d.temp_aussen,  "Aussen", 0xF0 },
        { d.temp_innen,   "Innen", 0xF2 },
        { d.temp_fridge,  "Kuehl", 0xE8 },
        { d.temp_cabinet, "Schrank", 0xF0 },
    };
    
    for (size_t i = 0; i < sizeof(temps) / sizeof(temps[0]); i++) {
        int x = COL_LEFT + (int)i * (temp_card_w + temp_gap);
        
        // Mini-Karte für jede Temperatur
        draw_card(x, temp_card_y, temp_card_w, temp_card_h, temps[i].color, false);
        
        // Temperatur-Icon (vereinfachtes Thermometer)
        epd_draw_rect(x + 8, temp_card_y + 8, 3, 20, 0x00, fb);
        epd_fill_circle(x + 9, temp_card_y + 30, 4, 0x00, fb);
        
        // Text
        draw_text_small(temps[i].name, x + 18, temp_card_y + 20);
        char tmp[16];
        ftoa1(temps[i].val, tmp, sizeof(tmp));
        snprintf(buf, sizeof(buf), "%s \xb0""C", tmp);
        draw_text_small(buf, x + 18, temp_card_y + 40);
    }
}

static const char *relay_st_txt(int8_t v) {
    if (v == 1) return "AN";
    if (v == 0) return "aus";
    return "--";
}

// Status + unteres Menue (Abschnitt 4, y 410..540) - Fancy Design
static void render_status(bool wifi_ok, bool mqtt_ok, const char *ip, int menu_sel,
                          const int8_t *relay_st) {
    char buf[64];

    const int status_y = 410;
    draw_card(COL_LEFT, status_y, EPD_WIDTH - 60, 30, 0xF8, false);

    if (!wifi_ok) {
        snprintf(buf, sizeof(buf), "WLAN getrennt – verbinde...");
        draw_text_small(buf, COL_LEFT + 40, status_y + 20);
    } else {
        epd_draw_circle(COL_LEFT + 15, status_y + 15, 3, 0x00, fb);
        for (int i = 1; i <= 3; i++) {
            epd_draw_circle(COL_LEFT + 15, status_y + 15, 3 + i * 2, 0x88, fb);
        }
        snprintf(buf, sizeof(buf), "WiFi: %s     MQTT: %s",
                 ip ? ip : "--", mqtt_ok ? "OK" : "getrennt");
        draw_text_small(buf, COL_LEFT + 40, status_y + 20);
    }

    // Fancy Menu-Buttons mit abgerundeten Ecken und Schatten
    const int menu_y = 458;
    const int menu_h = 56;
    const int gap = 8;
    const int n = MENU_ITEM_COUNT;
    const int cell = (EPD_WIDTH - 2 * COL_LEFT - (n - 1) * gap) / n;

    for (int i = 0; i < n; i++) {
        int x = COL_LEFT + i * (cell + gap);
        
        // Button-Stil abhängig von Auswahl und Status
        uint8_t bg_color = 0xF0;
        bool selected = (i == menu_sel);
        
        if (i < RELAY_STATE_COUNT) {
            // Relais-Status-Farbe
            if (relay_st[i] == 1) bg_color = 0xD0;      // Aktiv = dunkler
            else if (relay_st[i] == 0) bg_color = 0xF0; // Inaktiv = hell
            else bg_color = 0xE0;                       // Unbekannt = mittel
        }
        
        // Fancy Button mit Schatten und Auswahl-Effekt
        if (selected) {
            // Ausgewählter Button: doppelter Rahmen, kein Schatten
            draw_card(x, menu_y, cell, menu_h, bg_color, false);
            epd_draw_rect(x - 2, menu_y - 2, cell + 4, menu_h + 4, 0x00, fb);
            epd_draw_rect(x - 1, menu_y - 1, cell + 2, menu_h + 2, 0x44, fb);
        } else {
            // Normaler Button mit Schatten
            draw_card(x, menu_y, cell, menu_h, bg_color, true);
        }
        
        // Button-Text und Icons
        if (i < RELAY_STATE_COUNT) {
            // Relais-Icon (vereinfachter Schalter)
            int icon_x = x + 8;
            int icon_y = menu_y + 12;
            epd_draw_rect(icon_x, icon_y, 12, 8, 0x00, fb);
            if (relay_st[i] == 1) {
                epd_fill_rect(icon_x + 2, icon_y + 2, 8, 4, 0x00, fb);
            }
            
            snprintf(buf, sizeof(buf), "R%d", i + 1);
            draw_text_small(buf, x + 25, menu_y + 20);
            draw_text_small(relay_st_txt(relay_st[i]), x + 8, menu_y + 40);
        } else {
            // Refresh-Icon
            int icon_x = x + cell/2 - 6;
            int icon_y = menu_y + 15;
            epd_draw_circle(icon_x + 6, icon_y + 6, 8, 0x00, fb);
            epd_draw_line(icon_x + 2, icon_y + 2, icon_x + 6, icon_y + 6, 0xFF, fb);
            epd_draw_line(icon_x + 6, icon_y + 6, icon_x + 10, icon_y + 2, 0xFF, fb);
            
            draw_text_small("Refresh", x + 8, menu_y + 40);
        }
    }
}

// ── EPD-Ausgabe-Hilfsfunktionen ───────────────────────────────────────────────

// Unteren Bildschirmteil ab STATUS_TOP nur diesen Ausschnitt aus dem FB zum Panel schicken
static constexpr int32_t STATUS_TOP = 400;

// Definierte Bereiche für partielle Updates
static constexpr Rect_t AREA_SOC_PERCENT = {EPD_WIDTH - 140, 60, 120, 40};     // SOC Prozent
static constexpr Rect_t AREA_VOLTAGE = {COL_MID + 120, 40, 200, 40};           // Spannung
static constexpr Rect_t AREA_CURRENT = {COL_LEFT, 165, 200, 40};               // Strom
static constexpr Rect_t AREA_SOC_BAR = {COL_LEFT, 80, EPD_WIDTH - 40, 60};     // SOC Balken
static constexpr Rect_t AREA_SOLAR = {COL_LEFT, 295, 200, 40};                 // Solar Watt
static constexpr Rect_t AREA_LOAD = {COL_MID + 20, 295, 200, 40};              // Load Watt
static constexpr Rect_t AREA_TEMPS = {COL_LEFT, 340, EPD_WIDTH - 40, 60};      // Alle Temperaturen

static void epd_push_region(Rect_t area) {
    uint8_t *ptr = fb + (size_t)area.y * (EPD_WIDTH / 2) + (size_t)area.x / 2;
    epd_poweron();
    epd_draw_grayscale_image(area, ptr);
    delay(100);  // Longer delay for partial updates to complete properly
    epd_poweroff_all();  // Use poweroff_all for better power management
}

// Framebuffer auf das Display schreiben (ohne vorheriges Clear → schnell)
static void epd_push(bool with_clear) {
    epd_poweron();
    if (with_clear) {
        epd_clear();
        delay(200);  // Longer delay for clear to complete properly
    }
    epd_draw_grayscale_image(epd_full_screen(), fb);
    delay(100);  // Add delay after drawing
    epd_poweroff_all();  // Use poweroff_all for complete power down
}

// ── Öffentliche API ───────────────────────────────────────────────────────────

void display_init() {
    epd_init();
    fb = (uint8_t *)ps_malloc(EPD_WIDTH * EPD_HEIGHT / 2);
    if (fb) {
        fb_clear();
    }
    s_small_text_buf = (uint8_t *)heap_caps_malloc((EPD_WIDTH / 2) * 70,
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void display_boot_msg(const char *line1, const char *line2) {
    fb_clear();
    draw_text(line1, 40, 260);
    if (line2) draw_text(line2, 40, 320);
    epd_push(/*with_clear=*/true);
}

void display_full_refresh(const VictronData &d, bool wifi_ok, bool mqtt_ok, const char *ip,
                          int menu_sel, const int8_t *relay_st) {
    fb_clear();
    render_data(d);
    render_status(wifi_ok, mqtt_ok, ip, menu_sel, relay_st);
    epd_push(/*with_clear=*/true);
}

void display_menu_strip_update(bool wifi_ok, bool mqtt_ok, const char *ip, int menu_sel,
                               const int8_t *relay_st) {
    fb_clear_rect(0, STATUS_TOP, EPD_WIDTH, EPD_HEIGHT - STATUS_TOP);
    render_status(wifi_ok, mqtt_ok, ip, menu_sel, relay_st);
    Rect_t area = { 0, STATUS_TOP, EPD_WIDTH, EPD_HEIGHT - STATUS_TOP };
    epd_push_region(area);
}

void display_partial_update(const VictronData &d, const VictronData &last_d,
                            bool wifi_ok, bool mqtt_ok, const char *ip) {
    char buf[32];
    bool any_update = false;

    // SOC Prozent aktualisieren  
    if (fabsf(d.soc - last_d.soc) >= 5.0f) {  // Use literal value instead of constant
        fb_clear_rect(AREA_SOC_PERCENT.x, AREA_SOC_PERCENT.y, AREA_SOC_PERCENT.width, AREA_SOC_PERCENT.height);
        if (d.soc >= 0.0f) snprintf(buf, sizeof(buf), "%.0f %%", (double)d.soc);
        else                snprintf(buf, sizeof(buf), "-- %%");
        draw_text(buf, EPD_WIDTH - 140, 75);
        epd_push_region(AREA_SOC_PERCENT);
        any_update = true;
    }

    // Spannung aktualisieren
    if (fabsf(d.voltage - last_d.voltage) >= 0.5f) {  // Use literal value instead of constant
        fb_clear_rect(AREA_VOLTAGE.x, AREA_VOLTAGE.y, AREA_VOLTAGE.width, AREA_VOLTAGE.height);
        ftoa1(d.voltage, buf, sizeof(buf));
        strncat(buf, " V", sizeof(buf) - strlen(buf) - 1);
        draw_text(buf, COL_MID + 120, 60);
        epd_push_region(AREA_VOLTAGE);
        any_update = true;
    }

    // Strom aktualisieren
    if (fabsf(d.current - last_d.current) >= 0.5f) {  // Use literal value instead of constant
        fb_clear_rect(AREA_CURRENT.x, AREA_CURRENT.y, AREA_CURRENT.width, AREA_CURRENT.height);
        ftoa1(d.current, buf, sizeof(buf));
        strncat(buf, " A", sizeof(buf) - strlen(buf) - 1);
        draw_text(buf, COL_LEFT, 185);
        epd_push_region(AREA_CURRENT);
        any_update = true;
    }

    // SOC Balken nur bei größeren Änderungen aktualisieren (teuer)
    if (fabsf(d.soc - last_d.soc) >= 10.0f) {  // Use literal value (SOC threshold * 2)
        fb_clear_rect(AREA_SOC_BAR.x, AREA_SOC_BAR.y, AREA_SOC_BAR.width, AREA_SOC_BAR.height);
        draw_soc_bar(COL_LEFT, 80, EPD_WIDTH - 40, 60, d.soc);
        epd_push_region(AREA_SOC_BAR);
        any_update = true;
    }

    // Solar Leistung aktualisieren
    if (fabsf(d.solar_w - last_d.solar_w) >= 5.0f) {  // Use literal value instead of constant
        fb_clear_rect(AREA_SOLAR.x, AREA_SOLAR.y, AREA_SOLAR.width, AREA_SOLAR.height);
        ftoa1(d.solar_w, buf, sizeof(buf));
        strncat(buf, " W", sizeof(buf) - strlen(buf) - 1);
        draw_text(buf, COL_LEFT, 315);
        epd_push_region(AREA_SOLAR);
        any_update = true;
    }

    // Verbrauch aktualisieren
    if (fabsf(d.load_w - last_d.load_w) >= 5.0f) {  // Use literal value instead of constant
        fb_clear_rect(AREA_LOAD.x, AREA_LOAD.y, AREA_LOAD.width, AREA_LOAD.height);
        ftoa1(d.load_w, buf, sizeof(buf));
        strncat(buf, " W", sizeof(buf) - strlen(buf) - 1);
        draw_text(buf, COL_MID + 20, 315);
        epd_push_region(AREA_LOAD);
        any_update = true;
    }

    // Temperaturen aktualisieren (alle zusammen, da sie nah beieinander sind)
    bool temp_changed = fabsf(d.temp_aussen - last_d.temp_aussen) >= 0.5f ||
                       fabsf(d.temp_innen - last_d.temp_innen) >= 0.5f ||
                       fabsf(d.temp_fridge - last_d.temp_fridge) >= 0.5f ||
                       fabsf(d.temp_cabinet - last_d.temp_cabinet) >= 0.5f;
    
    if (temp_changed) {
        fb_clear_rect(AREA_TEMPS.x, AREA_TEMPS.y, AREA_TEMPS.width, AREA_TEMPS.height);
        
        const int temp_col_w = (EPD_WIDTH - 2 * COL_LEFT) / 4;
        constexpr int TEMP_BAND_Y0       = 340;
        constexpr int TEMP_BAND_Y1       = 400;
        constexpr int TEMP_BAND_MID      = (TEMP_BAND_Y0 + TEMP_BAND_Y1) / 2;
        constexpr int TEMP_VERTICAL_BIAS = 9;
        constexpr int TEMP_BASE_GAP        = 24;
        const int mid    = TEMP_BAND_MID + TEMP_VERTICAL_BIAS;
        const int y_temp_lbl = mid - TEMP_BASE_GAP / 2;
        const int y_temp_val = mid + TEMP_BASE_GAP / 2;
        
        struct { float val; const char *name; } temps[] = {
            { d.temp_aussen,  "Aussen" },
            { d.temp_innen,   "Innen" },
            { d.temp_fridge,  "Kuehl" },
            { d.temp_cabinet, "Schrank" },
        };
        
        for (size_t i = 0; i < sizeof(temps) / sizeof(temps[0]); i++) {
            int x = COL_LEFT + (int)i * temp_col_w;
            draw_text_small(temps[i].name, x, y_temp_lbl);
            char tmp[16];
            ftoa1(temps[i].val, tmp, sizeof(tmp));
            snprintf(buf, sizeof(buf), "%s \xb0""C", tmp);
            draw_text_small(buf, x, y_temp_val);
        }
        
        epd_push_region(AREA_TEMPS);
        any_update = true;
    }
}

void display_prov_screen() {
    fb_clear();
    draw_text("SETUP-MODUS", COL_LEFT, 160);
    draw_text("Mit WLAN verbinden:", COL_LEFT, 240);
    
    char ssid_line[64];
    snprintf(ssid_line, sizeof(ssid_line), "  %s", AP_SSID);
    draw_text(ssid_line, COL_LEFT, 295);
    
    char pass_line[64];
    snprintf(pass_line, sizeof(pass_line), "  Passwort: %s", AP_PASSWORD);
    draw_text(pass_line, COL_LEFT, 345);
    
    draw_text("Browser: http://192.168.4.1", COL_LEFT, 430);
    epd_push(/*with_clear=*/true);
}
