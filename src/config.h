#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// config.h – Zentrale Konstanten für das Camping-Display
//
// Alle anpassbaren Werte stehen hier. Nichts davon muss im Quellcode gesucht
// werden. MQTT-Topics müssen mit dem Node-RED Flow übereinstimmen.
// ─────────────────────────────────────────────────────────────────────────────

// ── GPIO-Pins (LilyGo T5 4.7" ESP32-S3 V2.3) ────────────────
// Quelle: hardware.md / src/utilities.h im LilyGo-EPD47-Repo
// BOARD_SDA (18) und BOARD_SCL (17) werden von utilities.h definiert (via epd_driver.h)
#define PIN_BUTTON       21   // Kurz: Menue naechster Punkt · Lang: Eintrag ausfuehren
// NICHT GPIO 0 als Taster nutzen: am T5 4.7 = EPD CFG_STR + Boot-Strapping → Bild weg / kein Start
#define PIN_BATT         14   // ADC für Boardspannung (Backup-Akku)

// ── LittleFS Konfigurationsdatei ─────────────────────────────
// JSON mit SSID, WiFi-PW, MQTT-Host, Port, User, PW
#define CONFIG_FILE     "/config.json"

// ── AP-Provisioning ──────────────────────────────────────────
// Das Gerät öffnet dieses WLAN, wenn keine Konfiguration gefunden wird.
// Formular erreichbar unter http://192.168.4.1
#define AP_SSID         "CampingDisplay"
#define AP_PASSWORD     "camping123"

// ── MQTT ─────────────────────────────────────────────────────
#define MQTT_CLIENT_ID      "camping-epaper"   // muss im Broker eindeutig sein
#define MQTT_PORT_DEFAULT   1883

// MPPT-/Batterie-Werte (siehe main.cpp):
//   • TOPIC_TELEMETRY_JSON nicht leer: ein Topic, Payload = JSON-Objekt (UTF-8)
//   • Zusätzlich werden alle unten nicht-leeren flachen Topics abonniert (Überlagerung),
//     z. B. Solar nur auf lars/mppt/solarW während der Rest im JSON steht.
//   • TOPIC_TELEMETRY_JSON leer: nur flache Topics.
#define TOPIC_TELEMETRY_JSON  "camping/telemetry/mppt"

// Flache Topics (nur aktiv, wenn TOPIC_TELEMETRY_JSON "")
#define TOPIC_SOC           "lars/mppt/soc"      // SOC 0–100 [%]
#define TOPIC_VOLTAGE       "lars/mppt/batV"     // Batteriespannung [V]
#define TOPIC_SOLAR_POWER   "lars/mppt/solarW"   // Solarleistung [W]

// Optional: Topic-String leer lassen "" → kein MQTT-Subscribe, Anzeige bleibt „--"
#define TOPIC_CURRENT       ""   // z. B. "lars/mppt/batA" – positiv=laden [A]
#define TOPIC_LOAD_POWER    ""   // z. B. "lars/mppt/loadW" [W]

// Umgebungstemperaturen (Victron Temperature Sensor, node-red-contrib-victron)
#define TOPIC_TEMP_AUSSEN   "camping/temp/aussen"
#define TOPIC_TEMP_INNEN    "camping/temp/innen"
#define TOPIC_TEMP_FRIDGE   "camping/temp/kuehlschrank"
#define TOPIC_TEMP_CABINET  "camping/temp/geraeteschrank"

// Relais-Befehle (Publish bei Menue „Relais n" + Langdruck). Payload siehe unten.
// Leerer String = keine MQTT-Aktion fuer diesen Eintrag.
#define TOPIC_RELAY1_CMD    "lars/relais/1/cmd"
#define TOPIC_RELAY2_CMD    "lars/relais/2/cmd"
#define TOPIC_RELAY3_CMD    "lars/relais/3/cmd"
#define MQTT_RELAY_PAYLOAD  "toggle"   // z. B. toggle / 1 / 0 – an Node-RED anpassen

// Relais-Istzustand (Subscribe): Payload z. B. 1/0, on/off, an/aus — leer = kein Subscribe
#define TOPIC_RELAY1_STATE  "lars/relais/1/state"
#define TOPIC_RELAY2_STATE  "lars/relais/2/state"
#define TOPIC_RELAY3_STATE  "lars/relais/3/state"

// Unteres Menue: Anzahl Eintraege (Relais1–3 + Bild neu)
#define MENU_ITEM_COUNT     4
// Langdruck-Schwelle fuer „Auswaehlen" [ms] (Button2 long-click detected)
#define MENU_LONG_PRESS_MS  900UL

// ── Refresh-Timing ────────────────────────────────────────────
// Nur Vollbild (epd_clear): Partial-Update fuer Messwerte entfaellt (Ghosting/Panel).
#define DATA_REFRESH_INTERVAL_MS  30000UL    // Minimaler Abstand zwischen Vollbild-Refreshes [ms]
#define FULL_REFRESH_INTERVAL     600000UL   // Periodisches Vollbild gegen Ghosting [ms] (10 min)

// ── Schwellenwerte fuer Display-Refresh ───────────────────────
// Refresh nur wenn sich mindestens ein Wert um diesen Betrag geaendert hat.
#define DISPLAY_THRESHOLD_SOC    5.0f   // %
#define DISPLAY_THRESHOLD_VOLT   0.5f   // V
#define DISPLAY_THRESHOLD_CURR   0.5f   // A
#define DISPLAY_THRESHOLD_SOLAR  5.0f   // W
#define DISPLAY_THRESHOLD_LOAD   5.0f   // W
#define DISPLAY_THRESHOLD_TEMP   0.5f   // °C
