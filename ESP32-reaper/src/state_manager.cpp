/**
 * State machine implementation
 */

#include "state_manager.h"
#include <string.h>

StateManager::StateManager(RadioManager& radio, EspNowManager& espnow,
                           ButtonManager& button, LedManager& led)
    : state_(SystemState::BOOT),
      standalone_mode_(OperatingMode::BLUETOOTH_SURVEY),
      survey_active_(false),
      last_survey_step_ms_(0),
      survey_start_(SURVEY_BT_CHANNEL_START),
      survey_end_(SURVEY_BT_CHANNEL_END),
      survey_step_(2),
      radio_(radio),
      espnow_(espnow),
      button_(button),
      led_(led) {}

void StateManager::begin() {
    transitionTo(SystemState::BOOT);
}

void StateManager::transitionTo(SystemState next) {
    if (state_ == next) return;
    LOG_I("State: %u -> %u", static_cast<unsigned>(state_), static_cast<unsigned>(next));
    state_ = next;

    switch (state_) {
        case SystemState::BOOT:
            led_.setPattern(LedPattern::BOOT_PULSE);
            break;
        case SystemState::INITIALIZING:
            led_.setPattern(LedPattern::INIT_RAPID);
            break;
        case SystemState::CORE_DISCOVERY:
            led_.setPattern(LedPattern::SEARCHING_CORE);
            survey_active_ = false;
            radio_.stopAll();
            break;
        case SystemState::CORE_CONNECTED:
            led_.setPattern(LedPattern::CORE_CONNECTED);
            // Keep any active survey running; Core commands control start/stop
            break;
        case SystemState::STANDALONE:
            if (standalone_mode_ == OperatingMode::BLUETOOTH_SURVEY) {
                led_.setPattern(LedPattern::STANDALONE_BT);
                survey_start_ = SURVEY_BT_CHANNEL_START;
                survey_end_   = SURVEY_BT_CHANNEL_END;
            } else {
                led_.setPattern(LedPattern::STANDALONE_WIFI);
                survey_start_ = SURVEY_WIFI_CHANNEL_START;
                survey_end_   = SURVEY_WIFI_CHANNEL_END;
            }
            break;
        case SystemState::ERROR:
            led_.setPattern(LedPattern::ERROR_RAPID);
            survey_active_ = false;
            radio_.stopAll();
            break;
    }
}

void StateManager::update() {
    // Always update peripheral managers (non-blocking)
    led_.update();
    radio_.update();
    espnow_.update();

    ButtonEvent btn = button_.update();

    switch (state_) {
        case SystemState::BOOT:
            transitionTo(SystemState::INITIALIZING);
            break;

        case SystemState::INITIALIZING:
            // Radios and ESP-NOW are initialized from main before state machine runs
            // Move to discovery
            transitionTo(SystemState::CORE_DISCOVERY);
            break;

        case SystemState::CORE_DISCOVERY:
            if (espnow_.isPaired()) {
                transitionTo(SystemState::CORE_CONNECTED);
            }
            // Button still usable for local emergency stop / force standalone if desired
            // Per requirements: when not connected, button controls standalone
            // Here we allow short press to enter standalone immediately
            if (btn == ButtonEvent::SHORT_PRESS) {
                LOG_I("Button: forcing STANDALONE while searching");
                transitionTo(SystemState::STANDALONE);
            }
            break;

        case SystemState::CORE_CONNECTED:
            if (!espnow_.isPaired()) {
                LOG_I("Core connection lost -> STANDALONE");
                transitionTo(SystemState::STANDALONE);
                break;
            }
            // Feed live mode/radio health into ESP-NOW heartbeats
            espnow_.setHeartbeatFields(
                static_cast<uint8_t>(standalone_mode_),
                radio_.getHealthBitfield());
            // Continue survey stepping under Core control
            if (survey_active_) {
                runSurveyStep();
            }
            // Emergency local stop on long hold
            if (btn == ButtonEvent::LONG_HOLD) {
                LOG_I("Button: emergency STOP while Core-controlled");
                survey_active_ = false;
                radio_.stopAll();
                sendStatusResponse();
            }
            break;

        case SystemState::STANDALONE:
            handleButtonEvent(btn);
            if (survey_active_) {
                runSurveyStep();
            }
            // If Core appears later, automatically switch
            if (espnow_.isPaired()) {
                LOG_I("Core appeared -> CORE_CONNECTED");
                transitionTo(SystemState::CORE_CONNECTED);
            }
            break;

        case SystemState::ERROR:
            // Stay in error until external reset or successful re-init
            if (btn == ButtonEvent::SHORT_PRESS) {
                LOG_I("Button: attempting recovery from ERROR");
                transitionTo(SystemState::CORE_DISCOVERY);
            }
            break;
    }
}

void StateManager::handleButtonEvent(ButtonEvent ev) {
    if (ev == ButtonEvent::NONE) return;

    if (ev == ButtonEvent::LONG_HOLD) {
        // Toggle between BT and Wi-Fi survey modes
        if (standalone_mode_ == OperatingMode::BLUETOOTH_SURVEY) {
            standalone_mode_ = OperatingMode::WIFI_SURVEY;
            survey_start_ = SURVEY_WIFI_CHANNEL_START;
            survey_end_   = SURVEY_WIFI_CHANNEL_END;
            led_.setPattern(LedPattern::STANDALONE_WIFI);
            LOG_I("Standalone mode -> WIFI_SURVEY");
        } else {
            standalone_mode_ = OperatingMode::BLUETOOTH_SURVEY;
            survey_start_ = SURVEY_BT_CHANNEL_START;
            survey_end_   = SURVEY_BT_CHANNEL_END;
            led_.setPattern(LedPattern::STANDALONE_BT);
            LOG_I("Standalone mode -> BLUETOOTH_SURVEY");
        }
        survey_active_ = false; // require short press to start after mode change
        radio_.stopAll();
    } else if (ev == ButtonEvent::SHORT_PRESS) {
        // Execute currently selected function (toggle survey on/off)
        survey_active_ = !survey_active_;
        if (survey_active_) {
            LOG_I("Survey STARTED (%s) ch %u-%u step %u",
                  Protocol::modeToString(standalone_mode_),
                  survey_start_, survey_end_, survey_step_);
            // Ensure radios are powered and listening
            for (int i = 0; i < 3; ++i) {
                radio_.enableRadio(static_cast<RadioId>(i), true);
            }
        } else {
            LOG_I("Survey STOPPED");
            radio_.stopAll();
        }
    }
}

void StateManager::runSurveyStep() {
    uint32_t now = millis();
    if (now - last_survey_step_ms_ < SURVEY_STEP_INTERVAL_MS) return;
    last_survey_step_ms_ = now;
    radio_.surveyStep(survey_start_, survey_end_, survey_step_);
}

void StateManager::handleCoreCommand(const ProtocolPacket& pkt) {
    if (state_ != SystemState::CORE_CONNECTED) {
        LOG_E("Core command ignored - not in CORE_CONNECTED");
        return;
    }

    switch (pkt.header.type) {
        case PacketType::STATUS_REQUEST:
            sendStatusResponse();
            break;

        case PacketType::MODE_COMMAND:
            if (pkt.header.payload_len >= sizeof(ModeCommandPayload)) {
                ModeCommandPayload m;
                memcpy(&m, pkt.payload, sizeof(m));
                applyModeCommand(static_cast<OperatingMode>(m.mode));
            }
            break;

        case PacketType::RADIO_CONFIG_COMMAND:
            if (pkt.header.payload_len >= sizeof(RadioConfigPayload)) {
                RadioConfigPayload cfg;
                memcpy(&cfg, pkt.payload, sizeof(cfg));
                for (int i = 0; i < 3; ++i) {
                    if (cfg.radio_mask & (1 << i)) {
                        radio_.setChannel(static_cast<RadioId>(i), cfg.channel);
                        radio_.setPowerLevel(static_cast<RadioId>(i), cfg.power_level);
                        radio_.setDataRate(static_cast<RadioId>(i), cfg.data_rate);
                    }
                }
                LOG_I("Radio config applied (mask=0x%02X ch=%u)", cfg.radio_mask, cfg.channel);
            }
            break;

        case PacketType::SCAN_TEST_COMMAND:
            if (pkt.header.payload_len >= sizeof(ScanTestPayload)) {
                ScanTestPayload sc;
                memcpy(&sc, pkt.payload, sizeof(sc));
                survey_start_ = sc.start_channel;
                survey_end_   = sc.end_channel;
                survey_step_  = sc.hop_step ? sc.hop_step : 1;
                standalone_mode_ = static_cast<OperatingMode>(sc.survey_type);
                survey_active_ = true;
                for (int i = 0; i < 3; ++i) {
                    radio_.enableRadio(static_cast<RadioId>(i), true);
                }
                LOG_I("Core SCAN command: type=%u %u-%u step %u",
                      sc.survey_type, survey_start_, survey_end_, survey_step_);
                sendStatusResponse();
            }
            break;

        case PacketType::STOP_COMMAND:
            survey_active_ = false;
            radio_.stopAll();
            LOG_I("Core STOP command");
            sendStatusResponse();
            break;

        default:
            LOG_D("Unhandled Core command");
            break;
    }
}

void StateManager::applyModeCommand(OperatingMode mode) {
    LOG_I("Core MODE command: %s", Protocol::modeToString(mode));
    if (mode == OperatingMode::STOPPED || mode == OperatingMode::IDLE) {
        survey_active_ = false;
        radio_.stopAll();
    } else if (mode == OperatingMode::BLUETOOTH_SURVEY ||
               mode == OperatingMode::WIFI_SURVEY) {
        standalone_mode_ = mode;
        if (mode == OperatingMode::BLUETOOTH_SURVEY) {
            survey_start_ = SURVEY_BT_CHANNEL_START;
            survey_end_   = SURVEY_BT_CHANNEL_END;
        } else {
            survey_start_ = SURVEY_WIFI_CHANNEL_START;
            survey_end_   = SURVEY_WIFI_CHANNEL_END;
        }
        survey_active_ = true;
        for (int i = 0; i < 3; ++i) {
            radio_.enableRadio(static_cast<RadioId>(i), true);
        }
    }
    sendStatusResponse();
}

void StateManager::sendStatusResponse() {
    StatusResponsePayload st = {};
    st.mode = static_cast<uint8_t>(standalone_mode_);
    st.radio_status = radio_.getHealthBitfield();
    st.core_connected = espnow_.isPaired() ? 1 : 0;
    st.free_heap = ESP.getFreeHeap();
    st.uptime_ms = millis();
    espnow_.sendToCore(PacketType::STATUS_RESPONSE, &st, sizeof(st));
}
