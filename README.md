# ESP32-Reaper 

**Modular ESP32-C3 RF survey / security-research field module**

REAPER is the radio/field node of the [doomBOX](https://github.com/richardGladson/doomBOX) system.  
**doomBOX** is the controller (OLED UI, Wi‑Fi/BLE tools); **Reaper** is the remote triple-nRF24 sensor that can also run fully standalone.

This firmware does **not** implement Bluetooth or Wi‑Fi jamming or continuous carrier.  
It provides passive 2.4 GHz channel observation, coordinated multi-radio stepping, status reporting, and an ESP‑NOW command interface for doomBOX.

**Repository:** [github.com/RichardGladson/ESP32-reaper](https://github.com/RichardGladson/ESP32-reaper)

---

## System role

| Role | Repo | What it does |
|------|------|----------------|
| **doomBOX** | [richardGladson/doomBOX](https://github.com/richardGladson/doomBOX) | Controller UI — discover/pair Reaper, send survey commands, show status |
| **ESP32-Reaper (this repo)** | [RichardGladson/ESP32-reaper](https://github.com/RichardGladson/ESP32-reaper) | 3× nRF24 field node — passive survey, heartbeats, command execution |

**With doomBOX:** power Reaper → on doomBOX open **Reaper** (main menu, right column) → wait for **PAIRED** → start BT/Wi‑Fi survey or stop from the OLED.

**Standalone:** short press toggles survey on/off; hold ~2 s switches between Bluetooth-range and Wi‑Fi-range channel sets. LED shows mode.

---

## Hardware

| Component | Details |
|-----------|---------|
| MCU | ESP32-C3 Super Mini |
| Radios | 3 × nRF24L01 (+PA/LNA optional) |
| Status LED | GPIO 1 |
| Mode button | GPIO 10 (active-low, internal pull-up) |

### Pinout

| Function | GPIO |
|----------|------|
| SPI SCK | 4 |
| SPI MISO | 6 |
| SPI MOSI | 7 |
| Radio 1 CE | 0 |
| Radio 1 CSN | 2 |
| Radio 2 CE | 3 |
| Radio 2 CSN | 5 |
| Radio 3 CE | 8 |
| Radio 3 CSN | 9 |
| Status LED | 1 |
| Mode button | 10 |

All three nRF24 modules share SCK / MISO / MOSI; each has its own CE and CSN. IRQ lines are unused.  
**VCC = 3.3 V only. Do not power nRF24 modules from 5 V.**

### GPIO notes (ESP32-C3)

- GPIO 0 is a strapping pin (used as CE). Ensure the radio does not drive it during boot.
- GPIO 8 / 9 are strapping-related on some boards; treat as normal GPIOs after boot.
- USB-CDC is enabled (`ARDUINO_USB_CDC_ON_BOOT`); serial appears on the USB port.

> Hardware bring-up should be verified on first power-up (SPI, CE/CSN polarity, LED active level, button, ESP‑NOW coexistence with nRF24).

---

## Features

- Three independent nRF24 instances on one SPI bus (per-radio enable, channel, health)
- Coordinated channel stepping for wider concurrent coverage
- Survey modes (channel ranges only — no frame decode):
  - **Bluetooth survey** — nRF24 channels ~2–80 (~2402–2480 MHz)
  - **Wi‑Fi survey** — nRF24 channels ~1–14
- Automatic ESP‑NOW discovery and pairing with doomBOX (no manual MAC entry)
- Core-controlled mode (commands have priority) and full standalone mode
- Non-blocking state machine, button handling, LED patterns
- Versioned binary protocol so doomBOX and Reaper stay compatible

**Not included:** continuous carrier, noise transmission, or jamming. nRF24 has no calibrated continuous RSSI; survey is listen/step only.

---

## Architecture

```
main.cpp
  ├── RadioManager      (3× RF24, shared SPI, independent CE/CSN)
  ├── EspNowManager     (discovery, pairing, heartbeats, commands)
  ├── Protocol          (versioned packet format)
  ├── ButtonManager     (debounce, short press, 2 s hold)
  ├── LedManager        (non-blocking patterns)
  └── StateManager      (BOOT → INIT → DISCOVERY → CORE_CONNECTED / STANDALONE)
```

| Mode | Behaviour |
|------|-----------|
| **CORE_CONNECTED** | doomBOX commands have priority; short button press ignored; long-hold = local emergency stop |
| **STANDALONE** | Short press = toggle survey; long-hold = switch BT ↔ Wi‑Fi survey ranges |

If the Core heartbeat times out, Reaper falls back to discovery / standalone and keeps trying to re-pair.

---

## ESP‑NOW protocol (with doomBOX)

On boot Reaper:

1. Wi‑Fi STA (no AP needed) + ESP‑NOW init  
2. Listens for `DISCOVERY_BEACON` from role **CORE** (or **WRAITH**)  
3. Validates magic `0xDB`, protocol version, checksum  
4. Sends `PAIRING_REQUEST`  
5. On accepted `PAIRING_RESPONSE`, adds peer → **PAIRED**  
6. Exchanges heartbeats; timeout → rediscovery  

### Packet types (summary)

| Type | Direction | Purpose |
|------|-----------|---------|
| `DISCOVERY_BEACON` | Core → all | Advertise Core presence |
| `PAIRING_REQUEST` / `RESPONSE` | Reaper ↔ Core | Pair |
| `HEARTBEAT` | Both | Keep-alive; Reaper includes mode + radio health |
| `MODE_COMMAND` | Core → Reaper | Idle / BT survey / Wi‑Fi survey / stop |
| `SCAN_TEST_COMMAND` | Core → Reaper | Start survey with channel range + hop step |
| `STOP_COMMAND` | Core → Reaper | Stop activity |
| `STATUS_REQUEST` / `RESPONSE` | Core ↔ Reaper | Mode, radio bitfield, heap, uptime |
| `RADIO_CONFIG_COMMAND` | Core → Reaper | Per-radio channel / power / rate |

ESP‑NOW channel is fixed to **1** (`ESPNOW_CHANNEL` in `config.h`). Encryption is off (`encrypt = false`); application-layer auth can be added later.

Full definitions: `src/protocol.h` / `protocol.cpp`.

---

## Standalone controls

| Action | Behaviour |
|--------|-----------|
| Short press | Toggle current survey on/off |
| Hold ~2 seconds | Switch between Bluetooth and Wi‑Fi survey ranges |

Non-blocking debounce/hold; no `delay()` in the main loop.

---

## LED patterns (GPIO 1)

| State | Pattern |
|-------|---------|
| BOOT | Slow pulse (~1 Hz) |
| Initializing radios | Rapid blink |
| Searching for Core | Short periodic blink |
| Core connected | Solid on |
| Standalone BT survey | Double-blink |
| Standalone Wi‑Fi survey | Triple-blink |
| Error | Very rapid blink |

---

## Build

**Toolchain:** [PlatformIO](https://platformio.org/)

```bash
cd ESP32-reaper   # directory with platformio.ini
pio run
pio run -t upload
pio device monitor
```

**Environment:** `esp32-c3-super-mini` · board `esp32-c3-devkitm-1`

**Dependencies:**
- PlatformIO `espressif32` + Arduino framework  
- [RF24](https://github.com/nRF24/RF24) (`nrf24/RF24@^1.4.10`)  
- ESP‑IDF Wi‑Fi / ESP‑NOW (bundled with the Arduino core)

USB-CDC is enabled; the serial port appears as a USB ACM device.

---

## Project layout

```
ESP32-reaper/
  platformio.ini
  src/
    main.cpp
    config.h
    protocol.h / protocol.cpp
    radio_manager.h / .cpp
    espnow_manager.h / .cpp
    state_manager.h / .cpp
    button_manager.h / .cpp
    led_manager.h / .cpp
```

---

## Known limitations

- No continuous energy detection / spectrum analyser (nRF24 hardware limit).  
- “Bluetooth” / “Wi‑Fi” modes only select different nRF24 channel ranges; they do not decode frames.  
- ESP‑NOW and nRF24 both use 2.4 GHz; high duty cycle on both may affect range.  
- One radio failing is reported in the health bitfield; a full SPI bus failure needs a power cycle.  
- Protocol encryption is not enabled.

---

## Pairing with doomBOX

1. Flash and power **ESP32-Reaper**.  
2. On doomBOX: main menu → **Right** → **Reaper**.  
3. Wait for **PAIRED** (MAC + radio health on the OLED).  
4. Run **BT Survey** / **WiFi Survey** / **Stop** / **Refresh Status** from the Reaper UI.  
5. Center (SEL) exits back to the doomBOX main menu.

doomBOX implements the Core side of the same protocol (`reaper_protocol`, `reaper_manager`, `reaper_ui` in the doomBOX tree).

---

## Disclaimer

Intended for **legitimate RF survey, education, and security research** on equipment you own or have permission to test. Comply with local radio regulations. Authors and contributors accept no liability for misuse.

---

## License

MIT License — same family as [doomBOX](https://github.com/richardGladson/doomBOX) and upstream nyanBOX tooling.  
Retain copyright notices in source headers. See the repository `LICENSE` file if present, or add a standard MIT `LICENSE` at the repo root.
