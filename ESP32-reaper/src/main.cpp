/**
 * DOOMBOX REAPER - Main entry point
 * ESP32-C3 Super Mini + 3x nRF24L01 modular RF survey node
 *
 * Architecture:
 *   - RadioManager     : three independent nRF24 instances
 *   - EspNowManager    : automatic Core discovery / pairing / commands
 *   - ButtonManager    : non-blocking debounce + short/long press
 *   - LedManager       : non-blocking status patterns
 *   - StateManager     : explicit state machine (Core vs Standalone)
 *
 * Hardware has not been physically validated.
 */

#include <Arduino.h>
#include "config.h"
#include "protocol.h"
#include "radio_manager.h"
#include "espnow_manager.h"
#include "button_manager.h"
#include "led_manager.h"
#include "state_manager.h"

// Global module instances
static RadioManager   g_radio;
static EspNowManager  g_espnow;
static ButtonManager  g_button;
static LedManager     g_led;
static StateManager   g_state(g_radio, g_espnow, g_button, g_led);

// Forward command packets from ESP-NOW into the state machine
static void onCoreCommand(const ProtocolPacket& pkt, const uint8_t* mac) {
    (void)mac;
    g_state.handleCoreCommand(pkt);
}

void setup() {
    Serial.begin(115200);
    delay(200); // allow USB-CDC to settle on C3
    Serial.println();
    LOG_I("========================================");
    LOG_I("  DOOMBOX REAPER  v%s", REAPER_FW_VERSION);
    LOG_I("  ESP32-C3 Super Mini RF Survey Module");
    LOG_I("========================================");

    // LED first so we have visual feedback during init
    g_led.begin();
    g_led.setPattern(LedPattern::BOOT_PULSE);

    g_button.begin();

    // Radio initialization (may partially fail - firmware continues)
    g_led.setPattern(LedPattern::INIT_RAPID);
    bool radio_ok = g_radio.begin();
    if (!radio_ok) {
        LOG_E("All radios failed - survey features unavailable");
        // Continue; Core communication may still work
    }

    // ESP-NOW / Core discovery
    bool espnow_ok = g_espnow.begin();
    if (!espnow_ok) {
        LOG_E("ESP-NOW init failed - Core control unavailable");
        // Fall through to standalone-capable state machine
    }
    g_espnow.setCommandHandler(onCoreCommand);

    // Start state machine
    g_state.begin();

    LOG_I("Setup complete. Free heap: %u", ESP.getFreeHeap());
}

void loop() {
    g_state.update();
    // All timing is millis()-based inside the managers; no delay() in the loop.
    // A short yield keeps the watchdog happy without blocking.
    yield();
}
