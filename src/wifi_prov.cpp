#include "wifi_prov.h"
#include "config.h"
#include "display.h"
#include <LittleFS.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

// ─────────────────────────────────────────────────────────────────────────────
// Config-Storage
// ─────────────────────────────────────────────────────────────────────────────

bool config_load(AppConfig &cfg) {
    if (!LittleFS.exists(CONFIG_FILE)) return false;
    File f = LittleFS.open(CONFIG_FILE, "r");
    if (!f) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return false;

    cfg.wifi_ssid  = doc["ssid"]      | "";
    cfg.wifi_pass  = doc["pass"]      | "";
    cfg.mqtt_host  = doc["mqtt_host"] | "";
    cfg.mqtt_port  = doc["mqtt_port"] | (uint16_t)MQTT_PORT_DEFAULT;
    cfg.mqtt_user  = doc["mqtt_user"] | "";
    cfg.mqtt_pass  = doc["mqtt_pass"] | "";

    // Minimal-Check: ohne diese zwei Felder kann das Gerät nicht arbeiten
    return cfg.wifi_ssid.length() > 0 && cfg.mqtt_host.length() > 0;
}

void config_save(const AppConfig &cfg) {
    JsonDocument doc;
    doc["ssid"]      = cfg.wifi_ssid;
    doc["pass"]      = cfg.wifi_pass;
    doc["mqtt_host"] = cfg.mqtt_host;
    doc["mqtt_port"] = cfg.mqtt_port;
    doc["mqtt_user"] = cfg.mqtt_user;
    doc["mqtt_pass"] = cfg.mqtt_pass;

    File f = LittleFS.open(CONFIG_FILE, "w");
    serializeJson(doc, f);
    f.close();
}

void config_erase() {
    LittleFS.remove(CONFIG_FILE);
}

// ─────────────────────────────────────────────────────────────────────────────
// WiFi-Verbindung
// ─────────────────────────────────────────────────────────────────────────────

bool wifi_connect(const AppConfig &cfg, uint32_t timeout_ms, void (*while_waiting)(void)) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfg.wifi_ssid.c_str(), cfg.wifi_pass.c_str());

    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - t0 > timeout_ms) return false;
        if (while_waiting) while_waiting();
        delay(10);
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// AP-Provisioning
//
// HTML wird als PROGMEM-String gespeichert, da er zu gross für den Stack ist.
// Der Webserver läuft in einer Endlosschleife; das Gerät startet nach dem
// Speichern automatisch neu. Damit ist kein Rückgabewert nötig.
// ─────────────────────────────────────────────────────────────────────────────

static const char PROV_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html><html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Camping Display Setup</title>
<style>
body{font-family:sans-serif;max-width:440px;margin:30px auto;padding:0 16px;background:#f4f4f4}
h2{color:#1a6;margin-bottom:4px}
p.sub{color:#888;font-size:13px;margin-top:0}
label{display:block;font-size:13px;color:#555;margin-top:12px;font-weight:bold}
input{width:100%;padding:9px;box-sizing:border-box;border:1px solid #ccc;
      border-radius:5px;font-size:15px;background:#fff}
hr{border:none;border-top:1px solid #ddd;margin:18px 0}
button{margin-top:18px;width:100%;padding:13px;background:#1a6;color:#fff;
       border:none;border-radius:5px;font-size:16px;cursor:pointer}
button:active{background:#148}
</style></head><body>
<h2>Camping Display</h2>
<p class="sub">WLAN &amp; MQTT konfigurieren</p>
<form action="/save" method="POST">
  <label>WLAN SSID</label>
  <input name="ssid" required autocomplete="off">
  <label>WLAN Passwort</label>
  <input name="pass" type="password" autocomplete="off">
  <hr>
  <label>MQTT Server (IP oder Hostname)</label>
  <input name="mqtt_host" required placeholder="192.168.1.100">
  <label>MQTT Port</label>
  <input name="mqtt_port" type="number" value="1883" min="1" max="65535">
  <label>MQTT Benutzername (optional)</label>
  <input name="mqtt_user" autocomplete="off">
  <label>MQTT Passwort (optional)</label>
  <input name="mqtt_pass" type="password" autocomplete="off">
  <button type="submit">Speichern &amp; Neustart</button>
</form>
<hr>
<p style="font-size:12px;color:#666;margin-bottom:6px">Gespeicherte WLAN-/MQTT-Daten loeschen (wie Werkreset):</p>
<form action="/erase" method="POST" onsubmit="return confirm('Alle Einstellungen wirklich loeschen?');">
<button type="submit" style="background:#a53;margin-top:6px">Konfiguration loeschen &amp; Neustart</button>
</form>
</body></html>
)HTML";

static const char PROV_ERASED[] PROGMEM = R"HTML(
<!DOCTYPE html><html><head><meta charset="utf-8"><title>Geloescht</title>
<style>body{font-family:sans-serif;text-align:center;padding-top:60px;color:#a53}</style>
</head><body><h2>Konfiguration geloescht</h2><p>Geraet startet neu …</p></body></html>
)HTML";

static const char PROV_SAVED[] PROGMEM = R"HTML(
<!DOCTYPE html><html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Gespeichert</title>
<style>body{font-family:sans-serif;text-align:center;padding-top:60px;color:#1a6}</style>
</head><body>
<h2>Gespeichert!</h2>
<p>Das Gerat startet neu und verbindet sich...</p>
</body></html>
)HTML";

void wifi_prov_start(void (*while_idle)(void)) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    // AP-IP ist standardmässig 192.168.4.1

    WebServer server(80);

    server.on("/", HTTP_GET, [&]() {
        server.send_P(200, "text/html", PROV_HTML);
    });

    server.on("/save", HTTP_POST, [&]() {
        AppConfig cfg;
        cfg.wifi_ssid  = server.arg("ssid");
        cfg.wifi_pass  = server.arg("pass");
        cfg.mqtt_host  = server.arg("mqtt_host");
        cfg.mqtt_port  = server.arg("mqtt_port").toInt();
        cfg.mqtt_user  = server.arg("mqtt_user");
        cfg.mqtt_pass  = server.arg("mqtt_pass");
        config_save(cfg);
        server.send_P(200, "text/html", PROV_SAVED);
        display_boot_msg("Gespeichert", "Neustart …");
        delay(800);
        ESP.restart();
    });

    server.on("/erase", HTTP_POST, [&]() {
        config_erase();
        server.send_P(200, "text/html", PROV_ERASED);
        display_boot_msg("Konfig geloescht", "Neustart …");
        delay(800);
        ESP.restart();
    });

    // Captive-Portal-Verhalten: alle unbekannten URLs zur Startseite
    server.onNotFound([&]() {
        server.sendHeader("Location", "/");
        server.send(302);
    });

    server.begin();
    while (true) {
        server.handleClient();
        if (while_idle) while_idle();
        delay(2);
    }
}
