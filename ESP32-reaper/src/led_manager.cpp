/**
 * Non-blocking LED patterns
 */

#include "led_manager.h"

LedManager::LedManager()
    : pattern_(LedPattern::OFF), last_toggle_ms_(0), led_state_(false), phase_(0) {}

void LedManager::begin() {
    pinMode(PIN_STATUS_LED, OUTPUT);
    writeLed(false);
    LOG_I("LED manager ready on GPIO%u", PIN_STATUS_LED);
}

void LedManager::setPattern(LedPattern pattern) {
    if (pattern_ == pattern) return;
    pattern_ = pattern;
    phase_ = 0;
    last_toggle_ms_ = millis();
    // Immediate visual feedback for solid states
    if (pattern == LedPattern::SOLID_ON || pattern == LedPattern::CORE_CONNECTED) {
        writeLed(true);
    } else if (pattern == LedPattern::OFF) {
        writeLed(false);
    }
}

void LedManager::update() {
    uint32_t now = millis();
    uint32_t interval = 0;
    bool should_toggle = false;

    switch (pattern_) {
        case LedPattern::OFF:
            writeLed(false);
            return;

        case LedPattern::SOLID_ON:
        case LedPattern::CORE_CONNECTED:
            writeLed(true);
            return;

        case LedPattern::BOOT_PULSE:
            // ~1 Hz soft pulse (500 ms on / 500 ms off)
            interval = 500;
            break;

        case LedPattern::INIT_RAPID:
            // ~10 Hz
            interval = 50;
            break;

        case LedPattern::SEARCHING_CORE:
            // Short blink every 800 ms
            if (phase_ == 0) {
                interval = 100; // on time
            } else {
                interval = 700; // off time
            }
            break;

        case LedPattern::STANDALONE_BT:
            // Double blink pattern
            if (phase_ == 0 || phase_ == 2) interval = 80;
            else if (phase_ == 1) interval = 80;
            else interval = 600; // pause
            break;

        case LedPattern::STANDALONE_WIFI:
            // Triple short blink
            if (phase_ <= 4) interval = 70;
            else interval = 500;
            break;

        case LedPattern::ERROR_RAPID:
            // Very fast 20 Hz style
            interval = 40;
            break;

        default:
            writeLed(false);
            return;
    }

    if (now - last_toggle_ms_ >= interval) {
        last_toggle_ms_ = now;
        led_state_ = !led_state_;
        writeLed(led_state_);

        // Advance phase for multi-step patterns
        if (pattern_ == LedPattern::SEARCHING_CORE) {
            phase_ = (phase_ + 1) % 2;
        } else if (pattern_ == LedPattern::STANDALONE_BT) {
            phase_ = (phase_ + 1) % 4;
        } else if (pattern_ == LedPattern::STANDALONE_WIFI) {
            phase_ = (phase_ + 1) % 6;
        }
    }
}

void LedManager::writeLed(bool on) {
    digitalWrite(PIN_STATUS_LED, on ? HIGH : LOW);
    led_state_ = on;
}
