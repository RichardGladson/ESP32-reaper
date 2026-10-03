/**
 * Non-blocking LED status manager for GPIO1
 */

#ifndef DOOMBOX_LED_MANAGER_H
#define DOOMBOX_LED_MANAGER_H

#include <Arduino.h>
#include "config.h"

enum class LedPattern : uint8_t {
    OFF = 0,
    SOLID_ON,
    BOOT_PULSE,          // slow pulse
    INIT_RAPID,          // rapid blink during radio init
    SEARCHING_CORE,      // periodic blink while discovering
    CORE_CONNECTED,      // solid or slow breathing
    STANDALONE_BT,       // distinct pattern for BT survey
    STANDALONE_WIFI,     // distinct pattern for Wi-Fi survey
    ERROR_RAPID,         // rapid repeating error
};

class LedManager {
public:
    LedManager();

    void begin();
    void setPattern(LedPattern pattern);
    LedPattern getPattern() const { return pattern_; }

    /** Call frequently from main loop (non-blocking). */
    void update();

private:
    LedPattern pattern_;
    uint32_t last_toggle_ms_;
    bool led_state_;
    uint8_t phase_;

    void writeLed(bool on);
};

#endif // DOOMBOX_LED_MANAGER_H
