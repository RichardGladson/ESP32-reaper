/**
 * DOOMBOX Protocol definitions
 * Shared between REAPER and future CORE/WRAITH firmware.
 * Versioned and extensible.
 */

#ifndef DOOMBOX_PROTOCOL_H
#define DOOMBOX_PROTOCOL_H

#include <Arduino.h>
#include "config.h"

#pragma pack(push, 1)

// ---------------------------------------------------------------------------
// Packet types (explicit enums, no magic numbers in application code)
// ---------------------------------------------------------------------------
enum class PacketType : uint8_t {
    // Discovery / pairing
    DISCOVERY_BEACON      = 0x01,  // Core broadcasts this
    PAIRING_REQUEST       = 0x02,  // REAPER -> Core
    PAIRING_RESPONSE      = 0x03,  // Core -> REAPER
    HEARTBEAT             = 0x04,  // bidirectional keep-alive

    // Device management
    DEVICE_IDENT          = 0x10,  // identification exchange
    STATUS_REQUEST        = 0x11,
    STATUS_RESPONSE       = 0x12,
    ERROR_RESPONSE        = 0x13,

    // Mode / control
    MODE_COMMAND          = 0x20,  // set operating mode
    RADIO_CONFIG_COMMAND  = 0x21,  // configure radios
    SCAN_TEST_COMMAND     = 0x22,  // start survey/scan
    STOP_COMMAND          = 0x23,  // stop current activity

    // Future expansion
    CUSTOM_PAYLOAD        = 0xF0,
};

enum class DeviceRole : uint8_t {
    UNKNOWN   = 0x00,
    REAPER    = 0x01,
    CORE      = 0x02,
    WRAITH    = 0x03,
};

enum class OperatingMode : uint8_t {
    IDLE              = 0x00,
    BLUETOOTH_SURVEY  = 0x01,
    WIFI_SURVEY       = 0x02,
    RADIO_TEST        = 0x03,
    STOPPED           = 0x04,
};

enum class ErrorCode : uint8_t {
    NONE                  = 0x00,
    UNSUPPORTED_VERSION   = 0x01,
    MALFORMED_PACKET      = 0x02,
    INVALID_COMMAND       = 0x03,
    RADIO_FAILURE         = 0x04,
    NOT_PAIRED            = 0x05,
    BUSY                  = 0x06,
    TIMEOUT               = 0x07,
};

// ---------------------------------------------------------------------------
// Fixed header present in every packet
// ---------------------------------------------------------------------------
struct ProtocolHeader {
    uint8_t  magic;           // PROTOCOL_MAGIC (0xDB)
    uint8_t  version;         // REAPER_PROTOCOL_VERSION
    PacketType type;
    DeviceRole sender_role;
    uint8_t  sender_id[DEVICE_ID_LEN];  // MAC or derived ID
    uint16_t sequence;
    uint8_t  payload_len;     // length of following payload (0-200)
    uint8_t  checksum;        // simple XOR of header bytes (excl. checksum itself)
};

// Maximum total packet size for ESP-NOW (250 bytes practical)
#define PROTOCOL_MAX_PAYLOAD  200
#define PROTOCOL_HEADER_SIZE  sizeof(ProtocolHeader)

struct ProtocolPacket {
    ProtocolHeader header;
    uint8_t payload[PROTOCOL_MAX_PAYLOAD];
};

// ---------------------------------------------------------------------------
// Specific payloads
// ---------------------------------------------------------------------------
struct DiscoveryBeaconPayload {
    uint8_t core_id[DEVICE_ID_LEN];
    uint8_t protocol_version;
    uint8_t flags;            // reserved
};

struct PairingRequestPayload {
    uint8_t reaper_id[DEVICE_ID_LEN];
    uint8_t fw_major;
    uint8_t fw_minor;
    uint8_t radio_count;      // 3 for REAPER
};

struct PairingResponsePayload {
    uint8_t status;           // 0 = accepted
    uint8_t core_id[DEVICE_ID_LEN];
    uint8_t assigned_slot;    // optional
};

struct HeartbeatPayload {
    uint32_t uptime_ms;
    uint8_t  mode;            // OperatingMode
    uint8_t  radio_status;    // bitfield: bit0=R1 ok, bit1=R2, bit2=R3
};

struct StatusResponsePayload {
    uint8_t  mode;
    uint8_t  radio_status;
    uint8_t  core_connected;
    uint16_t free_heap;
    uint32_t uptime_ms;
};

struct ModeCommandPayload {
    uint8_t mode;             // OperatingMode
};

struct RadioConfigPayload {
    uint8_t radio_mask;       // bit0=R1, bit1=R2, bit2=R3
    uint8_t channel;          // 0-125
    uint8_t power_level;      // RF24_PA_* mapped 0-3
    uint8_t data_rate;        // RF24_1MBPS etc.
};

struct ScanTestPayload {
    uint8_t survey_type;      // OperatingMode BLUETOOTH_SURVEY / WIFI_SURVEY
    uint8_t start_channel;
    uint8_t end_channel;
    uint8_t hop_step;
};

#pragma pack(pop)

// ---------------------------------------------------------------------------
// Protocol helpers (implemented in protocol.cpp)
// ---------------------------------------------------------------------------
namespace Protocol {

bool isValidHeader(const ProtocolHeader& hdr);
uint8_t computeChecksum(const ProtocolHeader& hdr);
void fillHeader(ProtocolHeader& hdr, PacketType type, DeviceRole role,
                const uint8_t* sender_id, uint16_t seq, uint8_t payload_len);

size_t packPacket(uint8_t* out, size_t out_len,
                  PacketType type, DeviceRole role,
                  const uint8_t* sender_id, uint16_t seq,
                  const void* payload, uint8_t payload_len);

bool unpackPacket(const uint8_t* data, size_t len, ProtocolPacket& pkt);

const char* packetTypeToString(PacketType t);
const char* modeToString(OperatingMode m);

} // namespace Protocol

#endif // DOOMBOX_PROTOCOL_H
