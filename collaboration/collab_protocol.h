// collab_protocol.h - Message serialization/deserialization
// Binary protocol with length-prefixed framing

#pragma once
#include "../base.h"
#include "collab_util.h"

// Protocol constants
#define COLLAB_MAGIC 0x4F524341  // "ORCA" in hex
#define COLLAB_HEADER_SIZE 8
#define COLLAB_MAX_PAYLOAD_SIZE 65535
#define COLLAB_MAX_MESSAGE_SIZE (COLLAB_HEADER_SIZE + COLLAB_MAX_PAYLOAD_SIZE)
#define COLLAB_PROTOCOL_VERSION 1
#define COLLAB_MAX_CLIENT_NAME 32

// Message types
typedef enum {
    MSG_HEARTBEAT = 0x00,        // Bidirectional: Keep connection alive
    MSG_CLIENT_JOIN = 0x01,      // Client -> Server: Request to join
    MSG_CLIENT_ACCEPT = 0x02,    // Server -> Client: Join accepted + client_id
    MSG_CLIENT_REJECT = 0x03,    // Server -> Client: Join rejected (full/error)
    MSG_CLIENT_LEAVE = 0x04,     // Client -> Server: Disconnect
    MSG_GRID_FULL_SYNC = 0x10,   // Server -> Client: Full grid state
    MSG_GRID_WRITE = 0x11,       // Bidirectional: Single cell write
    MSG_GRID_WRITE_BATCH = 0x12, // Bidirectional: Multiple cell writes
    MSG_CURSOR_UPDATE = 0x20,    // Bidirectional: Cursor position
    MSG_CLIENT_HASH = 0x30,      // Client -> Server: Grid hash for verification
    MSG_RESYNC_REQUEST = 0x31,   // Server -> Client: Hash mismatch, re-sync
    MSG_PLAY_STATE = 0x40,       // Server -> Clients: Play/pause state
    MSG_BPM_UPDATE = 0x41,       // Server -> Clients: Tempo change
    MSG_TICK_SYNC = 0x42,        // Server -> Clients: Tick counter sync
} Collab_msg_type;

// Message header (8 bytes)
// Wire format:
//   [0-3]: Magic (0x4F524341 "ORCA")
//   [4-5]: Payload length
//   [6]:   Message type
//   [7]:   Flags (reserved)
typedef struct {
    U32 magic;
    U16 length;     // Payload length (not including header)
    U8 msg_type;
    U8 flags;
} Collab_msg_header;

// MSG_HEARTBEAT payload (0 bytes - just header)

// MSG_CLIENT_JOIN payload
typedef struct {
    U8 protocol_version;
    char client_name[COLLAB_MAX_CLIENT_NAME];
} Msg_client_join;

// MSG_CLIENT_ACCEPT payload
typedef struct {
    U8 client_id;       // Assigned ID (1-10)
    U8 color_index;     // Assigned color (for cursor)
    U8 num_clients;     // Total clients (including this one)
    U8 reserved;
} Msg_client_accept;

// MSG_CLIENT_REJECT payload
typedef struct {
    U8 reason_code;     // Rejection reason
    char reason_text[64];
} Msg_client_reject;

// Rejection reasons
#define REJECT_SERVER_FULL 1
#define REJECT_PROTOCOL_MISMATCH 2
#define REJECT_INVALID_NAME 3

// MSG_CLIENT_LEAVE payload (empty, just header)

// MSG_GRID_WRITE payload
typedef struct {
    U8 client_id;
    U16 y;
    U16 x;
    Glyph glyph;
} Msg_grid_write;

// MSG_CURSOR_UPDATE payload
typedef struct {
    U8 client_id;
    U16 y;
    U16 x;
    U16 h;  // Selection height
    U16 w;  // Selection width
} Msg_cursor_update;

// MSG_CLIENT_HASH payload
typedef struct {
    U8 client_id;
    U64 hash;
} Msg_client_hash;

// MSG_PLAY_STATE payload (Server -> Clients)
// Wire format:
//   [0]:     is_playing (U8, 0 or 1)
//   [1-8]:   tick_num (U64)
//   [9-10]:  bpm (U16)
//   [11-18]: accum_secs as fixed-point (U64, microseconds * 1000)
typedef struct {
    U8 is_playing;
    U64 tick_num;
    U16 bpm;
    double accum_secs;  // Timing accumulator for sync
} Msg_play_state;

#define MSG_PLAY_STATE_SIZE 19  // 1 + 8 + 2 + 8

// MSG_BPM_UPDATE payload (Server -> Clients)
// Wire format:
//   [0-1]: bpm (U16)
typedef struct {
    U16 bpm;
} Msg_bpm_update;

#define MSG_BPM_UPDATE_SIZE 2

// MSG_TICK_SYNC payload (Server -> Clients)
// Wire format:
//   [0-7]:   tick_num (U64)
//   [8-15]:  accum_secs as fixed-point (U64, microseconds * 1000)
typedef struct {
    U64 tick_num;
    double accum_secs;
} Msg_tick_sync;

#define MSG_TICK_SYNC_SIZE 16  // 8 + 8

// Message buffer for assembling fragmented TCP packets
typedef struct {
    U8 buffer[COLLAB_MAX_MESSAGE_SIZE];
    Usz offset;         // Current read position in buffer
    Usz expected_len;   // Total message length (header + payload)
    bool has_header;    // Have we read the 8-byte header yet?
    bool needs_shift;   // Does buffer need to be shifted after message consumed?
    Usz shift_amount;   // How much to shift when consuming
} Collab_msg_buffer;

// Initialize message buffer
void collab_msg_buffer_init(Collab_msg_buffer *buf);

// Reset message buffer
void collab_msg_buffer_reset(Collab_msg_buffer *buf);

// Header serialization
// Serialize header into buffer (returns bytes written = COLLAB_HEADER_SIZE)
Usz collab_serialize_header(U8 *buf, Collab_msg_type type, U16 payload_len);

// Parse header from buffer
// Returns true if valid header, false if invalid magic or corrupt data
bool collab_parse_header(const U8 *buf, Collab_msg_header *header);

// Validate header magic
bool collab_validate_magic(U32 magic);

// Message serialization functions
// All return number of bytes written to payload buffer (not including header)

// Serialize MSG_HEARTBEAT (empty payload)
Usz collab_serialize_heartbeat(U8 *buf);

// Serialize MSG_CLIENT_JOIN
Usz collab_serialize_client_join(U8 *buf, const char *client_name);

// Serialize MSG_CLIENT_ACCEPT
Usz collab_serialize_client_accept(U8 *buf, U8 client_id, U8 color_index, U8 num_clients);

// Serialize MSG_CLIENT_REJECT
Usz collab_serialize_client_reject(U8 *buf, U8 reason_code, const char *reason_text);

// Serialize MSG_GRID_WRITE
Usz collab_serialize_grid_write(U8 *buf, U8 client_id, U16 y, U16 x, Glyph glyph);

// Serialize MSG_CURSOR_UPDATE
Usz collab_serialize_cursor_update(U8 *buf, U8 client_id, U16 y, U16 x, U16 h, U16 w);

// Serialize MSG_CLIENT_HASH
Usz collab_serialize_client_hash(U8 *buf, U8 client_id, U64 hash);

// Message deserialization functions
// All return true if successful, false if payload is malformed

// Deserialize MSG_CLIENT_JOIN
bool collab_deserialize_client_join(const U8 *buf, Usz len, Msg_client_join *msg);

// Deserialize MSG_CLIENT_ACCEPT
bool collab_deserialize_client_accept(const U8 *buf, Usz len, Msg_client_accept *msg);

// Deserialize MSG_CLIENT_REJECT
bool collab_deserialize_client_reject(const U8 *buf, Usz len, Msg_client_reject *msg);

// Deserialize MSG_GRID_WRITE
bool collab_deserialize_grid_write(const U8 *buf, Usz len, Msg_grid_write *msg);

// Deserialize MSG_CURSOR_UPDATE
bool collab_deserialize_cursor_update(const U8 *buf, Usz len, Msg_cursor_update *msg);

// Deserialize MSG_CLIENT_HASH
bool collab_deserialize_client_hash(const U8 *buf, Usz len, Msg_client_hash *msg);

// Serialize MSG_PLAY_STATE
Usz collab_serialize_play_state(U8 *buf, bool is_playing, U64 tick_num, U16 bpm, double accum_secs);

// Deserialize MSG_PLAY_STATE
bool collab_deserialize_play_state(const U8 *buf, Usz len, Msg_play_state *msg);

// Serialize MSG_BPM_UPDATE
Usz collab_serialize_bpm_update(U8 *buf, U16 bpm);

// Deserialize MSG_BPM_UPDATE
bool collab_deserialize_bpm_update(const U8 *buf, Usz len, Msg_bpm_update *msg);

// Serialize MSG_TICK_SYNC
Usz collab_serialize_tick_sync(U8 *buf, U64 tick_num, double accum_secs);

// Deserialize MSG_TICK_SYNC
bool collab_deserialize_tick_sync(const U8 *buf, Usz len, Msg_tick_sync *msg);

// Build complete message (header + payload) in buffer
// Returns total message size (header + payload)
Usz collab_build_message(U8 *buf, Collab_msg_type type, const U8 *payload, Usz payload_len);

// Get message type name for debugging
const char *collab_msg_type_name(Collab_msg_type type);
