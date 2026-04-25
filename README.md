# Camping-Bus E-Paper Display

**Version:** 0.7.0 (siehe [`VERSION`](VERSION), Historie [`CHANGELOG.md`](CHANGELOG.md))

E-Paper-Statusanzeige für den Campingbus: MPPT/Batterie- und Solarwerte per **MQTT**, steuerbare **Relais** über dasselbe MQTT, Bedienung mit **einem Taster** und **unterem Menü** auf dem LilyGo **T5 4.7″** (ESP32-S3).

---

## Hardware

| Komponente | Details |
|------------|---------|
| Board | **LilyGo T5 4.7″ E-Paper V2.3** (ESP32-S3) |
| Flash / PSRAM | 16 MB / 8 MB OPI |
| Display | ED047TC1 · 960×540 px · 4 Graustufen |
| Taster | GPIO **21** (nicht GPIO 0 verwenden – EPD/Boot-Konflikt) |

Detailliertes Pinout: [`hardware.md`](hardware.md)

---

## Architektur

```
MPPT / Batterie / Node-RED
        │  MQTT publish/subscribe
        ▼
   MQTT-Broker (z. B. Mosquitto :1883)
        │  WiFi
        ▼
   ESP32-S3  ←→  E-Paper (Vollbild + optional Menü-Streifen-Partial)
```

---

## Konfiguration im Code

Alle anpassbaren Werte: [`src/config.h`](src/config.h) (Topics, Timing, Menü, Relais).

---

## MQTT: Messwerte (Subscribe)

Zwei Modi (siehe `TOPIC_TELEMETRY_JSON` in [`src/config.h`](src/config.h)):

1. **JSON-Telemetrie** (Standard: Topic nicht leer, z. B. `camping/telemetry/mppt`): ein MQTT-Topic, Payload = **JSON-Objekt** mit optionalen Feldern `soc`, `voltage`/`bat_v`, `solar_w`/`solarW`, `current_a`/`batA`, `load_w`/`loadW`. **Temperaturen** kommen in der Firmware **nicht** aus diesem JSON, sondern nur über die separaten Topics unten.
2. **Flache Topics**: wenn `TOPIC_TELEMETRY_JSON` auf `""` steht, gelten die Zeilen unten; Payload jeweils **reiner Text** mit **Float** (z. B. `"78.5"`).

| Topic (Standard, flacher Modus) | Bedeutung |
|---------------------------------|-----------|
| `lars/mppt/soc` | Ladezustand % |
| `lars/mppt/batV` | Batteriespannung V |
| `lars/mppt/solarW` | Solarleistung W |
| *optional* `TOPIC_CURRENT` | Strom A (leer `""` = deaktiviert) |
| *optional* `TOPIC_LOAD_POWER` | Last W |

**Umgebungstemperaturen** (eigene Topics, immer Subscribe wenn nicht leer):

| Topic (Standard) | Bedeutung |
|------------------|-----------|
| `camping/temp/aussen` | Außen °C |
| `camping/temp/innen` | Innen °C |
| `camping/temp/kuehlschrank` | Kühlschrank °C |
| `camping/temp/geraeteschrank` | Geräteschrank °C |

---

## MQTT: Relais

| Richtung | Topic (Standard) | Hinweis |
|----------|------------------|---------|
| **Publish** (Langdruck im Menü) | `lars/relais/1/cmd` … `/3/cmd` | Payload: `MQTT_RELAY_PAYLOAD` (z. B. `toggle`) |
| **Subscribe** (Anzeige) | `lars/relais/1/state` … `/3/state` | z. B. `1`/`0`, `on`/`off`, `an`/`aus` |

Node-RED (oder anderer Dienst) sollte nach dem Schalten den **Istzustand** auf `…/state` publizieren, damit die Kästen `R1 AN` / `R1 aus` stimmen.

---

## Bedienung (GPIO 21)

| Aktion | Funktion |
|--------|----------|
| **Kurz** | Nächster Menüeintrag (Relais 1–3, **Bild neu**); untere Leiste wird per **Partial** aktualisiert (~2–4 s). |
| **Lang** (~900 ms) | **Auswahl**: Relais → MQTT-Befehl; Eintrag **Bild neu** → **Vollbild** (`epd_clear`). |

---

## Display-Refresh

| Auslöser | Verhalten |
|----------|-----------|
| Neue / geänderte Messwerte über **Schwellen** (`DISPLAY_THRESHOLD_*` in `config.h`) und Mindestabstand `DATA_REFRESH_INTERVAL_MS` (15 s) | Vollbild mit Messwerten + Menü |
| Intervall `FULL_REFRESH_INTERVAL` (5 min) | Vollbild (Geister reduzieren) |
| Menüwechsel / neuer Relais-State | Nur Bereich ab **y = 400** (WiFi/MQTT + Menüleiste) |
| Start, Menü „Bild neu“ (Lang) | Vollbild |

Messwerte-Bereich oben: **kein** klassisches Partial mehr (Ghosting) – nur **Vollbild**.

---

## Erstkonfiguration (AP)

1. Ohne gültige `config.json`: AP **`CampingDisplay`** (Passwort `camping123`).
2. Browser: **`http://192.168.4.1`**
3. WLAN + MQTT eintragen, **Speichern & Neustart** → Display zeigt „Gespeichert / Neustart …“.
4. **Konfiguration löschen**: im gleichen Formular Button „Konfiguration löschen & Neustart“ (mit Bestätigung).

---

## Build & Flash

- **PlatformIO**, `espressif32@6.12.0`, Board `esp32-s3-devkitc1-n16r8`
- **Flash-Modus:** BOOT halten → RST tippen → BOOT los → `pio run -t upload`
- In [`platformio.ini`](platformio.ini): `--before=no_reset` – Upload oft nur, wenn der Chip **bereits im Bootloader** ist.

```bash
pio run                  # bauen
pio run -t upload        # flashen
pio device monitor       # 115200 Baud
pio run -t uploadfs      # LittleFS (optional)
```

---

## Projektstruktur

```
LarsEpaperDisplay/
├── VERSION
├── CHANGELOG.md
├── README.md
├── NODE_RED.md            # MQTT/JSON, Node-RED-Flow
├── CLAUDE.md              # Kurzreferenz für Entwicklung / KI
├── hardware.md
├── platformio.ini
├── data/                  # LittleFS-Vorlage (config.json entsteht zur Laufzeit)
├── node-red/              # Beispiel-Flow (siehe NODE_RED.md)
└── src/
    ├── config.h
    ├── main.cpp
    ├── display.h / display.cpp
    └── wifi_prov.h / wifi_prov.cpp
```

---

## Abhängigkeiten (platformio.ini)

| Library | Zweck |
|---------|--------|
| LilyGo-EPD47 (`esp32s3`) | EPD-Treiber |
| Button2 | Taster ENTPRELLUNG |
| ArduinoJson | `config.json` |
| PubSubClient | MQTT |
| SensorLib | in `lib_deps` (Touch derzeit nicht genutzt) |

---

## Einschränkungen

- **Arduino Core:** mit **2.x** (`espressif32@6.12.0`) getestet; Core 3.x bricht den EPD-Treiber.
- **EPD:** nach jedem Zeichnen `epd_poweroff()` (im Treiber gekapselt) – sonst Überhitzungsrisiko.
- **GPIO 0:** nicht als Software-Taster nutzen (gemeinsam mit EPD / Boot).

---

## Lizenz / Herkunft

Projekt „LarsEpaperDisplay“ – Camping-Bus-Display; Hardware-Dokumentation verweist auf LilyGo / Espressif Datenblätter.
