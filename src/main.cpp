#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Button2.h>
#include <epd_driver.h>
#include <string.h>
#include <ctype.h>

#include "config.h"
#include "display.h"
#include "wifi_prov.h"

// ─────────────────────────────────────────────────────────────────────────────
// Globaler Zustand
// ─────────────────────────────────────────────────────────────────────────────

static AppConfig    cfg;
static VictronData  victron;
static char         ip_str[20]  = "--";

static WiFiClient   wifiClient;
static PubSubClient mqtt(wifiClient);

static Button2      btn;

static int          menu_sel = 0;

static int8_t       relay_state[RELAY_STATE_COUNT] = { -1, -1, -1 };
static volatile bool menu_strip_dirty                = false;

static uint32_t     last_data_ms  = 0;
static uint32_t     last_ghost_ms = 0;
static bool         full_refresh_needed = true;

// ─────────────────────────────────────────────────────────────────────────────
// MQTT
// ─────────────────────────────────────────────────────────────────────────────

static int8_t parse_relay_payload(char *s) {
    // trim
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }
    while (*s && isspace((unsigned char)*s)) s++;

    if (!*s) return -1;

    char t[20];
    strncpy(t, s, sizeof(t) - 1);
    t[sizeof(t) - 1] = '\0';
    for (char *p = t; *p; p++) *p = (char)tolower((unsigned char)*p);

    if (strcmp(t, "1") == 0 || strcmp(t, "on") == 0 || strcmp(t, "true") == 0 || strcmp(t, "an") == 0)
        return 1;
    if (strcmp(t, "0") == 0 || strcmp(t, "off") == 0 || strcmp(t, "false") == 0 || strcmp(t, "aus") == 0)
        return 0;

    float f = atof(t);
    if (f >= 0.5f) return 1;
    if (f < 0.5f && strchr("0123456789", t[0])) return 0;
    return -1;
}

static void mqtt_callback(char *topic, byte *payload, unsigned int len) {
    char buf[32];
    if (len >= sizeof(buf)) return;
    memcpy(buf, payload, len);
    buf[len] = '\0';

    if (TOPIC_RELAY1_STATE[0] && strcmp(topic, TOPIC_RELAY1_STATE) == 0) {
        relay_state[0] = parse_relay_payload(buf);
        menu_strip_dirty = true;
        return;
    }
    if (TOPIC_RELAY2_STATE[0] && strcmp(topic, TOPIC_RELAY2_STATE) == 0) {
        relay_state[1] = parse_relay_payload(buf);
        menu_strip_dirty = true;
        return;
    }
    if (TOPIC_RELAY3_STATE[0] && strcmp(topic, TOPIC_RELAY3_STATE) == 0) {
        relay_state[2] = parse_relay_payload(buf);
        menu_strip_dirty = true;
        return;
    }

    float val = atof(buf);

    if (TOPIC_SOC[0] && strcmp(topic, TOPIC_SOC) == 0) victron.soc = val;
    else if (TOPIC_VOLTAGE[0] && strcmp(topic, TOPIC_VOLTAGE) == 0) victron.voltage = val;
    else if (TOPIC_CURRENT[0] && strcmp(topic, TOPIC_CURRENT) == 0) victron.current = val;
    else if (TOPIC_SOLAR_POWER[0] && strcmp(topic, TOPIC_SOLAR_POWER) == 0) victron.solar_w = val;
    else if (TOPIC_LOAD_POWER[0] && strcmp(topic, TOPIC_LOAD_POWER) == 0) victron.load_w = val;
    else if (TOPIC_TEMP[0] && strcmp(topic, TOPIC_TEMP) == 0) victron.temp = val;
}

static bool mqtt_connect() {
    mqtt.setServer(cfg.mqtt_host.c_str(), cfg.mqtt_port);
    mqtt.setCallback(mqtt_callback);

    bool ok;
    if (cfg.mqtt_user.length() > 0) {
        ok = mqtt.connect(MQTT_CLIENT_ID,
                          cfg.mqtt_user.c_str(), cfg.mqtt_pass.c_str());
    } else {
        ok = mqtt.connect(MQTT_CLIENT_ID);
    }

    if (ok) {
        if (TOPIC_SOC[0]) mqtt.subscribe(TOPIC_SOC);
        if (TOPIC_VOLTAGE[0]) mqtt.subscribe(TOPIC_VOLTAGE);
        if (TOPIC_CURRENT[0]) mqtt.subscribe(TOPIC_CURRENT);
        if (TOPIC_SOLAR_POWER[0]) mqtt.subscribe(TOPIC_SOLAR_POWER);
        if (TOPIC_LOAD_POWER[0]) mqtt.subscribe(TOPIC_LOAD_POWER);
        if (TOPIC_TEMP[0]) mqtt.subscribe(TOPIC_TEMP);
        if (TOPIC_RELAY1_STATE[0]) mqtt.subscribe(TOPIC_RELAY1_STATE);
        if (TOPIC_RELAY2_STATE[0]) mqtt.subscribe(TOPIC_RELAY2_STATE);
        if (TOPIC_RELAY3_STATE[0]) mqtt.subscribe(TOPIC_RELAY3_STATE);
        Serial.println("MQTT verbunden");
    } else {
        Serial.printf("MQTT Fehler: %d\n", mqtt.state());
    }
    return ok;
}

static bool mqtt_publish_relay(const char *topic) {
    if (!topic || !topic[0]) {
        Serial.println("Relais: kein Topic konfiguriert");
        return false;
    }
    if (!mqtt.connected()) {
        Serial.println("Relais: MQTT nicht verbunden");
        return false;
    }
    bool ok = mqtt.publish(topic, MQTT_RELAY_PAYLOAD, false);
    Serial.printf("Relais publish %s -> %s\n", topic, ok ? "OK" : "fehlgeschlagen");
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
// Taster
// ─────────────────────────────────────────────────────────────────────────────

static void on_menu_advance(Button2 &b) {
    menu_sel = (menu_sel + 1) % MENU_ITEM_COUNT;
    if (WiFi.status() == WL_CONNECTED) {
        display_menu_strip_update(mqtt.connected(), ip_str, menu_sel, relay_state);
    }
}

static void on_menu_select(Button2 &b) {
    switch (menu_sel) {
    case 0:
        mqtt_publish_relay(TOPIC_RELAY1_CMD);
        break;
    case 1:
        mqtt_publish_relay(TOPIC_RELAY2_CMD);
        break;
    case 2:
        mqtt_publish_relay(TOPIC_RELAY3_CMD);
        break;
    case 3:
    default:
        full_refresh_needed = true;
        Serial.println("Menue: Vollbild (Bild neu)");
        break;
    }
}

static void idle_btn() { btn.loop(); }

// ─────────────────────────────────────────────────────────────────────────────
// Setup
// ─────────────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);
    delay(500);

    Serial.printf("Heap: %u  PSRAM: %u\n", ESP.getFreeHeap(), ESP.getFreePsram());

    LittleFS.begin(true);
    display_init();
    display_boot_msg("Camping Display", "Starte...");

    btn.begin(PIN_BUTTON, INPUT_PULLUP, true);
    btn.setClickHandler(on_menu_advance);
    btn.setLongClickTime(MENU_LONG_PRESS_MS);
    btn.setLongClickDetectedHandler(on_menu_select);

    if (!config_load(cfg)) {
        Serial.println("Keine Config – AP-Modus");
        display_prov_screen();
        wifi_prov_start(idle_btn);
        return;
    }

    display_boot_msg("Verbinde WLAN...", cfg.wifi_ssid.c_str());

    if (!wifi_connect(cfg, 15000, idle_btn)) {
        Serial.println("WiFi fehlgeschlagen – AP-Modus");
        display_boot_msg("WLAN-Fehler", "Setup-Modus...");
        delay(2000);
        display_prov_screen();
        wifi_prov_start(idle_btn);
        return;
    }

    WiFi.localIP().toString().toCharArray(ip_str, sizeof(ip_str));
    Serial.printf("WiFi OK: %s\n", ip_str);

    display_boot_msg("Verbinde MQTT...", cfg.mqtt_host.c_str());
    mqtt_connect();

    Serial.println("Setup fertig");
}

// ─────────────────────────────────────────────────────────────────────────────
// Loop
// ─────────────────────────────────────────────────────────────────────────────

void loop() {
    btn.loop();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi weg – reconnecte...");
        wifi_connect(cfg, 10000, idle_btn);
        return;
    }

    if (!mqtt.connected()) {
        static uint32_t last_mqtt_attempt = 0;
        uint32_t t = millis();
        if (t - last_mqtt_attempt > 5000) {
            last_mqtt_attempt = t;
            mqtt_connect();
        }
    }
    mqtt.loop();

    uint32_t now      = millis();
    bool     mqtt_ok  = mqtt.connected();
    bool     data_due = (now - last_data_ms >= DATA_REFRESH_INTERVAL_MS);
    bool     ghost_due = (now - last_ghost_ms >= FULL_REFRESH_INTERVAL);

    bool do_full = full_refresh_needed || data_due || ghost_due;
    if (do_full) {
        display_full_refresh(victron, mqtt_ok, ip_str, menu_sel, relay_state);
        last_data_ms = now;
        if (ghost_due || full_refresh_needed) last_ghost_ms = now;
        full_refresh_needed = false;
        menu_strip_dirty = false;
    } else if (menu_strip_dirty) {
        display_menu_strip_update(mqtt_ok, ip_str, menu_sel, relay_state);
        menu_strip_dirty = false;
    }
}
