#pragma once

#import <cstdint>

// Port number enum
enum PortNum
{
  PORTNUM_UNKNOWN = 0,         // binary - unknown packet format
  PORTNUM_TEXT_MESSAGE = 1,    // utf-8 - chat message
  PORTNUM_REMOTE_HARDWARE = 2, // NodeRemoteHardwarePinSchema
  PORTNUM_POSITION = 3,        // PositionSchema
  PORTNUM_NODEINFO = 4,        // UserSchema
  PORTNUM_ROUTING = 5,         // RoutingSchema
  PORTNUM_ADMIN = 6,           // AdminMessageSchema
  PORTNUM_TEXT_COMPRESSED = 7, // Unishox2 Compressed utf-8
  PORTNUM_WAYPOINT = 8,        // WaypointSchema
  PORTNUM_AUDIO = 9,           // codec2 packets
  PORTNUM_DETECTION_SENSOR = 10,
  PORTNUM_REPLY = 32, // ping - replies to all packets
  PORTNUM_IP_TUNNEL_APP = 33,
  PORTNUM_SERIAL_APP = 64,
  PORTNUM_STORE_FORWARD_APP = 65,
  PORTNUM_RANGE_TEST_APP = 66,
  PORTNUM_TELEMETRY_APP = 67, // Telemetry
  PORTNUM_ZPS_APP = 68,
  PORTNUM_SIMULATOR_APP = 69,
  PORTNUM_TRACEROUTE_APP = 70, // traceroute
  PORTNUM_NEIGHBORINFO_APP = 71,
  PORTNUM_ATAK_PLUGIN = 72,
  PORTNUM_MAP_REPORT_APP = 73 // Map report
};

typedef struct
{
  uint32_t to;        // destination node ID
  uint32_t from;      // source node ID
  uint32_t packet_id; // packet ID
  uint8_t flags;      // packed flags byte
  uint8_t channel;    // channel number
  uint8_t next_hop;   // next hop node
  uint8_t relay_node; // relay node
} PacketHeader;

// Data object structure (simplified Mesh.DataSchema)
typedef struct
{
  uint8_t portnum;    // port number
  uint8_t *payload;   // message payload
  std::size_t payload_len; // payload length
  bool want_response; // want response flag
} DataObj;

typedef struct {
    uint8_t portnum;
    char payload_text[256];
    std::size_t payload_len;
    bool want_response;
    uint32_t dest;
    uint32_t source;
    uint32_t request_id;
    uint32_t reply_id;
    uint32_t emoji;
    uint32_t bitfield;
    bool has_portnum;
    bool has_payload;
    bool has_want_response;
    bool has_dest;
    bool has_source;
    bool has_request_id;
    bool has_reply_id;
    bool has_emoji;
    bool has_bitfield;
} DecodedDataMessage;

// Channel identifier - minutemesh channel
static const uint8_t MINUTEMESH_KEY[16] = {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29,
                                           0x07, 0x59, 0xf0, 0xbc, 0xff, 0xab,
                                           0xcf, 0x4e, 0x69, 0xe5};

void decodeProtobufFromHex(const char *);

int buildWirePacket(
    uint8_t *,
    std::size_t,
    uint32_t,
    uint32_t,
    uint8_t,
    bool,
    bool,
    uint8_t,
    uint8_t,
    uint8_t,
    uint8_t,
    const DataObj *,
    const uint8_t *);

int hexStringToBytes(const char*, uint8_t*, int);

int readVarint(const uint8_t*, int, int, uint32_t*);

int decodeProtobufData(const uint8_t*, int, DecodedDataMessage*);

void decodeProtobufFromHex(const char*);
