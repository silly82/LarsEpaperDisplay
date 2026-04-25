# Hardware Reference – LilyGo T5 4.7" E-Paper (ESP32-S3)

## Board

| Eigenschaft | Wert |
|-------------|------|
| Modell | LilyGo T5 4.7" E-Paper **V2.3** |
| MCU | **ESP32-S3** |
| Flash | 16 MB |
| PSRAM | 8 MB OPI |
| Anschluss | USB-C (Datenkabel erforderlich zum Flashen) |
| USB-Seriell | Espressif USB JTAG/serial · VID `0x303A` PID `0x1001` · Port `cu.usbmodem…` |

Download-Modus: **BOOT** halten → **RST** tippen → **BOOT** loslassen → flashen.

---

## Display-Panel ED047TC1

| Eigenschaft | Wert |
|-------------|------|
| Panel | **ED047TC1** |
| Auflösung | **960 × 540 px** (Landscape) |
| Farbtiefe | 4-bit · ~16 Graustufen |
| Treiber-Defines | `EPD_WIDTH 960` · `EPD_HEIGHT 540` (in `src/epd_driver.h`) |
| Interface | 8-bit parallel über I2S-Peripheral |

---

## Pinout ESP32-S3

### EPD-Bus (`src/ed047tc1.h`)

| Signal | GPIO | Beschreibung |
|--------|------|--------------|
| `CFG_DATA` | 13 | Config-Register Daten |
| `CFG_CLK` | 12 | Config-Register Clock |
| `CFG_STR` | 0 | Config-Register Strobe |
| `CKV` | 38 | Vertical Gate Clock |
| `STH` | 40 | Start Horizontal (Source) |
| `CKH` | 41 | Horizontal Clock (Edge) |
| `D0` | 8 | Datenbus Bit 0 |
| `D1` | 1 | Datenbus Bit 1 |
| `D2` | 2 | Datenbus Bit 2 |
| `D3` | 3 | Datenbus Bit 3 |
| `D4` | 4 | Datenbus Bit 4 |
| `D5` | 5 | Datenbus Bit 5 |
| `D6` | 6 | Datenbus Bit 6 |
| `D7` | 7 | Datenbus Bit 7 |

### microSD-Karte – SPI (`src/utilities.h`)

| Signal | GPIO |
|--------|------|
| `SD_MISO` | 16 |
| `SD_MOSI` | 15 |
| `SD_SCLK` | 11 |
| `SD_CS` | 42 |

GPIOs 16, 15, 11, 42 sind frei nutzbar, wenn kein SD-Slot bestückt ist.

### Allgemeiner SPI-Header (`src/utilities.h`)

| Signal | GPIO |
|--------|------|
| `GPIO_MISO` | 45 |
| `GPIO_MOSI` | 10 |
| `GPIO_SCLK` | 48 |
| `GPIO_CS` | 39 |

### Sonstige Peripherie (`src/utilities.h`)

| Signal | GPIO | Beschreibung |
|--------|------|--------------|
| `BUTTON_1` | 21 | Taster |
| `BATT_PIN` | 14 | Batterie-ADC |
| `BOARD_SCL` | 17 | I²C Clock |
| `BOARD_SDA` | 18 | I²C Data |
| `TOUCH_INT` | 47 | Touch-Interrupt |

---

## Touch – GT911 (Goodix)

| Eigenschaft | Wert |
|-------------|------|
| IC | **GT911** (kapazitiv) |
| I²C-Adresse | `0x14` oder `0x5D` (per Scan ermitteln, da Reset-Pin Hardware-Pull-up) |
| Library | `TouchDrvGT911` · **SensorLib ≥ v0.19** |
| Max. Touchpunkte | **5 simultan** |
| Koordinaten | X/Y, kalibrierbar auf 960 × 540 |
| Ausrichtung | SwapXY = true · MirrorY = true (wie in `examples/touch`) |

### Touch-Pins

| Signal | GPIO |
|--------|------|
| INT (`TOUCH_INT`) | 47 |
| SDA (`BOARD_SDA`) | 18 |
| SCL (`BOARD_SCL`) | 17 |

### Einschränkungen

- **Deep-Sleep-Wakeup per Touch nicht möglich**: GPIO 47 ist kein RTC-GPIO. Workaround: GPIO 10 physisch mit GPIO 47 brücken (Issue #93). Im Beispiel wird stattdessen GPIO 21 (Taster) als Weckquelle genutzt.
- **Polling-Intervall**: ≥ 300 ms empfohlen, da jedes EPD-Update Zeit braucht.
- Nach Sleep-Wakeup des ESP32: 1 s warten, bevor Touch initialisiert wird, sonst ist der GT911 nicht adressierbar.

---

## Referenzen

- Schaltplan V2.3: `schematic/T5-ePaper-S3-V2.3.pdf`
- Schaltplan V2.4: `schematic/Screen-4.7-S3-V2.4 24-12-03.pdf`
- Panel-Datenblatt: `datasheet/ED047TC1.pdf`
- [LilyGo T5 4.7" E-Paper V2.3 (Produkt)](https://lilygo.cc/en-us/products/t5-4-7-inch-e-paper-v2-3)
- [LilyGo-EPD47, Branch `esp32s3` (GitHub)](https://github.com/Xinyuan-LilyGO/LilyGo-EPD47/tree/esp32s3)
