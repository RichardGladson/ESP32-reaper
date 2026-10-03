/**
 * DOOMBOX REAPER - Configuration
 * ESP32-C3 Super Mini + 3x nRF24L01
 *
 * Hardware not yet physically validated.
 * Pin assignments follow the provided pinout.
 */

#ifndef DOOMBOX_REAPER_CONFIG_H
#define DOOMBOX_REAPER_CONFIG_H

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Firmware identity
// ---------------------------------------------------------------------------
#define REAPER_FW_VERSION       "1.0.0"
#define REAPER_PROTOCOL_VERSION 0x01
#define REAPER_DEVICE_TYPE      0x01   // REAPER radio module

// Protocol magic used in ESP-NOW packets
#define PROTOCOL_MAGIC          0xDB   // "DoomBox"

// ---------------------------------------------------------------------------
// GPIO pin assignments (ESP32-C3 Super Mini)
// ---------------------------------------------------------------------------
// Shared SPI bus for all three nRF24 modules
#define PIN_SPI_SCK             4
#define PIN_SPI_MISO            6
#define PIN_SPI_MOSI            7

// Radio 1
#define PIN_RADIO1_CE           0
#define PIN_RADIO1_CSN          2

// Radio 2
#define PIN_RADIO2_CE           3
#define PIN_RADIO2_CSN          5

// Radio 3
#define PIN_RADIO3_CE           8
#define PIN_RADIO3_CSN          9

// Status LED (active-high assumed; invert in LED manager if needed)
#define PIN_STATUS_LED          1

// Mode button (active-low with internal pull-up)
#define PIN_MODE_BUTTON         10

// ---------------------------------------------------------------------------
// nRF24 channel limits (hardware)
// nRF24L01 channels 0-125 map to 2400-2525 MHz (1 MHz steps)
// Bluetooth classic uses 2402-2480 MHz (79 channels, 1 MHz)
// Wi-Fi 2.4 GHz uses channels 1-14 (approx 2412-2484 MHz)
// ---------------------------------------------------------------------------
#define NRF24_CHANNEL_MIN       0
#define NRF24_CHANNEL_MAX       125
#define NRF24_DEFAULT_CHANNEL   76

// Configurable survey ranges (can be changed at runtime via commands)
#define SURVEY_BT_CHANNEL_START 2     // ~2402 MHz
#define SURVEY_BT_CHANNEL_END   80    // ~2480 MHz
#define SURVEY_WIFI_CHANNEL_START 1
#define SURVEY_WIFI_CHANNEL_END   14  // map later to nRF24 freqs if needed

// ---------------------------------------------------------------------------
// Timing constants (milliseconds)
// ---------------------------------------------------------------------------
#define BUTTON_DEBOUNCE_MS      40
#define BUTTON_HOLD_MS          2000
#define CORE_HEARTBEAT_TIMEOUT_MS 5000
#define CORE_DISCOVERY_INTERVAL_MS 1000
#define LED_UPDATE_INTERVAL_MS  20
#define RADIO_HEALTH_INTERVAL_MS  2000
#define SURVEY_STEP_INTERVAL_MS   50

// ---------------------------------------------------------------------------
// ESP-NOW
// ---------------------------------------------------------------------------
#define ESPNOW_CHANNEL          1     // Wi-Fi channel for ESP-NOW
#define MAX_PEER_COUNT          4
#define DEVICE_ID_LEN           6     // MAC length used as device ID

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------
#ifndef REAPER_LOG_LEVEL
#define REAPER_LOG_LEVEL        2     // 0=off, 1=error, 2=info, 3=debug
#endif

#if REAPER_LOG_LEVEL >= 1
  #define LOG_E(fmt, ...) Serial.printf("[E] " fmt "\n", ##__VA_ARGS__)
#else
  #define LOG_E(fmt, ...)
#endif

#if REAPER_LOG_LEVEL >= 2
  #define LOG_I(fmt, ...) Serial.printf("[I] " fmt "\n", ##__VA_ARGS__)
#else
  #define LOG_I(fmt, ...)
#endif

#if REAPER_LOG_LEVEL >= 3
  #define LOG_D(fmt, ...) Serial.printf("[D] " fmt "\n", ##__VA_ARGS__)
#else
  #define LOG_D(fmt, ...)
#endif

#endif // DOOMBOX_REAPER_CONFIG_H
