#include "CraftPacket.h"
#include "Utils.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static int tryDecryptChannelPacket(const uint8_t *packet, const uint8_t *channelKey, const uint8_t *extraNonce) {
    return 0; // placeholder return
}

static void encryptChannelPacket(const uint8_t *channelKey, const char* payload, const uint8_t from, const uint8_t packetId, const uint8_t extraNonce[8]) {
    mesh::Utils::encryptAESCtr(from, packetId, strlen(payload), (uint8_t*)payload);
}

static int* uint32ToUnit8Array(uint32_t value) {
    static int arr[4];
    arr[0] = (value >> 24) & 0xFF;
    arr[1] = (value >> 16) & 0xFF;
    arr[2] = (value >> 8) & 0xFF;
    arr[3] = value & 0xFF;
    return arr;
}

static void randomBytes(uint8_t* buffer, int length) {
    for (int i = 0; i < length; i++) {
        buffer[i] = rand() % 256; // simple random byte generation
    }
}

// Pack flags byte (equivalent to packFlagsByte in JS)
static uint8_t packFlagsByte(uint8_t hop_limit, bool want_ack, bool via_mqtt, uint8_t hop_start) {
    uint8_t hl = hop_limit & 0x7;
    uint8_t wa = want_ack ? 1 : 0;
    uint8_t vm = via_mqtt ? 1 : 0;
    uint8_t hs = hop_start & 0x7;
    return (hs << 5) | (vm << 4) | (wa << 3) | hl;
}

// Convert uint32 to big-endian bytes (for header)
static void uint32ToBytes(uint32_t value, uint8_t* bytes) {
    bytes[0] = (value >> 24) & 0xFF;
    bytes[1] = (value >> 16) & 0xFF;
    bytes[2] = (value >> 8) & 0xFF;
    bytes[3] = value & 0xFF;
}

// REAL STUFF
/**
 * Build a wire packet equivalent to JavaScript buildWirePacket function
 * Returns the packet length, or -1 on error
 */
int buildWirePacket(uint8_t* output_buffer, std::size_t buffer_size,
                          uint32_t to, uint32_t from,
                          uint8_t hop_limit, bool want_ack, bool via_mqtt, uint8_t hop_start,
                          uint8_t channel, uint8_t next_hop, uint8_t relay_node,
                          const DataObj* data_obj, const uint8_t* channel_key) {

    if (!output_buffer || !data_obj || buffer_size < 16) {
        return -1; // Error: invalid parameters
    }

    // Generate random packet ID (4 bytes)
    uint8_t packet_id_bytes[4];
    randomBytes(packet_id_bytes, 4);
    uint32_t packet_id = (packet_id_bytes[0] << 24) | (packet_id_bytes[1] << 16) |
                        (packet_id_bytes[2] << 8) | packet_id_bytes[3];

    // Create packet header (16 bytes)
    PacketHeader header;
    header.to = to;
    header.from = from;
    header.packet_id = packet_id;
    header.flags = packFlagsByte(hop_limit, want_ack, via_mqtt, hop_start);
    header.channel = channel;
    header.next_hop = next_hop;
    header.relay_node = relay_node;

    // Write header to buffer in big-endian format
    uint32ToBytes(header.to, &output_buffer[0]);
    uint32ToBytes(header.from, &output_buffer[4]);
    uint32ToBytes(header.packet_id, &output_buffer[8]);
    output_buffer[12] = header.flags;
    output_buffer[13] = header.channel;
    output_buffer[14] = header.next_hop;
    output_buffer[15] = header.relay_node;

    int packet_len = 16; // Header length

    // Add payload if present
    if (data_obj->payload && data_obj->payload_len > 0) {
        // Simple protobuf encoding for portnum + payload
        uint8_t protobuf_buffer[512];
        int protobuf_len = 0;

        // Field 1: portnum (varint)
        if (data_obj->portnum != 0) {
            protobuf_buffer[protobuf_len++] = (1 << 3) | 0; // field 1, wire type 0
            protobuf_buffer[protobuf_len++] = data_obj->portnum;
        }

        // Field 2: payload (length-delimited)
        if (data_obj->payload_len > 0) {
            protobuf_buffer[protobuf_len++] = (2 << 3) | 2; // field 2, wire type 2
            protobuf_buffer[protobuf_len++] = data_obj->payload_len; // assume length < 128
            memcpy(&protobuf_buffer[protobuf_len], data_obj->payload, data_obj->payload_len);
            protobuf_len += data_obj->payload_len;
        }

        // Field 9: want_response (varint) - always include
        protobuf_buffer[protobuf_len++] = (9 << 3) | 0; // field 9, wire type 0
        protobuf_buffer[protobuf_len++] = data_obj->want_response ? 1 : 0;

        // Check if we have enough space
        if (packet_len + protobuf_len > buffer_size) {
            return -1; // Not enough space
        }

        // Create nonce for encryption using makeNonce function
        uint8_t nonce[16];
        mesh::Utils::makeNonce(nonce, header.from, header.packet_id);
        
        // Copy protobuf data to output buffer for in-place encryption
        memcpy(&output_buffer[packet_len], protobuf_buffer, protobuf_len);
        
        // Encrypt the protobuf data in-place using AES-CTR
        encryptChannelPacket(channel_key, (const char*)&output_buffer[packet_len], header.from, header.packet_id, nonce);
        
        packet_len += protobuf_len;
    }

    return packet_len;
}

// Function to parse a hex string into binary data
int hexStringToBytes(const char* hex_str, uint8_t* output, int max_len) {
    int len = strlen(hex_str);
    if (len % 2 != 0 || len/2 > max_len) {
        return -1; // Invalid hex string or too long
    }

    for (int i = 0; i < len; i += 2) {
        char hex_byte[3] = {hex_str[i], hex_str[i+1], '\0'};
        output[i/2] = (uint8_t)strtol(hex_byte, NULL, 16);
    }

    return len / 2; // Return number of bytes
}

// Read varint from protobuf data (simplified for values < 128)
int readVarint(const uint8_t* data, int pos, int max_pos, uint32_t* value) {
    if (pos >= max_pos) return -1;

    *value = data[pos];
    if (*value < 128) {
        return pos + 1; // Single byte varint
    }

    // Multi-byte varint (simplified - only handles up to 4 bytes)
    *value = data[pos] & 0x7F;
    pos++;

    for (int i = 1; i < 4 && pos < max_pos; i++) {
        uint8_t byte = data[pos++];
        *value |= ((uint32_t)(byte & 0x7F)) << (7 * i);
        if (byte < 128) break;
    }

    return pos;
}

// Decode protobuf Data message
int decodeProtobufData(const uint8_t* data, int data_len, DecodedDataMessage* decoded) {
    // Initialize decoded structure
    memset(decoded, 0, sizeof(DecodedDataMessage));

    int pos = 0;

    while (pos < data_len) {
        if (pos >= data_len) break;

        uint8_t tag = data[pos++];
        uint8_t field_num = tag >> 3;
        uint8_t wire_type = tag & 0x7;

        printf("  Field %d (wire type %d): ", field_num, wire_type);

        switch (wire_type) {
            case 0: { // varint
                uint32_t value;
                pos = readVarint(data, pos, data_len, &value);
                if (pos < 0) return -1;

                printf("varint = %u", value);

                switch (field_num) {
                    case 1:
                        decoded->portnum = (uint8_t)value;
                        decoded->has_portnum = true;
                        printf(" (portnum: %s)",
                            value == 1 ? "TEXT_MESSAGE" :
                            value == 3 ? "POSITION" :
                            value == 4 ? "NODEINFO" : "UNKNOWN");
                        break;
                    case 3:
                        decoded->want_response = value != 0;
                        decoded->has_want_response = true;
                        printf(" (want_response: %s)", value ? "true" : "false");
                        break;
                    case 4:
                        decoded->dest = value;
                        decoded->has_dest = true;
                        printf(" (dest)");
                        break;
                    case 5:
                        decoded->source = value;
                        decoded->has_source = true;
                        printf(" (source)");
                        break;
                    case 6:
                        decoded->request_id = value;
                        decoded->has_request_id = true;
                        printf(" (request_id)");
                        break;
                    case 7:
                        decoded->reply_id = value;
                        decoded->has_reply_id = true;
                        printf(" (reply_id)");
                        break;
                    case 8:
                        decoded->emoji = value;
                        decoded->has_emoji = true;
                        printf(" (emoji)");
                        break;
                    case 9:
                        decoded->want_response = value != 0;
                        decoded->has_want_response = true;
                        printf(" (want_response: %s)", value ? "true" : "false");
                        break;
                    case 10:
                        decoded->bitfield = value;
                        decoded->has_bitfield = true;
                        printf(" (bitfield)");
                        break;
                }
                break;
            }

            case 2: { // length-delimited (bytes/string)
                if (pos >= data_len) return -1;
                uint8_t len = data[pos++];

                printf("bytes[%d] = ", len);

                if (field_num == 2) { // payload field
                    decoded->has_payload = true;
                    decoded->payload_len = len;

                    if (len < sizeof(decoded->payload_text) - 1) {
                        memcpy(decoded->payload_text, &data[pos], len);
                        decoded->payload_text[len] = '\0'; // null terminate

                        printf("\"");
                        for (int i = 0; i < len; i++) {
                            char c = decoded->payload_text[i];
                            printf("%c", (c >= 32 && c <= 126) ? c : '.');
                        }
                        printf("\" (payload)");
                    }
                } else {
                    // Other length-delimited fields
                    printf("\"");
                    for (int i = 0; i < len && pos + i < data_len; i++) {
                        char c = data[pos + i];
                        printf("%c", (c >= 32 && c <= 126) ? c : '.');
                    }
                    printf("\"");
                }

                pos += len;
                break;
            }

            default:
                printf("Unknown wire type %d", wire_type);
                return -1; // Unknown wire type
        }

        printf("\n");
    }

    return 0; // Success
}

// Function to decode protobuf from hex string
void decodeProtobufFromHex(const char* hex_str) {
    printf("\n=== Decoding Protobuf from Hex ===\n");

    uint8_t packet_data[512];
    int packet_len = hexStringToBytes(hex_str, packet_data, sizeof(packet_data));

    if (packet_len < 16) {
        printf("❌ Invalid packet: too short (need at least 16 bytes for header)\n");
        return;
    }

    printf("Packet: %s\n", hex_str);
    printf("Length: %d bytes\n", packet_len);

    // Parse header first
    printf("\nHeader:\n");
    uint32_t to = (packet_data[0] << 24) | (packet_data[1] << 16) |
                 (packet_data[2] << 8) | packet_data[3];
    uint32_t from = (packet_data[4] << 24) | (packet_data[5] << 16) |
                   (packet_data[6] << 8) | packet_data[7];
    uint32_t packet_id = (packet_data[8] << 24) | (packet_data[9] << 16) |
                        (packet_data[10] << 8) | packet_data[11];

    printf("  To: 0x%08X, From: 0x%08X, ID: 0x%08X\n", to, from, packet_id);

    // Decode protobuf payload
    if (packet_len > 16) {
        printf("\nProtobuf Data (%d bytes):\n", packet_len - 16);

        DecodedDataMessage decoded;
        int result = decodeProtobufData(&packet_data[16], packet_len - 16, &decoded);

        if (result == 0) {
            printf("\n✅ Successfully decoded protobuf!\n");
            printf("Summary:\n");
            if (decoded.has_portnum) {
                printf("  Port: %d (%s)\n", decoded.portnum,
                    decoded.portnum == 1 ? "TEXT_MESSAGE" :
                    decoded.portnum == 3 ? "POSITION" :
                    decoded.portnum == 4 ? "NODEINFO" : "UNKNOWN");
            }
            if (decoded.has_payload) {
                printf("  Message: \"%s\" (%zu bytes)\n", decoded.payload_text, decoded.payload_len);
            }
            if (decoded.has_want_response) {
                printf("  Want Response: %s\n", decoded.want_response ? "true" : "false");
            }
        } else {
            printf("❌ Failed to decode protobuf\n");
        }
    } else {
        printf("No payload data\n");
    }
}
