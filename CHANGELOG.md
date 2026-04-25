# Changelog

Alle nennenswerten Änderungen pro Version.

## [0.7.0] – 2026-04-25

Offizielles Release nach Merge von `feature/mqtt-json-telemetry` in `main`; Versionsprung auf **0.7.0**. Doku (`README`, `NODE_RED.md`, `CLAUDE.md`) und Node-RED-Beispiel an den Firmware-Stand angeglichen.

## [0.5.0] – 2026-04-25

### Funktionen
- **MQTT MPPT/Batterie**: alternativ **ein JSON-Topic** (`TOPIC_TELEMETRY_JSON`, Standard `camping/telemetry/mppt`) mit Feldern wie `soc`, `voltage`/`bat_v`, `solar_w`/`solarW`, `current_a`/`batA`, `load_w`/`loadW` – oder **flache Topics** `lars/mppt/…`, wenn das JSON-Topic leer ist; optionale Topics für Strom und Last (leer = nicht abonniert).
- **Vier Umgebungstemperaturen** auf eigenen Topics (Standard `camping/temp/aussen`, `…/innen`, `…/kuehlschrank`, `…/geraeteschrank`; in `config.h` deaktivierbar).
- **Unteres Menü** (GPIO-21-Taster): **Kurz** = nächster Eintrag, **Lang** (~900 ms) = Aktion.
- **Relais 1–3**: Langdruck sendet Befehl auf `lars/relais/n/cmd` (Payload `toggle`, konfigurierbar).
- **Relais-Anzeige**: Subscribe auf `lars/relais/n/state` mit Payload `1`/`0`, `on`/`off`, `an`/`aus` usw.; Kästen zeigen `R1 AN` / `R1 aus` / `R1 --`.
- **Bild neu**: vierter Menüpunkt → voller Refresh (`epd_clear`).
- **Menü-Streifen-Partial**: nur Bereich ab y=400 beim Menüwechsel oder neuem Relais-State (weniger Ghosting im Messwerte-Bereich).
- **Automatik**: Vollbild bei **relevanten Messwertänderungen** (Schwellen in `config.h`) und frühestens alle `DATA_REFRESH_INTERVAL_MS` (15 s); zusätzlich alle `FULL_REFRESH_INTERVAL` (5 min) gegen Ghosting.

### Provisioning
- AP `CampingDisplay` / Captive-Setup unter `http://192.168.4.1`.
- Nach Speichern: Meldung „Gespeichert / Neustart“ auf dem Display.
- **Konfiguration löschen** über Formularbutton im Setup (nicht mehr per Taster-Langdruck).

### Hardware / Plattform
- **Kein GPIO 0 als Taster** (Konflikt mit EPD `CFG_STR` / Boot-Strapping).
- **Button2** mit `activeLow = true` (LilyGo-Taster nach GND).
- **Upload**: `upload_flags --before=no_reset` – typisch zuerst Boot-Modus, dann Flashen.
- **Upload-Baud** 460800 (robuster als 921600 bei manchen Kabeln).

### Entfernt / Nicht genutzt in dieser Version
- Kein Touch-Code auf dem Gerät (GT911 nicht angebunden).
- Kein klassisches Partial-Update für den oberen Messwerte-Bereich (nur Vollbild).
