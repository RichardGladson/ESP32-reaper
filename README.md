# DOOMBOX REAPER

**Modular ESP32-C3 RF survey / security-research field module**

REAPER is the radio/field node of the larger DOOMBOX system.  
DOOMBOX CORE / WRAITH will act as the controller; REAPER can also operate fully standalone.

This firmware does **not** implement Bluetooth or Wi-Fi interference / jamming.  
It provides passive channel observation, coordinated multi-radio channel stepping, status reporting, and a clean ESP-NOW command interface for a future CORE.

---

## Purpose

- Three independent nRF24L01 radios on a shared SPI bus for 2.4 GHz ISM band observation
- Automatic discovery and pairing with a DOOMBOX CORE over ESP-NOW
- Core-controlled mode (commands have priority) and fully usable standalone mode
- Non-blocking state machine, button handling, and LED status patterns
- Clean, versioned protocol so CORE firmware can be written later without rewriting REAPER

---

## Hardware

| Component            | Details                          |
|----------------------|----------------------------------|
| MCU                  | ESP32-C3 Super Mini              |
| Radios               | 3 × nRF24L01 (+PA/LNA optional)  |
| Status LED           | GPIO1                            |
| Mode button          | GPIO10 (active-low, internal pull-up) |

### Exact pinout

| Function          | GPIO |
|-------------------|------|
| SPI SCK           | 4    |
| SPI MISO          | 6    |
| SPI MOSI          | 7    |
| Radio 1 CE        | 0    |
| Radio 1 CSN       | 2    |
| Radio 2 CE        | 3    |
| Radio 2 CSN       | 5    |
| Radio 3 CE        | 8    |
| Radio 3 CSN       | 9    |
| Status LED        | 1    |
| Mode button       | 10   |

All three nRF24 modules share SCK / MISO / MOSI.  
Each has its own CE and CSN. IRQ lines are unused.  
VCC = 3.3 V, GND = common ground.  
**Do not power nRF24 modules from 5 V.**

### GPIO notes (ESP32-C3)

- GPIO0 is a strapping pin; used here as CE (output). Ensure the radio does not drive it during boot.
- GPIO8 / GPIO9 are also strapping-related on some C3 boards; treat as normal GPIOs after boot.
- USB-CDC is enabled (`ARDUINO_USB_CDC_ON_BOOT`). Serial appears on the USB port.

**Hardware has not been physically validated.** Pin assignments follow the design specification and should be verified on first power-up.

---

## Architecture

```
main.cpp
  ├── RadioManager      (3× RF24, shared SPI, independent CE/CSN)
  ├── EspNowManager     (discovery, pairing, command transport)
  ├── Protocol          (versioned packet format)
  ├── ButtonManager     (debounce, short press, 2 s hold)
  ├── LedManager        (non-blocking patterns)
  └── StateManager      (BOOT → INIT → DISCOVERY → CORE_CONNECTED / STANDALONE)
```

Operating-control states are independent of the actual RF survey functions:

1. **CORE-CONTROLLED MODE** – paired with CORE; Core commands have priority; standalone button functions are disabled (long-hold may still trigger a local emergency stop).
2. **STANDALONE MODE** – no Core present (or Core lost); physical button controls survey start/stop and mode selection.

---

## Three-radio architecture

- One `SPIClass` (FSPI) shared by all three `RF24` instances.
- Individual CE / CSN pins.
- Radios can be enabled/disabled, channel-configured, and health-checked independently.
- Failure of one radio does **not** crash the firmware; the health bitfield reports which radios are alive.
- Coordinated survey stepping can assign staggered channels across the three radios for wider concurrent coverage.

### Hardware limitations (important)

- nRF24L01 does **not** provide continuous energy detection or true RSSI without a received packet.
- The RPD (Received Power Detector) bit exists on some clones but is not a calibrated RSSI.
- Channel numbers 0–125 correspond to 2400–2525 MHz (1 MHz steps). This overlaps the 2.4 GHz ISM band used by Bluetooth and Wi-Fi but is **not** a 1:1 map of Bluetooth advertising channels or Wi-Fi channel numbers.
- This firmware only steps channels and listens. **No continuous carrier or noise transmission is implemented.**

---

## ESP-NOW Core communication

On boot REAPER:

1. Initializes Wi-Fi in station mode (no AP connection required).
2. Initializes ESP-NOW.
3. Enters automatic discovery and listens for a valid `DISCOVERY_BEACON` from a device advertising `DeviceRole::CORE` or `WRAITH`.
4. Validates magic, protocol version, and checksum.
5. Sends a `PAIRING_REQUEST`.
6. On accepted `PAIRING_RESPONSE`, adds the Core as an ESP-NOW peer and enters **CORE_CONNECTED**.
7. Exchanges heartbeats; if the Core heartbeat times out, REAPER returns to discovery / standalone and continues trying to re-pair.

No manual MAC entry is required for normal operation.

### Protocol overview

Every packet has a fixed header:

- Magic (`0xDB`)
- Protocol version
- Packet type (enum)
- Sender role
- Sender ID (6-byte MAC)
- Sequence number
- Payload length
- Checksum (XOR of header fields)

Packet types include: discovery/pairing, heartbeat, status request/response, mode command, radio configuration, scan/test, stop, and error responses.  
The design is intentionally extensible for future CORE versions.

---

## Standalone mode

When not connected to CORE:

| Action              | Behaviour                                      |
|---------------------|------------------------------------------------|
| Short press         | Toggle the currently selected survey on/off    |
| Hold 2 seconds      | Switch between BLUETOOTH_SURVEY and WIFI_SURVEY |

Button handling is fully non-blocking (debounce, press/release detection, hold detection) and does not use `delay()`.

While **CORE_CONNECTED**, short-press is ignored; long-hold is treated as a local emergency stop only.

---

## LED status patterns (GPIO1)

| State                     | Pattern                          |
|---------------------------|----------------------------------|
| BOOT                      | Slow pulse (~1 Hz)               |
| INITIALIZING RADIOS       | Rapid blink                      |
| SEARCHING FOR CORE        | Short periodic blink             |
| CORE CONNECTED            | Solid on                         |
| STANDALONE BT SURVEY      | Double-blink                     |
| STANDALONE WIFI SURVEY    | Triple-blink                     |
| ERROR                     | Very rapid blink                 |

All patterns are non-blocking.

---

## Software dependencies

- PlatformIO + `espressif32` platform
- Arduino framework
- [RF24](https://github.com/nRF24/RF24) library (`nrf24/RF24@^1.4.10`)
- ESP-IDF Wi-Fi / ESP-NOW stack (bundled with the Arduino core)

---

## Build instructions

```bash
# Clone / extract the project
cd doombox-reaper

# Build
pio run

# Upload (adjust port as needed)
pio run -t upload

# Serial monitor
pio device monitor
```

Board environment: `esp32-c3-devkitm-1` (compatible with most ESP32-C3 Super Mini boards).  
USB-CDC is enabled; the serial port appears as a USB ACM device.

---

## Current hardware-testing status

**Not tested on physical hardware.**  
The ESP32-C3 Super Mini and nRF24 modules had not arrived at the time of firmware development.  

Code prioritizes:

- Compile correctness
- Isolation of hardware-dependent paths
- Explicit documentation of assumptions that require bench validation

Items that must be verified on first bring-up:

1. SPI pin mapping and radio communication
2. CE/CSN polarity and levels
3. LED active level (HIGH = on assumed)
4. Button pull-up and active-low behaviour
5. ESP-NOW range and channel coexistence with the nRF24 radios
6. Strapping-pin behaviour of GPIO0 / GPIO8 / GPIO9 on the specific Super Mini revision

---

## Known limitations

- No continuous energy detection / spectrum analyser capability (nRF24 hardware limit).
- Survey “Bluetooth” / “Wi-Fi” modes only select different nRF24 channel ranges; they do not decode Bluetooth or Wi-Fi frames.
- ESP-NOW and nRF24 both use the 2.4 GHz band; simultaneous high-duty-cycle operation may affect each other.
- Protocol encryption is not enabled (ESP-NOW `encrypt = false`). Future CORE versions may add application-layer authentication.
- One radio failing is handled; complete SPI bus failure is not recoverable without power cycle.

---

## Future DOOMBOX CORE integration

REAPER already implements:

- Automatic discovery and pairing
- Versioned packet format with explicit types
- Command handlers for mode, radio config, scan/test, stop, and status
- Heartbeat watchdog and clean fallback to standalone

CORE / WRAITH only needs to:

1. Broadcast `DISCOVERY_BEACON` packets with `DeviceRole::CORE` (or `WRAITH`).
2. Accept `PAIRING_REQUEST` and reply with `PAIRING_RESPONSE` (status = 0).
3. Send commands using the same header + payload structures defined in `protocol.h`.
4. Expect periodic `HEARTBEAT` and `STATUS_RESPONSE` packets from REAPER.

No major rewrite of REAPER should be required.

---

## License / research use

Intended for legitimate RF survey, educational, and security-research purposes on equipment you own or have permission to test.  
Comply with local radio regulations.
