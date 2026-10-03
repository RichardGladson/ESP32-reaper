/**
 * Non-blocking button manager with debounce, short press and long hold
 * GPIO10, active-low with internal pull-up.
 */

#ifndef DOOMBOX_BUTTON_MANAGER_H
#define DOOMBOX_BUTTON_MANAGER_H

#include <Arduino.h>
#include "config.h"

enum class ButtonEvent : uint8_t {
    NONE = 0,
    SHORT_PRESS,
    LONG_HOLD,       // fired once when hold threshold reached
};

class ButtonManager {
public:
    ButtonManager();

    void begin();

    /**
     * Call every loop iteration. Returns the event that occurred
     * since the last call (or NONE).
     */
    ButtonEvent update();

    bool isPressed() const { return stable_pressed_; }

private:
    bool raw_state_;
    bool stable_pressed_;
    bool hold_fired_;
    uint32_t last_change_ms_;
    uint32_t press_start_ms_;
};

#endif // DOOMBOX_BUTTON_MANAGER_H
