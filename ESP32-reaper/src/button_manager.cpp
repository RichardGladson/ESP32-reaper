/**
 * Non-blocking debounced button handling
 */

#include "button_manager.h"

ButtonManager::ButtonManager()
    : raw_state_(false), stable_pressed_(false), hold_fired_(false),
      last_change_ms_(0), press_start_ms_(0) {}

void ButtonManager::begin() {
    pinMode(PIN_MODE_BUTTON, INPUT_PULLUP);
    // Active-low: pressed = LOW
    raw_state_ = (digitalRead(PIN_MODE_BUTTON) == LOW);
    stable_pressed_ = raw_state_;
    last_change_ms_ = millis();
    LOG_I("Button manager ready on GPIO%u (active-low)", PIN_MODE_BUTTON);
}

ButtonEvent ButtonManager::update() {
    ButtonEvent event = ButtonEvent::NONE;
    uint32_t now = millis();
    bool current = (digitalRead(PIN_MODE_BUTTON) == LOW);

    // Debounce: only accept a state change after BUTTON_DEBOUNCE_MS of stability
    if (current != raw_state_) {
        raw_state_ = current;
        last_change_ms_ = now;
    }

    if ((now - last_change_ms_) >= BUTTON_DEBOUNCE_MS) {
        if (raw_state_ != stable_pressed_) {
            // State transition after debounce
            if (raw_state_) {
                // Pressed
                stable_pressed_ = true;
                press_start_ms_ = now;
                hold_fired_ = false;
            } else {
                // Released
                if (stable_pressed_ && !hold_fired_) {
                    // Short press (released before hold threshold)
                    event = ButtonEvent::SHORT_PRESS;
                }
                stable_pressed_ = false;
                hold_fired_ = false;
            }
        }
    }

    // Long-hold detection (while still pressed)
    if (stable_pressed_ && !hold_fired_) {
        if ((now - press_start_ms_) >= BUTTON_HOLD_MS) {
            hold_fired_ = true;
            event = ButtonEvent::LONG_HOLD;
        }
    }

    return event;
}
