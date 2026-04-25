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
//   │  23.4 A                                                      │ y=185
//   ├─────────────────────────────────────────────────────────────┤ y=200
//   │  SOLAR                        │  VERBRAUCH                  │ y=255
//   │  342 W                        │  128 W                      │ y=315
//   ├─────────────────────────────────────────────────────────────┤ y=340
//   │  Aussen … Geraeteschrank (klein), zwei Zeilen leicht nach unten versetzt
//   │  °C zweite Zeile                                              │ y≈367 / 391
//   ├─────────────────────────────────────────────────────────────┤ y=400
//   │  WiFi: …   MQTT: OK  (eine Zeile, kompakt)                   │ y=430
//   │  [R1 AN] [R2 aus] [R3 --] [Bild neu]                        │ y=458–514
//   └─────────────────────────────────────────────────────────────┘ y=540
// ─────────────────────────────────────────────────────────────────────────────

// Alle Victron-Messwerte. Felder mit -1 bedeuten "noch kein Wert empfangen"
// und werden als "--" dargestellt.
struct VictronData {
    float soc          = -1;   // Ladezustand         [%]
    float voltage      = -1;   // Batteriespannung     [V]
    float current      = -1;   // Strom (+=laden)      [A]
    float solar_w      = -1;   // Solarleistung        [W]
    float load_w       = -1;   // Verbrauch            [W]
    float temp_aussen  = -1;   // Aussentemperatur     [°C]
    float temp_innen   = -1;   // Innentemperatur      [°C]
    float temp_fridge  = -1;   // Kuehlschrank         [°C]
    float temp_cabinet = -1;   // Geraeteschrank       [°C]
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
