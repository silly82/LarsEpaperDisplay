#pragma once
#include <Arduino.h>

// ─────────────────────────────────────────────────────────────────────────────
// display.h – E-Paper Rendering (LilyGo T5 4.7" / ED047TC1 / 960×540 px)
//
//   display_full_refresh()    epd_clear() + komplettes Layout (Messwerte + Menue)
//   display_menu_strip_update() nur untere Leiste (Menuewechsel / Relais-State)
//
// Display-Layout (960×540):
//
//   ┌─────────────────────────────────────────────────────────────┐ y=0
//   │  AKKU                                            xx.x V     │
//   │  [████████████████████████████░░░░░░░░]  78 %              │ y=80
//   │  23.4 A                                  24.0 °C            │ y=185
//   ├─────────────────────────────────────────────────────────────┤ y=220
//   │  SOLAR                        │  VERBRAUCH                  │ y=290
//   │  342 W                        │  128 W                      │ y=360
//   ├─────────────────────────────────────────────────────────────┤ y=400
//   │  WiFi: 192.168.1.42           MQTT: OK                      │ y=460
//   │  WiFi/MQTT · Menue R1–R3 (AN/aus) + Bild neu                   │ y=400–540
//   └─────────────────────────────────────────────────────────────┘ y=540
// ─────────────────────────────────────────────────────────────────────────────

// Alle Victron-Messwerte. Felder mit -1 bedeuten "noch kein Wert empfangen"
// und werden als "--" dargestellt.
struct VictronData {
    float soc     = -1;   // Ladezustand         [%]
    float voltage = -1;   // Batteriespannung     [V]
    float current = -1;   // Strom (+=laden)      [A]
    float solar_w = -1;   // Solarleistung        [W]
    float load_w  = -1;   // Verbrauch            [W]
    float temp    = -1;   // Batterietemperatur   [°C]
};

// Relais-Anzeige: -1 = unbekannt, 0 = aus, 1 = an
#define RELAY_STATE_COUNT 3

// EPD und PSRAM-Framebuffer initialisieren. Muss als erstes aufgerufen werden.
void display_init();

// Einfacher Text-Bildschirm während Boot / Verbindungsaufbau.
// line2 ist optional (nullptr = nur eine Zeile).
void display_boot_msg(const char *line1, const char *line2 = nullptr);

// relay_st: RELAY_STATE_COUNT Eintraege (-1/0/1)
void display_full_refresh(const VictronData &d, bool mqtt_ok, const char *ip, int menu_sel,
                          const int8_t *relay_st);

void display_menu_strip_update(bool mqtt_ok, const char *ip, int menu_sel,
                               const int8_t *relay_st);

// Provisioning-Bildschirm: zeigt AP-SSID, Passwort und URL an.
void display_prov_screen();
