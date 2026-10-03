/**
 * ESP-NOW manager for automatic Core discovery, pairing and command transport.
 * Designed so CORE firmware can be written later without rewriting REAPER.
 */

#ifndef DOOMBOX_ESPNOW_MANAGER_H
#define DOOMBOX_ESPNOW_MANAGER_H

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include "config.h"
#include "protocol.h"

enum class EspNowState : uint8_t {
    IDLE = 0,
    INITIALIZING,
    SEARCHING,
    PAIRED,
    ERROR,
};

using CommandHandler = void (*)(const ProtocolPacket& pkt, const uint8_t* mac);

class EspNowManager {
public:
    EspNowManager();

    bool begin();
    void update();  // non-blocking, call from loop

    EspNowState getState() const { return state_; }
    bool isPaired() const { return state_ == EspNowState::PAIRED; }
    const uint8_t* getCoreMac() const { return core_mac_; }
    const uint8_t* getLocalMac() const { return local_mac_; }

    /** Register callback invoked for validated command packets after pairing. */
    void setCommandHandler(CommandHandler handler);

    /** Send a packet to the currently paired Core (or broadcast if searching). */
    bool sendToCore(PacketType type, const void* payload, uint8_t payload_len);
    bool sendBroadcast(PacketType type, const void* payload, uint8_t payload_len);

    /** Force re-entry into discovery. */
    void startDiscovery();

    /** Called when Core heartbeat times out. */
    void onCoreTimeout();

private:
    EspNowState state_;
    uint8_t local_mac_[6];
    uint8_t core_mac_[6];
    bool core_mac_valid_;
    uint16_t sequence_;
    uint32_t last_discovery_ms_;
    uint32_t last_heartbeat_rx_ms_;
    uint32_t last_heartbeat_tx_ms_;
    CommandHandler cmd_handler_;

    static EspNowManager* instance_;  // for static callbacks

    static void onDataRecv(const uint8_t* mac, const uint8_t* data, int len);
    static void onDataSent(const uint8_t* mac, esp_now_send_status_t status);

    void handleReceived(const uint8_t* mac, const uint8_t* data, int len);
    void processDiscoveryBeacon(const ProtocolPacket& pkt, const uint8_t* mac);
    void processPairingResponse(const ProtocolPacket& pkt, const uint8_t* mac);
    void processHeartbeat(const ProtocolPacket& pkt);
    void processCommand(const ProtocolPacket& pkt, const uint8_t* mac);

    bool addPeer(const uint8_t* mac);
    void removePeer(const uint8_t* mac);
};

#endif // DOOMBOX_ESPNOW_MANAGER_H
