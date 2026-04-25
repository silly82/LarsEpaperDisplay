# CLAUDE.md – Camping-Bus E-Paper Display

**Version:** 0.7.1 · Vollständige Nutzerdoku: [`README.md`](README.md) · Änderungen: [`CHANGELOG.md`](CHANGELOG.md)

## Kurzüberblick

MQTT-E-Paper-Anzeige (LilyGo T5 4.7″ S3): MPPT/Batterie per **JSON-Topic** oder flachen Topics, **vier Temp-Topics**, Relais-Menü mit einem Taster (Kurz = weiter, Lang = Aktion), Relais-State per MQTT. **Kein Touch** in der Firmware. **Kein GPIO-0-Taster** (EPD CFG_STR / Strapping).

## Constraints

- **platform:** `espressif32@6.12.0` (Arduino Core 2.x). Kein Core 3.x (I2S-EPD API).
- **LilyGo-EPD47:** Branch **`esp32s3`**.
- **Framebuffer:** `ps_calloc`, Größe `EPD_WIDTH * EPD_HEIGHT / 2`.
- **Rect_t:** Felder `x, y, width, height`.
- **Upload:** oft `BOOT→RST`, dann Flashen; `platformio.ini`: `--before=no_reset`, `upload_speed = 460800`.
- **Button2:** `begin(pin, INPUT_PULLUP, true)` → **activeLow** muss **true** sein (LilyGo).

## Dateien

| Datei | Inhalt |
|-------|--------|
| `src/config.h` | Topics, Timing, Menü, Relais |
| `src/main.cpp` | MQTT, Taster, Menülogik |
| `src/display.cpp` | `display_full_refresh`, `display_menu_strip_update` (nur y≥400) |
| `src/wifi_prov.*` | AP, `/save`, `/erase`, `wifi_connect(..., idle_cb)` |

## MQTT (Defaults in config.h)

- MPPT/Batterie: `TOPIC_TELEMETRY_JSON` (z. B. `camping/telemetry/mppt`, JSON) **oder** flach `lars/mppt/soc`, `batV`, `solarW` (+ optional Strom/Last, leer = aus).
- Temperaturen: `camping/temp/aussen|innen|kuehlschrank|geraeteschrank` (je `""` = aus).
- Relais CMD: `lars/relais/{1,2,3}/cmd`, Payload `MQTT_RELAY_PAYLOAD`.
- Relais State: `lars/relais/{1,2,3}/state` → Anzeige `R1 AN` / `aus` / `--`.

## Refresh

- **Vollbild:** Schwellen `DISPLAY_THRESHOLD_*` + `DATA_REFRESH_INTERVAL_MS` (30 s), periodisch `FULL_REFRESH_INTERVAL` (10 min), Menü „Bild neu“.
- **Menü / Relais-State:** Partial nur untere Leiste (y ≥ 400).
- **Menü-Streifen:** `epd_draw_grayscale_image(Rect, fb + offset)` nur für unteren Bereich.

## Provisioning

- AP `CampingDisplay`; kein Config-Reset per Taster – nur Web-Formular „Konfiguration löschen“.

## Build

```bash
pio run && pio run -t upload
```
