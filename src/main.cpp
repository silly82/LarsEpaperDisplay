#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Button2.h>
#include <ArduinoJson.h>
#include <epd_driver.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "config.h"
#include "display.h"
#include "wifi_prov.h"

// ─────────────────────────────────────────────────────────────────────────────
// Globaler Zustand
// ─────────────────────────────────────────────────────────────────────────────

static AppConfig    cfg;
static VictronData  victron;
static VictronData  last_shown;   // Snapshot des zuletzt angezeigten Zustands
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

static char         mqtt_json_buf[768];

// ─────────────────────────────────────────────────────────────────────────────
// MQTT
// ─────────────────────────────────────────────────────────────────────────────

// UTF-8-BOM überspringen (manche Publisher vor "{")
static const char *json_skip_bom(const char *s) {
    if (s && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB &&
        (unsigned char)s[2] == 0xBF)
        return s + 3;
    return s;
}

// Messwerte oft unter data/mppt/payload verschachtelt
static JsonObjectConst telemetry_root(const JsonDocument &doc) {
    if (doc["data"].is<JsonObject>())
        return doc["data"].as<JsonObjectConst>();
    if (doc["mppt"].is<JsonObject>())
        return doc["mppt"].as<JsonObjectConst>();
    if (doc["payload"].is<JsonObject>())
        return doc["payload"].as<JsonObjectConst>();
    return doc.as<JsonObjectConst>();
}

static bool json_to_float(JsonVariantConst v, float &out) {
    if (v.isNull()) return false;
    if (v.is<float>() || v.is<double>()) {
        out = v.as<float>();
        return !isnan(out) && !isinf(out);
    }
    if (v.is<int>() || v.is<long>() || v.is<unsigned long>()) {
        out = static_cast<float>(v.as<double>());
        return true;
    }
    if (v.is<const char *>()) {
        const char *s = v.as<const char *>();
        if (!s || !*s) return false;
        char *end = nullptr;
        out = strtof(s, &end);
        return end != s && !isnan(out) && !isinf(out);
    }
    return false;
}

static bool threshold_exceeded() {
    auto chg = [](float prev, float curr, float thr) -> bool {
        if (prev < 0.0f) return curr >= 0.0f;   // erster empfangener Wert
        return fabsf(curr - prev) >= thr;
    };
    return chg(last_shown.soc,          victron.soc,          DISPLAY_THRESHOLD_SOC)
        || chg(last_shown.voltage,      victron.voltage,      DISPLAY_THRESHOLD_VOLT)
        || chg(last_shown.current,      victron.current,      DISPLAY_THRESHOLD_CURR)
        || chg(last_shown.solar_w,      victron.solar_w,      DISPLAY_THRESHOLD_SOLAR)
        || chg(last_shown.load_w,       victron.load_w,       DISPLAY_THRESHOLD_LOAD)
        || chg(last_shown.temp_aussen,  victron.temp_aussen,  DISPLAY_THRESHOLD_TEMP)
        || chg(last_shown.temp_innen,   victron.temp_innen,   DISPLAY_THRESHOLD_TEMP)
        || chg(last_shown.temp_fridge,  victron.temp_fridge,  DISPLAY_THRESHOLD_TEMP)
        || chg(last_shown.temp_cabinet, victron.temp_cabinet, DISPLAY_THRESHOLD_TEMP);
}

static void apply_telemetry_json(const char *json) {
    JsonDocument doc;
    json = json_skip_bom(json);
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.printf("Telemetrie-JSON: %s\n", err.c_str());
        return;
    }

    JsonObjectConst root = telemetry_root(doc);
    bool any_field = false;
    float v;

    auto apply2 = [&](const char *key_a, const char *key_b, float &dest) {
        if (json_to_float(root[key_a], v) || (key_b && json_to_float(root[key_b], v))) {
            dest      = v;
            any_field = true;
        }
    };

    apply2("soc", nullptr, victron.soc);
    apply2("voltage", "bat_v", victron.voltage);
    {
        static const char *const solar_keys[] = {
            "solar_w", "solarW", "pv_power", "solar_power", "yield_power",
            "ppv", "dc_pv_power", "mppt_power", nullptr,
        };
        for (const char *const *k = solar_keys; *k; k++) {
            if (json_to_float(root[*k], v)) {
                victron.solar_w = v;
                any_field       = true;
                break;
            }
        }
    }
    apply2("current_a", "batA", victron.current);
    apply2("load_w", "loadW", victron.load_w);

    if (any_field || threshold_exceeded()) full_refresh_needed = true;
}

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
    // JSON-Telemetrie zuerst: grosser Payload, eigener Puffer
    if (TOPIC_TELEMETRY_JSON[0] && strcmp(topic, TOPIC_TELEMETRY_JSON) == 0) {
        if (len >= sizeof(mqtt_json_buf)) {
            Serial.printf("MQTT JSON zu lang: %u\n", (unsigned)len);
            return;
        }
        memcpy(mqtt_json_buf, payload, len);
        mqtt_json_buf[len] = '\0';
        apply_telemetry_json(mqtt_json_buf);
        return;
    }

    // Alle anderen Topics haben kurze Payloads (relay state, flat metrics)
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

    if (TOPIC_SOC[0] && strcmp(topic, TOPIC_SOC) == 0) {
        victron.soc = val;
        if (threshold_exceeded()) full_refresh_needed = true;
    } else if (TOPIC_VOLTAGE[0] && strcmp(topic, TOPIC_VOLTAGE) == 0) {
        victron.voltage = val;
        if (threshold_exceeded()) full_refresh_needed = true;
    } else if (TOPIC_CURRENT[0] && strcmp(topic, TOPIC_CURRENT) == 0) {
        victron.current = val;
        if (threshold_exceeded()) full_refresh_needed = true;
    } else if (TOPIC_SOLAR_POWER[0] && strcmp(topic, TOPIC_SOLAR_POWER) == 0) {
        victron.solar_w = val;
        if (threshold_exceeded()) full_refresh_needed = true;
    } else if (TOPIC_LOAD_POWER[0] && strcmp(topic, TOPIC_LOAD_POWER) == 0) {
        victron.load_w = val;
        if (threshold_exceeded()) full_refresh_needed = true;
    }
    else if (TOPIC_TEMP_AUSSEN[0]  && strcmp(topic, TOPIC_TEMP_AUSSEN)  == 0) { victron.temp_aussen  = val; if (threshold_exceeded()) full_refresh_needed = true; }
    else if (TOPIC_TEMP_INNEN[0]   && strcmp(topic, TOPIC_TEMP_INNEN)   == 0) { victron.temp_innen   = val; if (threshold_exceeded()) full_refresh_needed = true; }
    else if (TOPIC_TEMP_FRIDGE[0]  && strcmp(topic, TOPIC_TEMP_FRIDGE)  == 0) { victron.temp_fridge  = val; if (threshold_exceeded()) full_refresh_needed = true; }
    else if (TOPIC_TEMP_CABINET[0] && strcmp(topic, TOPIC_TEMP_CABINET) == 0) { victron.temp_cabinet = val; if (threshold_exceeded()) full_refresh_needed = true; }
}

static bool mqtt_connect() {
    mqtt.setServer(cfg.mqtt_host.c_str(), cfg.mqtt_port);
    mqtt.setCallback(mqtt_callback);
    mqtt.setBufferSize(1024);

    bool ok;
    if (cfg.mqtt_user.length() > 0) {
        ok = mqtt.connect(MQTT_CLIENT_ID,
                          cfg.mqtt_user.c_str(), cfg.mqtt_pass.c_str());
    } else {
        ok = mqtt.connect(MQTT_CLIENT_ID);
    }

    if (ok) {
        if (TOPIC_TELEMETRY_JSON[0]) {
            mqtt.subscribe(TOPIC_TELEMETRY_JSON);
            Serial.printf("MQTT: JSON-Telemetrie %s\n", TOPIC_TELEMETRY_JSON);
        }
        if (TOPIC_SOC[0]) mqtt.subscribe(TOPIC_SOC);
        if (TOPIC_VOLTAGE[0]) mqtt.subscribe(TOPIC_VOLTAGE);
        if (TOPIC_CURRENT[0]) mqtt.subscribe(TOPIC_CURRENT);
        if (TOPIC_SOLAR_POWER[0]) mqtt.subscribe(TOPIC_SOLAR_POWER);
        if (TOPIC_LOAD_POWER[0]) mqtt.subscribe(TOPIC_LOAD_POWER);
        if (TOPIC_TEMP_AUSSEN[0])  mqtt.subscribe(TOPIC_TEMP_AUSSEN);
        if (TOPIC_TEMP_INNEN[0])   mqtt.subscribe(TOPIC_TEMP_INNEN);
        if (TOPIC_TEMP_FRIDGE[0])  mqtt.subscribe(TOPIC_TEMP_FRIDGE);
        if (TOPIC_TEMP_CABINET[0]) mqtt.subscribe(TOPIC_TEMP_CABINET);
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

    uint32_t now       = millis();
    bool     mqtt_ok   = mqtt.connected();
    bool     ghost_due = (now - last_ghost_ms >= FULL_REFRESH_INTERVAL);
    // Gate offen wenn: noch nie Daten angezeigt ODER Mindestabstand abgelaufen
    bool     gate_open = (last_shown.soc < 0.0f) || (now - last_data_ms >= DATA_REFRESH_INTERVAL_MS);

    bool do_full = (full_refresh_needed && gate_open) || ghost_due;
    if (do_full) {
        display_full_refresh(victron, mqtt_ok, ip_str, menu_sel, relay_state);
        last_shown   = victron;
        last_data_ms = now;
        if (ghost_due) last_ghost_ms = now;
        full_refresh_needed = false;
        menu_strip_dirty = false;
    } else if (menu_strip_dirty) {
        display_menu_strip_update(mqtt_ok, ip_str, menu_sel, relay_state);
        menu_strip_dirty = false;
    }
}
