// collab_protocol.c - Message serialization/deserialization
// Binary protocol with length-prefixed framing

#include "collab_protocol.h"
#include <string.h>
#include <stdio.h>

// Initialize message buffer
void collab_msg_buffer_init(Collab_msg_buffer *buf) {
    memset(buf, 0, sizeof(*buf));
    buf->needs_shift = false;
    buf->shift_amount = 0;
}

// Reset message buffer
void collab_msg_buffer_reset(Collab_msg_buffer *buf) {
    buf->offset = 0;
    buf->expected_len = 0;
    buf->has_header = false;
    buf->needs_shift = false;
    buf->shift_amount = 0;
}

// Validate magic number
bool collab_validate_magic(U32 magic) {
    return magic == COLLAB_MAGIC;
}

// Serialize header into buffer
Usz collab_serialize_header(U8 *buf, Collab_msg_type type, U16 payload_len) {
    collab_write_u32(buf, COLLAB_MAGIC);
    collab_write_u16(buf + 4, payload_len);
    buf[6] = (U8)type;
    buf[7] = 0;  // Reserved flags
    return COLLAB_HEADER_SIZE;
}

// Parse header from buffer
bool collab_parse_header(const U8 *buf, Collab_msg_header *header) {
    header->magic = collab_read_u32(buf);
    header->length = collab_read_u16(buf + 4);
    header->msg_type = buf[6];
    header->flags = buf[7];

    // Validate magic
    if (!collab_validate_magic(header->magic)) {
        return false;
    }

    return true;
}

// Serialize MSG_HEARTBEAT (empty payload)
Usz collab_serialize_heartbeat(U8 *buf) {
    (void)buf;  // No payload for heartbeat
    return 0;
}

// Serialize MSG_CLIENT_JOIN
Usz collab_serialize_client_join(U8 *buf, const char *client_name) {
    buf[0] = COLLAB_PROTOCOL_VERSION;
    memset(buf + 1, 0, COLLAB_MAX_CLIENT_NAME);
    strncpy((char *)(buf + 1), client_name, COLLAB_MAX_CLIENT_NAME - 1);
    return 1 + COLLAB_MAX_CLIENT_NAME;
}

// Serialize MSG_CLIENT_ACCEPT
Usz collab_serialize_client_accept(U8 *buf, U8 client_id, U8 color_index, U8 num_clients) {
    buf[0] = client_id;
    buf[1] = color_index;
    buf[2] = num_clients;
    buf[3] = 0;  // Reserved
    return 4;
}

// Serialize MSG_CLIENT_REJECT
Usz collab_serialize_client_reject(U8 *buf, U8 reason_code, const char *reason_text) {
    buf[0] = reason_code;
    memset(buf + 1, 0, 64);
    if (reason_text) {
        strncpy((char *)(buf + 1), reason_text, 63);
    }
    return 65;
}

// Serialize MSG_GRID_WRITE
Usz collab_serialize_grid_write(U8 *buf, U8 client_id, U16 y, U16 x, Glyph glyph) {
    buf[0] = client_id;
    collab_write_u16(buf + 1, y);
    collab_write_u16(buf + 3, x);
    buf[5] = (U8)glyph;
    return 6;
}

// Serialize MSG_CURSOR_UPDATE
Usz collab_serialize_cursor_update(U8 *buf, U8 client_id, U16 y, U16 x, U16 h, U16 w) {
    buf[0] = client_id;
    collab_write_u16(buf + 1, y);
    collab_write_u16(buf + 3, x);
    collab_write_u16(buf + 5, h);
    collab_write_u16(buf + 7, w);
    return 9;
}

// Serialize MSG_CLIENT_HASH
Usz collab_serialize_client_hash(U8 *buf, U8 client_id, U64 hash) {
    buf[0] = client_id;
    collab_write_u64(buf + 1, hash);
    return 9;
}

// Deserialize MSG_CLIENT_JOIN
bool collab_deserialize_client_join(const U8 *buf, Usz len, Msg_client_join *msg) {
    if (len < 1 + COLLAB_MAX_CLIENT_NAME) {
        return false;
    }
    msg->protocol_version = buf[0];
    memcpy(msg->client_name, buf + 1, COLLAB_MAX_CLIENT_NAME);
    msg->client_name[COLLAB_MAX_CLIENT_NAME - 1] = '\0';  // Ensure null termination
    return true;
}

// Deserialize MSG_CLIENT_ACCEPT
bool collab_deserialize_client_accept(const U8 *buf, Usz len, Msg_client_accept *msg) {
    if (len < 4) {
        return false;
    }
    msg->client_id = buf[0];
    msg->color_index = buf[1];
    msg->num_clients = buf[2];
    msg->reserved = buf[3];
    return true;
}

// Deserialize MSG_CLIENT_REJECT
bool collab_deserialize_client_reject(const U8 *buf, Usz len, Msg_client_reject *msg) {
    if (len < 1) {
        return false;
    }
    msg->reason_code = buf[0];
    if (len > 1) {
        Usz text_len = len - 1;
        if (text_len > 63) text_len = 63;
        memcpy(msg->reason_text, buf + 1, text_len);
        msg->reason_text[text_len] = '\0';
    } else {
        msg->reason_text[0] = '\0';
    }
    return true;
}

// Deserialize MSG_GRID_WRITE
bool collab_deserialize_grid_write(const U8 *buf, Usz len, Msg_grid_write *msg) {
    if (len < 6) {
        return false;
    }
    msg->client_id = buf[0];
    msg->y = collab_read_u16(buf + 1);
    msg->x = collab_read_u16(buf + 3);
    msg->glyph = (Glyph)buf[5];
    return true;
}

// Deserialize MSG_CURSOR_UPDATE
bool collab_deserialize_cursor_update(const U8 *buf, Usz len, Msg_cursor_update *msg) {
    if (len < 9) {
        return false;
    }
    msg->client_id = buf[0];
    msg->y = collab_read_u16(buf + 1);
    msg->x = collab_read_u16(buf + 3);
    msg->h = collab_read_u16(buf + 5);
    msg->w = collab_read_u16(buf + 7);
    return true;
}

// Deserialize MSG_CLIENT_HASH
bool collab_deserialize_client_hash(const U8 *buf, Usz len, Msg_client_hash *msg) {
    if (len < 9) {
        return false;
    }
    msg->client_id = buf[0];
    msg->hash = collab_read_u64(buf + 1);
    return true;
}

// Helper: convert double to fixed-point U64 (microseconds * 1000)
static U64 double_to_fixed(double val) {
    // Store as nanoseconds to preserve precision
    return (U64)(val * 1000000000.0);
}

// Helper: convert fixed-point U64 back to double
static double fixed_to_double(U64 val) {
    return (double)val / 1000000000.0;
}

// Serialize MSG_PLAY_STATE
Usz collab_serialize_play_state(U8 *buf, bool is_playing, U64 tick_num, U16 bpm, double accum_secs) {
    buf[0] = is_playing ? 1 : 0;
    collab_write_u64(buf + 1, tick_num);
    collab_write_u16(buf + 9, bpm);
    collab_write_u64(buf + 11, double_to_fixed(accum_secs));
    return MSG_PLAY_STATE_SIZE;
}

// Deserialize MSG_PLAY_STATE
bool collab_deserialize_play_state(const U8 *buf, Usz len, Msg_play_state *msg) {
    if (len < MSG_PLAY_STATE_SIZE) {
        return false;
    }
    msg->is_playing = buf[0];
    msg->tick_num = collab_read_u64(buf + 1);
    msg->bpm = collab_read_u16(buf + 9);
    msg->accum_secs = fixed_to_double(collab_read_u64(buf + 11));
    return true;
}

// Serialize MSG_BPM_UPDATE
Usz collab_serialize_bpm_update(U8 *buf, U16 bpm) {
    collab_write_u16(buf, bpm);
    return MSG_BPM_UPDATE_SIZE;
}

// Deserialize MSG_BPM_UPDATE
bool collab_deserialize_bpm_update(const U8 *buf, Usz len, Msg_bpm_update *msg) {
    if (len < MSG_BPM_UPDATE_SIZE) {
        return false;
    }
    msg->bpm = collab_read_u16(buf);
    return true;
}

// Serialize MSG_TICK_SYNC
Usz collab_serialize_tick_sync(U8 *buf, U64 tick_num, double accum_secs) {
    collab_write_u64(buf, tick_num);
    collab_write_u64(buf + 8, double_to_fixed(accum_secs));
    return MSG_TICK_SYNC_SIZE;
}

// Deserialize MSG_TICK_SYNC
bool collab_deserialize_tick_sync(const U8 *buf, Usz len, Msg_tick_sync *msg) {
    if (len < MSG_TICK_SYNC_SIZE) {
        return false;
    }
    msg->tick_num = collab_read_u64(buf);
    msg->accum_secs = fixed_to_double(collab_read_u64(buf + 8));
    return true;
}

// Build complete message (header + payload) in buffer
Usz collab_build_message(U8 *buf, Collab_msg_type type, const U8 *payload, Usz payload_len) {
    collab_serialize_header(buf, type, (U16)payload_len);
    if (payload && payload_len > 0) {
        memcpy(buf + COLLAB_HEADER_SIZE, payload, payload_len);
    }
    return COLLAB_HEADER_SIZE + payload_len;
}

// Get message type name for debugging
const char *collab_msg_type_name(Collab_msg_type type) {
    switch (type) {
        case MSG_HEARTBEAT:       return "HEARTBEAT";
        case MSG_CLIENT_JOIN:     return "CLIENT_JOIN";
        case MSG_CLIENT_ACCEPT:   return "CLIENT_ACCEPT";
        case MSG_CLIENT_REJECT:   return "CLIENT_REJECT";
        case MSG_CLIENT_LEAVE:    return "CLIENT_LEAVE";
        case MSG_GRID_FULL_SYNC:  return "GRID_FULL_SYNC";
        case MSG_GRID_WRITE:      return "GRID_WRITE";
        case MSG_GRID_WRITE_BATCH: return "GRID_WRITE_BATCH";
        case MSG_CURSOR_UPDATE:   return "CURSOR_UPDATE";
        case MSG_CLIENT_HASH:     return "CLIENT_HASH";
        case MSG_RESYNC_REQUEST:  return "RESYNC_REQUEST";
        case MSG_PLAY_STATE:      return "PLAY_STATE";
        case MSG_BPM_UPDATE:      return "BPM_UPDATE";
        case MSG_TICK_SYNC:       return "TICK_SYNC";
        default:                  return "UNKNOWN";
    }
}
