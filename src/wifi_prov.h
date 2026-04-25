#pragma once
#include <Arduino.h>

// ─────────────────────────────────────────────────────────────────────────────
// wifi_prov.h – WiFi-Verbindung und AP-Provisioning
//
// Ablauf beim ersten Start:
//   1. config_load() → Datei nicht vorhanden
//   2. display_prov_screen() zeigt WLAN-Name am Display
//   3. wifi_prov_start() öffnet AP + Webserver (blockiert)
//   4. User füllt Formular aus → config_save() → ESP.restart()
//   5. Beim Neustart: config_load() → wifi_connect() → MQTT
// ─────────────────────────────────────────────────────────────────────────────

// Enthält alle persistierten Verbindungsparameter.
// Wird aus /config.json geladen und nach dem Provisioning-Formular gespeichert.
struct AppConfig {
    String   wifi_ssid;
    String   wifi_pass;
    String   mqtt_host;
    uint16_t mqtt_port = 1883;
    String   mqtt_user;   // leer = kein Auth
    String   mqtt_pass;
};

// Lädt Konfiguration aus LittleFS.
// Gibt false zurück wenn Datei fehlt oder SSID/mqtt_host leer sind.
bool config_load(AppConfig &cfg);

// Schreibt Konfiguration als JSON in LittleFS.
void config_save(const AppConfig &cfg);

// Löscht die Konfigurationsdatei (Werkreset z. B. per Flash/Tool).
void config_erase();

// Verbindet ESP mit dem gespeicherten WLAN.
// while_waiting: optional (z. B. btn.loop), damit Taster-Logik während des Wartens läuft.
bool wifi_connect(const AppConfig &cfg, uint32_t timeout_ms = 15000,
                  void (*while_waiting)(void) = nullptr);

// Öffnet AP "CampingDisplay" und startet den Webserver.
// while_idle: optional (z. B. btn.loop) in der AP-Schleife.
void wifi_prov_start(void (*while_idle)(void) = nullptr);
