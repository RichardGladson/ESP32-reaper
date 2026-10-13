/**
 * ESP-NOW manager implementation
 * Automatic discovery and pairing with DOOMBOX CORE.
 */

#include "espnow_manager.h"
#include <string.h>

EspNowManager* EspNowManager::instance_ = nullptr;

EspNowManager::EspNowManager()
    : state_(EspNowState::IDLE),
      core_mac_valid_(false),
      sequence_(0),
      last_discovery_ms_(0),
      last_heartbeat_rx_ms_(0),
      last_heartbeat_tx_ms_(0),
      cmd_handler_(nullptr),
      hb_mode_(0),
      hb_radio_status_(0) {
    memset(local_mac_, 0, 6);
    memset(core_mac_, 0, 6);
    instance_ = this;
}

void EspNowManager::setHeartbeatFields(uint8_t mode, uint8_t radio_status) {
    hb_mode_ = mode;
    hb_radio_status_ = radio_status;
}

bool EspNowManager::begin() {
    state_ = EspNowState::INITIALIZING;
    LOG_I("ESP-NOW: initializing");

    // ESP32-C3: set Wi-Fi to station mode, no connection needed for ESP-NOW
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true);
    delay(50); // short settle; unavoidable at init only

    // Obtain local MAC
    uint8_t mac[6];
    WiFi.macAddress(mac);
    memcpy(local_mac_, mac, 6);
    LOG_I("ESP-NOW: local MAC %02X:%02X:%02X:%02X:%02X:%02X",
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    if (esp_now_init() != ESP_OK) {
        LOG_E("ESP-NOW: esp_now_init failed");
        state_ = EspNowState::ERROR;
        return false;
    }

    esp_now_register_recv_cb(onDataRecv);
    esp_now_register_send_cb(onDataSent);

    // Add broadcast peer for discovery
    uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, bcast, 6);
    peer.channel = ESPNOW_CHANNEL;
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) != ESP_OK) {
        LOG_E("ESP-NOW: failed to add broadcast peer");
        // continue anyway; some stacks allow send without explicit peer
    }

    state_ = EspNowState::SEARCHING;
    last_discovery_ms_ = millis();
    LOG_I("ESP-NOW: entering discovery (waiting for CORE beacon)");
    return true;
}

void EspNowManager::setCommandHandler(CommandHandler handler) {
    cmd_handler_ = handler;
}

void EspNowManager::startDiscovery() {
    if (core_mac_valid_) {
        removePeer(core_mac_);
        core_mac_valid_ = false;
    }
    state_ = EspNowState::SEARCHING;
    last_discovery_ms_ = millis();
    LOG_I("ESP-NOW: restarting discovery");
}

void EspNowManager::onCoreTimeout() {
    LOG_E("ESP-NOW: Core heartbeat timeout");
    startDiscovery();
}

void EspNowManager::update() {
    uint32_t now = millis();

    if (state_ == EspNowState::SEARCHING) {
        // Periodically we could send a presence packet; primarily we listen
        // for Core DISCOVERY_BEACON. Optional: send DEVICE_IDENT occasionally.
        if (now - last_discovery_ms_ >= CORE_DISCOVERY_INTERVAL_MS * 5) {
            last_discovery_ms_ = now;
            // Optional presence announcement
            PairingRequestPayload req = {};
            memcpy(req.reaper_id, local_mac_, DEVICE_ID_LEN);
            req.fw_major = 1;
            req.fw_minor = 0;
            req.radio_count = 3;
            sendBroadcast(PacketType::DEVICE_IDENT, &req, sizeof(req));
        }
    } else if (state_ == EspNowState::PAIRED) {
        // Watchdog for Core heartbeat
        if (now - last_heartbeat_rx_ms_ > CORE_HEARTBEAT_TIMEOUT_MS) {
            onCoreTimeout();
            return;
        }
        // Send our own heartbeat periodically
        if (now - last_heartbeat_tx_ms_ >= 1000) {
            last_heartbeat_tx_ms_ = now;
            HeartbeatPayload hb = {};
            hb.uptime_ms = now;
            hb.mode = hb_mode_;
            hb.radio_status = hb_radio_status_;
            sendToCore(PacketType::HEARTBEAT, &hb, sizeof(hb));
        }
    }
}

bool EspNowManager::sendToCore(PacketType type, const void* payload, uint8_t payload_len) {
    if (!core_mac_valid_ || state_ != EspNowState::PAIRED) {
        return false;
    }
    uint8_t buf[PROTOCOL_HEADER_SIZE + PROTOCOL_MAX_PAYLOAD];
    size_t len = Protocol::packPacket(buf, sizeof(buf), type, DeviceRole::REAPER,
                                      local_mac_, sequence_++, payload, payload_len);
    if (len == 0) return false;
    esp_err_t err = esp_now_send(core_mac_, buf, len);
    return (err == ESP_OK);
}

bool EspNowManager::sendBroadcast(PacketType type, const void* payload, uint8_t payload_len) {
    uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t buf[PROTOCOL_HEADER_SIZE + PROTOCOL_MAX_PAYLOAD];
    size_t len = Protocol::packPacket(buf, sizeof(buf), type, DeviceRole::REAPER,
                                      local_mac_, sequence_++, payload, payload_len);
    if (len == 0) return false;
    esp_err_t err = esp_now_send(bcast, buf, len);
    return (err == ESP_OK);
}

void EspNowManager::onDataRecv(const uint8_t* mac, const uint8_t* data, int len) {
    if (instance_) {
        instance_->handleReceived(mac, data, len);
    }
}

void EspNowManager::onDataSent(const uint8_t* mac, esp_now_send_status_t status) {
    // Optional: log failures at debug level
    if (status != ESP_NOW_SEND_SUCCESS) {
        LOG_D("ESP-NOW send failed");
    }
}

void EspNowManager::handleReceived(const uint8_t* mac, const uint8_t* data, int len) {
    ProtocolPacket pkt;
    if (!Protocol::unpackPacket(data, len, pkt)) {
        LOG_D("ESP-NOW: invalid/malformed packet discarded");
        return;
    }

    // Version check
    if (pkt.header.version > REAPER_PROTOCOL_VERSION + 2) {
        LOG_E("ESP-NOW: unsupported protocol version %u", pkt.header.version);
        return;
    }

    LOG_D("ESP-NOW RX type=%s from %02X:%02X:%02X:%02X:%02X:%02X",
          Protocol::packetTypeToString(pkt.header.type),
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    switch (pkt.header.type) {
        case PacketType::DISCOVERY_BEACON:
            processDiscoveryBeacon(pkt, mac);
            break;
        case PacketType::PAIRING_RESPONSE:
            processPairingResponse(pkt, mac);
            break;
        case PacketType::HEARTBEAT:
            processHeartbeat(pkt);
            break;
        case PacketType::MODE_COMMAND:
        case PacketType::RADIO_CONFIG_COMMAND:
        case PacketType::SCAN_TEST_COMMAND:
        case PacketType::STOP_COMMAND:
        case PacketType::STATUS_REQUEST:
            processCommand(pkt, mac);
            break;
        default:
            LOG_D("ESP-NOW: unhandled packet type");
            break;
    }
}

void EspNowManager::processDiscoveryBeacon(const ProtocolPacket& pkt, const uint8_t* mac) {
    if (state_ != EspNowState::SEARCHING) return;
    if (pkt.header.sender_role != DeviceRole::CORE &&
        pkt.header.sender_role != DeviceRole::WRAITH) {
        return;
    }

    LOG_I("ESP-NOW: CORE discovery beacon received");
    // Respond with pairing request
    PairingRequestPayload req = {};
    memcpy(req.reaper_id, local_mac_, DEVICE_ID_LEN);
    req.fw_major = 1;
    req.fw_minor = 0;
    req.radio_count = 3;

    // Temporarily add the Core as peer so we can unicast the request
    addPeer(mac);
    memcpy(core_mac_, mac, 6);
    // Send request (still in SEARCHING; will become PAIRED on response)
    uint8_t buf[PROTOCOL_HEADER_SIZE + PROTOCOL_MAX_PAYLOAD];
    size_t len = Protocol::packPacket(buf, sizeof(buf), PacketType::PAIRING_REQUEST,
                                      DeviceRole::REAPER, local_mac_, sequence_++,
                                      &req, sizeof(req));
    if (len > 0) {
        esp_now_send(mac, buf, len);
        LOG_I("ESP-NOW: pairing request sent");
    }
}

void EspNowManager::processPairingResponse(const ProtocolPacket& pkt, const uint8_t* mac) {
    if (pkt.header.payload_len < sizeof(PairingResponsePayload)) return;
    PairingResponsePayload resp;
    memcpy(&resp, pkt.payload, sizeof(resp));

    if (resp.status != 0) {
        LOG_E("ESP-NOW: pairing rejected by Core (status=%u)", resp.status);
        removePeer(mac);
        return;
    }

    memcpy(core_mac_, mac, 6);
    core_mac_valid_ = true;
    addPeer(mac);
    state_ = EspNowState::PAIRED;
    last_heartbeat_rx_ms_ = millis();
    last_heartbeat_tx_ms_ = millis();
    LOG_I("ESP-NOW: PAIRED with CORE %02X:%02X:%02X:%02X:%02X:%02X",
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void EspNowManager::processHeartbeat(const ProtocolPacket& pkt) {
    if (state_ == EspNowState::PAIRED) {
        last_heartbeat_rx_ms_ = millis();
    }
}

void EspNowManager::processCommand(const ProtocolPacket& pkt, const uint8_t* mac) {
    // Only accept commands after successful pairing and from the known Core
    if (state_ != EspNowState::PAIRED || !core_mac_valid_) {
        LOG_E("ESP-NOW: command rejected - not paired");
        return;
    }
    if (memcmp(mac, core_mac_, 6) != 0) {
        LOG_E("ESP-NOW: command rejected - unknown sender");
        return;
    }
    if (cmd_handler_) {
        cmd_handler_(pkt, mac);
    }
}

bool EspNowManager::addPeer(const uint8_t* mac) {
    if (esp_now_is_peer_exist(mac)) {
        return true;
    }
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = ESPNOW_CHANNEL;
    peer.encrypt = false;
    esp_err_t err = esp_now_add_peer(&peer);
    if (err != ESP_OK) {
        LOG_E("ESP-NOW: add_peer failed (%d)", err);
        return false;
    }
    return true;
}

void EspNowManager::removePeer(const uint8_t* mac) {
    if (esp_now_is_peer_exist(mac)) {
        esp_now_del_peer(mac);
    }
}
