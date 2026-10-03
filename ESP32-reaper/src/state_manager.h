/**
 * Explicit non-blocking state machine for DOOMBOX REAPER
 */

#ifndef DOOMBOX_STATE_MANAGER_H
#define DOOMBOX_STATE_MANAGER_H

#include <Arduino.h>
#include "config.h"
#include "protocol.h"
#include "radio_manager.h"
#include "espnow_manager.h"
#include "button_manager.h"
#include "led_manager.h"

enum class SystemState : uint8_t {
    BOOT = 0,
    INITIALIZING,
    CORE_DISCOVERY,
    CORE_CONNECTED,
    STANDALONE,
    ERROR,
};

class StateManager {
public:
    StateManager(RadioManager& radio, EspNowManager& espnow,
                 ButtonManager& button, LedManager& led);

    void begin();
    void update();  // call every loop

    SystemState getState() const { return state_; }
    OperatingMode getStandaloneMode() const { return standalone_mode_; }
    bool isCoreControlled() const { return state_ == SystemState::CORE_CONNECTED; }

    /** Called by ESP-NOW command handler */
    void handleCoreCommand(const ProtocolPacket& pkt);

private:
    SystemState state_;
    OperatingMode standalone_mode_;
    bool survey_active_;
    uint32_t last_survey_step_ms_;
    uint8_t survey_start_;
    uint8_t survey_end_;
    uint8_t survey_step_;

    RadioManager& radio_;
    EspNowManager& espnow_;
    ButtonManager& button_;
    LedManager& led_;

    void transitionTo(SystemState next);
    void handleButtonEvent(ButtonEvent ev);
    void runSurveyStep();
    void applyModeCommand(OperatingMode mode);
    void sendStatusResponse();
};

#endif // DOOMBOX_STATE_MANAGER_H
