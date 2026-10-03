/**
 * RadioManager implementation for three nRF24L01 on shared SPI
 */

#include "radio_manager.h"
#include <string.h>

const uint8_t RadioManager::CE_PINS[3]  = { PIN_RADIO1_CE,  PIN_RADIO2_CE,  PIN_RADIO3_CE };
const uint8_t RadioManager::CSN_PINS[3] = { PIN_RADIO1_CSN, PIN_RADIO2_CSN, PIN_RADIO3_CSN };

RadioManager::RadioManager()
    : spi_(nullptr), last_health_ms_(0) {
    for (int i = 0; i < 3; ++i) {
        radios_[i] = nullptr;
        enabled_[i] = true;
        survey_index_[i] = 0;
        memset(&status_[i], 0, sizeof(RadioStatus));
    }
}

bool RadioManager::begin() {
    LOG_I("RadioManager: initializing shared SPI (SCK=%d MISO=%d MOSI=%d)",
          PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI);

    // ESP32-C3: use FSPI (SPI2) or default SPI with explicit pins
    spi_ = new SPIClass(FSPI);
    spi_->begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, -1); // no hardware SS

    bool any_ok = false;
    for (uint8_t i = 0; i < 3; ++i) {
        if (initSingleRadio(i)) {
            any_ok = true;
            LOG_I("Radio %u initialized OK (CE=%u CSN=%u)", i + 1, CE_PINS[i], CSN_PINS[i]);
        } else {
            LOG_E("Radio %u FAILED to initialize (CE=%u CSN=%u)", i + 1, CE_PINS[i], CSN_PINS[i]);
            status_[i].initialized = false;
            status_[i].available = false;
        }
    }

    if (!any_ok) {
        LOG_E("RadioManager: NO radios initialized - survey will be unavailable");
    } else {
        LOG_I("RadioManager: at least one radio ready");
    }
    return any_ok;
}

bool RadioManager::initSingleRadio(uint8_t idx) {
    if (idx >= 3) return false;

    // Clean up previous instance if any
    if (radios_[idx]) {
        delete radios_[idx];
        radios_[idx] = nullptr;
    }

    radios_[idx] = new RF24(CE_PINS[idx], CSN_PINS[idx]);
    // Pass the shared SPI instance
    if (!radios_[idx]->begin(spi_)) {
        delete radios_[idx];
        radios_[idx] = nullptr;
        status_[idx].initialized = false;
        status_[idx].available = false;
        return false;
    }

    // Basic configuration for receive / survey use
    radios_[idx]->setPALevel(RF24_PA_LOW);       // start low; configurable later
    radios_[idx]->setDataRate(RF24_1MBPS);
    radios_[idx]->setChannel(NRF24_DEFAULT_CHANNEL);
    radios_[idx]->setAutoAck(false);             // survey: no ACK needed
    radios_[idx]->setRetries(0, 0);
    radios_[idx]->disableCRC();                  // optional for pure observation
    radios_[idx]->openReadingPipe(0, 0xE7E7E7E7E7LL);
    radios_[idx]->startListening();

    status_[idx].initialized = true;
    status_[idx].available = true;
    status_[idx].channel = NRF24_DEFAULT_CHANNEL;
    status_[idx].power_level = 1; // LOW
    status_[idx].data_rate = 0;   // 1 Mbps
    status_[idx].packets_rx = 0;
    status_[idx].last_activity_ms = millis();
    enabled_[idx] = true;
    return true;
}

bool RadioManager::initRadio(RadioId id) {
    return initSingleRadio(static_cast<uint8_t>(id));
}

void RadioManager::enableRadio(RadioId id, bool enable) {
    uint8_t idx = static_cast<uint8_t>(id);
    if (idx >= 3) return;
    enabled_[idx] = enable;
    if (!enable && radios_[idx]) {
        powerDownRadio(idx);
    } else if (enable && radios_[idx] && status_[idx].initialized) {
        radios_[idx]->powerUp();
        radios_[idx]->startListening();
    }
}

bool RadioManager::isEnabled(RadioId id) const {
    uint8_t idx = static_cast<uint8_t>(id);
    if (idx >= 3) return false;
    return enabled_[idx] && status_[idx].available;
}

bool RadioManager::setChannel(RadioId id, uint8_t channel) {
    uint8_t idx = static_cast<uint8_t>(id);
    if (idx >= 3 || !radios_[idx] || !status_[idx].initialized) return false;
    if (channel > NRF24_CHANNEL_MAX) channel = NRF24_CHANNEL_MAX;
    radios_[idx]->setChannel(channel);
    status_[idx].channel = channel;
    return true;
}

bool RadioManager::setAllChannels(uint8_t ch1, uint8_t ch2, uint8_t ch3) {
    bool ok = true;
    ok &= setChannel(RadioId::RADIO_1, ch1);
    ok &= setChannel(RadioId::RADIO_2, ch2);
    ok &= setChannel(RadioId::RADIO_3, ch3);
    return ok;
}

uint8_t RadioManager::getChannel(RadioId id) const {
    uint8_t idx = static_cast<uint8_t>(id);
    if (idx >= 3) return 0;
    return status_[idx].channel;
}

bool RadioManager::setPowerLevel(RadioId id, uint8_t level) {
    uint8_t idx = static_cast<uint8_t>(id);
    if (idx >= 3 || !radios_[idx] || !status_[idx].initialized) return false;
    rf24_pa_dbm_e pa;
    switch (level) {
        case 0: pa = RF24_PA_MIN; break;
        case 1: pa = RF24_PA_LOW; break;
        case 2: pa = RF24_PA_HIGH; break;
        default: pa = RF24_PA_MAX; level = 3; break;
    }
    radios_[idx]->setPALevel(pa);
    status_[idx].power_level = level;
    return true;
}

bool RadioManager::setDataRate(RadioId id, uint8_t rate) {
    uint8_t idx = static_cast<uint8_t>(id);
    if (idx >= 3 || !radios_[idx] || !status_[idx].initialized) return false;
    rf24_datarate_e dr;
    switch (rate) {
        case 1: dr = RF24_2MBPS; break;
        case 2: dr = RF24_250KBPS; break;
        default: dr = RF24_1MBPS; rate = 0; break;
    }
    radios_[idx]->setDataRate(dr);
    status_[idx].data_rate = rate;
    return true;
}

RadioStatus RadioManager::getStatus(RadioId id) const {
    uint8_t idx = static_cast<uint8_t>(id);
    if (idx >= 3) {
        RadioStatus empty = {};
        return empty;
    }
    return status_[idx];
}

uint8_t RadioManager::getHealthBitfield() const {
    uint8_t bits = 0;
    for (int i = 0; i < 3; ++i) {
        if (status_[i].available && status_[i].initialized) {
            bits |= (1 << i);
        }
    }
    return bits;
}

void RadioManager::surveyStep(uint8_t start_ch, uint8_t end_ch, uint8_t step) {
    if (step == 0) step = 1;
    if (end_ch > NRF24_CHANNEL_MAX) end_ch = NRF24_CHANNEL_MAX;
    if (start_ch > end_ch) return;

    for (uint8_t i = 0; i < 3; ++i) {
        if (!enabled_[i] || !status_[i].initialized || !radios_[i]) continue;

        // Stagger channels across the three radios for wider coverage
        uint8_t offset = (i * step) % (end_ch - start_ch + 1);
        uint8_t ch = start_ch + ((survey_index_[i] + offset) % (end_ch - start_ch + 1));
        radios_[i]->setChannel(ch);
        status_[i].channel = ch;
        survey_index_[i] = (survey_index_[i] + step) % (end_ch - start_ch + 1);

        // Quick non-blocking check for any activity
        checkReceive(static_cast<RadioId>(i));
    }
}

bool RadioManager::checkReceive(RadioId id) {
    uint8_t idx = static_cast<uint8_t>(id);
    if (idx >= 3 || !radios_[idx] || !status_[idx].initialized) return false;

    if (radios_[idx]->available()) {
        uint8_t buf[32];
        radios_[idx]->read(buf, sizeof(buf));
        status_[idx].packets_rx++;
        status_[idx].last_activity_ms = millis();
        // Note: true RSSI only available after packet on some firmwares;
        // RF24 library does not expose a continuous RSSI API.
        return true;
    }
    return false;
}

void RadioManager::update() {
    uint32_t now = millis();
    if (now - last_health_ms_ < RADIO_HEALTH_INTERVAL_MS) return;
    last_health_ms_ = now;

    for (uint8_t i = 0; i < 3; ++i) {
        if (!status_[i].initialized || !radios_[i]) continue;
        // Lightweight presence check: if chip does not respond, mark unavailable
        // RF24 isAvailable() / isChipConnected() can be used if library supports it
        if (radios_[i]->isChipConnected()) {
            status_[i].available = true;
        } else {
            status_[i].available = false;
            LOG_E("Radio %u no longer responding", i + 1);
        }
    }
}

void RadioManager::stopAll() {
    for (uint8_t i = 0; i < 3; ++i) {
        powerDownRadio(i);
    }
}

void RadioManager::powerDownRadio(uint8_t idx) {
    if (idx >= 3 || !radios_[idx]) return;
    radios_[idx]->stopListening();
    radios_[idx]->powerDown();
}
