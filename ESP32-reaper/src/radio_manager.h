/**
 * RadioManager - manages three independent nRF24L01 instances
 * on a shared SPI bus with individual CE/CSN lines.
 *
 * Hardware limitations (documented):
 * - nRF24L01 does not provide continuous energy detection / RSSI
 *   without a received packet. Available RSSI is only valid after
 *   a successful packet reception (or via the limited RPD bit on
 *   some clones).
 * - Channel range is 0-125 (2400-2525 MHz). This overlaps the
 *   2.4 GHz ISM band used by Bluetooth and Wi-Fi but does not
 *   map 1:1 to Bluetooth advertising channels or Wi-Fi channel
 *   numbers.
 * - This firmware performs passive observation / channel stepping
 *   only. No continuous carrier or noise transmission is implemented.
 */

#ifndef DOOMBOX_RADIO_MANAGER_H
#define DOOMBOX_RADIO_MANAGER_H

#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>
#include "config.h"

enum class RadioId : uint8_t {
    RADIO_1 = 0,
    RADIO_2 = 1,
    RADIO_3 = 2,
    RADIO_COUNT = 3
};

struct RadioStatus {
    bool     initialized;
    bool     available;
    uint8_t  channel;
    uint8_t  power_level;   // 0-3
    uint8_t  data_rate;     // 0=1Mbps, 1=2Mbps, 2=250kbps
    uint32_t packets_rx;
    uint32_t last_activity_ms;
};

class RadioManager {
public:
    RadioManager();

    /**
     * Initialize SPI bus and all three radios.
     * Returns true if at least one radio initialized successfully.
     * Individual failures are recorded in status and do not halt firmware.
     */
    bool begin();

    /** Re-attempt initialization of a specific radio. */
    bool initRadio(RadioId id);

    void enableRadio(RadioId id, bool enable);
    bool isEnabled(RadioId id) const;

    bool setChannel(RadioId id, uint8_t channel);
    bool setAllChannels(uint8_t ch1, uint8_t ch2, uint8_t ch3);
    uint8_t getChannel(RadioId id) const;

    bool setPowerLevel(RadioId id, uint8_t level); // 0=MIN .. 3=MAX
    bool setDataRate(RadioId id, uint8_t rate);

    RadioStatus getStatus(RadioId id) const;
    uint8_t getHealthBitfield() const; // bit0=R1, bit1=R2, bit2=R3

    /**
     * Non-blocking survey step: advance channel on enabled radios
     * within the configured range. Call periodically from main loop.
     */
    void surveyStep(uint8_t start_ch, uint8_t end_ch, uint8_t step);

    /**
     * Attempt to read any available packet on a radio (non-blocking).
     * Returns true if a packet was present.
     * Payload is discarded for survey mode; only presence/RSSI-like
     * indicators are useful given hardware limits.
     */
    bool checkReceive(RadioId id);

    /** Periodic health check - call from main loop. */
    void update();

    /** Stop all radio activity (power down). */
    void stopAll();

private:
    SPIClass* spi_;
    RF24* radios_[3];
    RadioStatus status_[3];
    bool enabled_[3];
    uint8_t survey_index_[3];
    uint32_t last_health_ms_;

    static const uint8_t CE_PINS[3];
    static const uint8_t CSN_PINS[3];

    bool initSingleRadio(uint8_t idx);
    void powerDownRadio(uint8_t idx);
};

#endif // DOOMBOX_RADIO_MANAGER_H
