// collab_client.c - Client-side collaboration logic
// Server connection, grid synchronization, write operations

#include "collab_client.h"
#include "../gbuffer.h"
#include <string.h>

// Initialize client
void collab_client_init(Collab_client *client) {
    memset(client, 0, sizeof(*client));

    collab_connection_init(&client->conn);
    client->state = COLLAB_CLIENT_DISCONNECTED;

    field_init(&client->grid);
    collab_hash_cache_init(&client->hash_cache);
    collab_poll_init(&client->poll_set);

    client->my_client_id = 0;
    client->my_color_index = 0;
    client->my_name[0] = '\0';
    client->server_host[0] = '\0';
    client->server_port = 0;
    client->needs_resync = false;

    client->tick_num = 0;
    client->bpm = 120;
    client->is_playing = false;
    client->accum_secs = 0.0;
    client->playback_changed = false;

    client->last_hash_time_ms = 0;
    client->last_cursor_time_ms = 0;

    client->cursor_callback = NULL;
    client->cursor_callback_userdata = NULL;
    client->has_pending_cursor = false;

    collab_error_clear(&client->last_error);
}

// Deinitialize client
void collab_client_deinit(Collab_client *client) {
    collab_client_disconnect(client);
    collab_connection_deinit(&client->conn);
    collab_poll_deinit(&client->poll_set);
    field_deinit(&client->grid);
}

// Connect to server
bool collab_client_connect(Collab_client *client, const char *host, U16 port, const char *name) {
    if (client->state != COLLAB_CLIENT_DISCONNECTED) {
        collab_error_set(&client->last_error, -1, "Already connected or connecting");
        return false;
    }

    // Store connection info
    strncpy(client->server_host, host, sizeof(client->server_host) - 1);
    client->server_host[sizeof(client->server_host) - 1] = '\0';
    client->server_port = port;
    strncpy(client->my_name, name, COLLAB_MAX_CLIENT_NAME - 1);
    client->my_name[COLLAB_MAX_CLIENT_NAME - 1] = '\0';

    // Start connection
    if (!collab_connect_start(&client->conn, host, port, &client->last_error)) {
        client->state = COLLAB_CLIENT_ERROR;
        return false;
    }

    if (client->conn.state == COLLAB_CONN_CONNECTED) {
        client->state = COLLAB_CLIENT_CONNECTED;
    } else {
        client->state = COLLAB_CLIENT_CONNECTING;
    }

    COLLAB_LOG_INFO("Connecting to %s:%u as '%s'", host, port, name);
    return true;
}

// Disconnect from server
void collab_client_disconnect(Collab_client *client) {
    if (client->state == COLLAB_CLIENT_DISCONNECTED) {
        return;
    }

    // Send leave message if connected
    if (client->state >= COLLAB_CLIENT_JOINING && client->conn.fd >= 0) {
        collab_send_message(&client->conn, MSG_CLIENT_LEAVE, NULL, 0, &client->last_error);
    }

    collab_connection_close(&client->conn);

    client->state = COLLAB_CLIENT_DISCONNECTED;
    client->my_client_id = 0;
    client->my_color_index = 0;
    client->needs_resync = false;

    COLLAB_LOG_INFO("Disconnected from server");
}

// Send join request
static bool collab_client_send_join(Collab_client *client) {
    U8 payload[1 + COLLAB_MAX_CLIENT_NAME];
    Usz len = collab_serialize_client_join(payload, client->my_name);

    if (!collab_send_message(&client->conn, MSG_CLIENT_JOIN, payload, len, &client->last_error)) {
        return false;
    }

    client->state = COLLAB_CLIENT_JOINING;
    COLLAB_LOG_DEBUG("Sent join request as '%s'", client->my_name);
    return true;
}

// Handle accept message
void collab_client_handle_accept(Collab_client *client, const U8 *payload, Usz len) {
    Msg_client_accept accept_msg;
    if (!collab_deserialize_client_accept(payload, len, &accept_msg)) {
        COLLAB_LOG_ERROR("Invalid accept message");
        client->state = COLLAB_CLIENT_ERROR;
        collab_error_set(&client->last_error, -1, "Invalid accept message");
        return;
    }

    client->my_client_id = accept_msg.client_id;
    client->my_color_index = accept_msg.color_index;
    client->state = COLLAB_CLIENT_SYNCING;

    COLLAB_LOG_INFO("Joined server: id=%u, color=%u, clients=%u",
                    accept_msg.client_id, accept_msg.color_index, accept_msg.num_clients);
}

// Handle reject message
void collab_client_handle_reject(Collab_client *client, const U8 *payload, Usz len) {
    Msg_client_reject reject_msg;
    if (!collab_deserialize_client_reject(payload, len, &reject_msg)) {
        collab_error_set(&client->last_error, -1, "Server rejected connection");
    } else {
        collab_error_set(&client->last_error, reject_msg.reason_code,
                         "Server rejected: %s", reject_msg.reason_text);
    }

    client->state = COLLAB_CLIENT_ERROR;
    collab_connection_close(&client->conn);

    COLLAB_LOG_ERROR("Connection rejected: %s", client->last_error.message);
}

// Handle full grid sync
void collab_client_handle_full_sync(Collab_client *client, const U8 *payload, Usz len) {
    Msg_grid_full_sync_header header;
    if (!collab_deserialize_grid_full_sync_header(payload, len, &header)) {
        COLLAB_LOG_ERROR("Invalid full sync header");
        return;
    }

    const U8 *grid_data = payload + MSG_GRID_FULL_SYNC_HEADER_SIZE;
    Usz grid_data_len = len - MSG_GRID_FULL_SYNC_HEADER_SIZE;

    if (!collab_apply_full_sync(&client->grid, &header, grid_data, grid_data_len)) {
        COLLAB_LOG_ERROR("Failed to apply full sync");
        return;
    }

    // Update state
    client->tick_num = header.tick_num;
    client->is_playing = header.is_playing != 0;
    client->bpm = header.bpm;
    client->needs_resync = false;
    client->playback_changed = true;  // join/resync also carries play state

    collab_hash_cache_invalidate(&client->hash_cache);
    client->grid_dirty = true;

    if (client->state == COLLAB_CLIENT_SYNCING) {
        client->state = COLLAB_CLIENT_ACTIVE;
        client->last_hash_time_ms = collab_time_ms();
        COLLAB_LOG_INFO("Full sync complete: %ux%u grid, now active",
                        header.width, header.height);
    } else {
        COLLAB_LOG_INFO("Resync complete: %ux%u grid", header.width, header.height);
    }
}

// Handle single grid write
void collab_client_handle_write(Collab_client *client, const U8 *payload, Usz len) {
    Msg_grid_write write_msg;
    if (!collab_deserialize_grid_write(payload, len, &write_msg)) {
        COLLAB_LOG_WARN("Invalid grid write message");
        return;
    }

    if (collab_apply_write(&client->grid, write_msg.y, write_msg.x, write_msg.glyph)) {
        collab_hash_cache_invalidate(&client->hash_cache);
        client->grid_dirty = true;
        COLLAB_LOG_DEBUG("Applied write from client %u: (%u, %u) = '%c'",
                         write_msg.client_id, write_msg.y, write_msg.x, write_msg.glyph);
    }
}

// Handle batch grid write
void collab_client_handle_write_batch(Collab_client *client, const U8 *payload, Usz len) {
    U8 client_id;
    U16 num_writes;
    if (!collab_deserialize_grid_write_batch_header(payload, len, &client_id, &num_writes)) {
        COLLAB_LOG_WARN("Invalid write batch message");
        return;
    }

    const U8 *ptr = payload + MSG_GRID_WRITE_BATCH_HEADER_SIZE;
    Usz applied = 0;
    for (U16 i = 0; i < num_writes; i++) {
        Collab_write_tuple write;
        collab_parse_write_tuple(ptr, &write);
        if (collab_apply_write(&client->grid, write.y, write.x, write.glyph)) {
            applied++;
        }
        ptr += MSG_GRID_WRITE_TUPLE_SIZE;
    }

    if (applied > 0) {
        collab_hash_cache_invalidate(&client->hash_cache);
        client->grid_dirty = true;
    }

    COLLAB_LOG_DEBUG("Applied write batch from client %u: %u/%u writes",
                     client_id, (unsigned)applied, num_writes);
}

// Handle resync request
void collab_client_handle_resync_request(Collab_client *client) {
    COLLAB_LOG_INFO("Resync requested by server");
    client->needs_resync = true;
    // Server will send full sync after this
}

// Handle cursor update from another client
void collab_client_handle_cursor_update(Collab_client *client, const U8 *payload, Usz len) {
    Msg_cursor_update cursor_msg;
    if (!collab_deserialize_cursor_update(payload, len, &cursor_msg)) {
        COLLAB_LOG_WARN("Invalid cursor update message");
        return;
    }

    // Don't process our own cursor updates
    if (cursor_msg.client_id == client->my_client_id) {
        return;
    }

    COLLAB_LOG_DEBUG("Cursor update from client %u: (%u, %u) %ux%u",
                     cursor_msg.client_id, cursor_msg.y, cursor_msg.x,
                     cursor_msg.w, cursor_msg.h);

    // Store pending cursor for polling
    client->pending_cursor = cursor_msg;
    client->has_pending_cursor = true;

    // Call callback if registered
    if (client->cursor_callback) {
        client->cursor_callback(client->cursor_callback_userdata, &cursor_msg);
    }
}

// Handle play state from server
void collab_client_handle_play_state(Collab_client *client, const U8 *payload, Usz len) {
    Msg_play_state msg;
    if (!collab_deserialize_play_state(payload, len, &msg)) {
        COLLAB_LOG_WARN("Invalid play state message");
        return;
    }

    bool was_playing = client->is_playing;

    client->is_playing = msg.is_playing != 0;
    client->tick_num = msg.tick_num;
    client->bpm = msg.bpm;
    client->accum_secs = msg.accum_secs;
    client->playback_changed = true;

    COLLAB_LOG_INFO("Play state update: playing=%d, tick=%llu, bpm=%u",
                    client->is_playing, (unsigned long long)client->tick_num, client->bpm);

    if (was_playing != client->is_playing) {
        COLLAB_LOG_INFO("Playback %s by server",
                        client->is_playing ? "started" : "stopped");
    }
}

// Handle BPM update from server
void collab_client_handle_bpm_update(Collab_client *client, const U8 *payload, Usz len) {
    Msg_bpm_update msg;
    if (!collab_deserialize_bpm_update(payload, len, &msg)) {
        COLLAB_LOG_WARN("Invalid BPM update message");
        return;
    }

    if (client->bpm != msg.bpm) {
        client->bpm = msg.bpm;
        client->playback_changed = true;

        COLLAB_LOG_INFO("BPM updated to %u by server", msg.bpm);
    }
}

// Handle tick sync from server
void collab_client_handle_tick_sync(Collab_client *client, const U8 *payload, Usz len) {
    Msg_tick_sync msg;
    if (!collab_deserialize_tick_sync(payload, len, &msg)) {
        COLLAB_LOG_WARN("Invalid tick sync message");
        return;
    }

    // Update tick and timing accumulator
    client->tick_num = msg.tick_num;
    client->accum_secs = msg.accum_secs;
    // Note: we set playback_changed so TUI can update, but only if there's
    // a significant drift (more than 1 tick difference)
    // For now, always mark changed to ensure sync
    client->playback_changed = true;

    COLLAB_LOG_DEBUG("Tick sync: tick=%llu, accum=%f",
                     (unsigned long long)msg.tick_num, msg.accum_secs);
}

// Process a received message
void collab_client_handle_message(Collab_client *client, Collab_msg_type type,
                                   const U8 *payload, Usz len) {
    switch (type) {
        case MSG_HEARTBEAT:
            // Server heartbeat - just update activity time (done in recv)
            break;

        case MSG_CLIENT_ACCEPT:
            if (client->state == COLLAB_CLIENT_JOINING) {
                collab_client_handle_accept(client, payload, len);
            }
            break;

        case MSG_CLIENT_REJECT:
            collab_client_handle_reject(client, payload, len);
            break;

        case MSG_GRID_FULL_SYNC:
            collab_client_handle_full_sync(client, payload, len);
            break;

        case MSG_GRID_WRITE:
            if (client->state == COLLAB_CLIENT_ACTIVE) {
                collab_client_handle_write(client, payload, len);
            }
            break;

        case MSG_GRID_WRITE_BATCH:
            if (client->state == COLLAB_CLIENT_ACTIVE) {
                collab_client_handle_write_batch(client, payload, len);
            }
            break;

        case MSG_RESYNC_REQUEST:
            if (client->state == COLLAB_CLIENT_ACTIVE) {
                collab_client_handle_resync_request(client);
            }
            break;

        case MSG_CURSOR_UPDATE:
            if (client->state == COLLAB_CLIENT_ACTIVE) {
                collab_client_handle_cursor_update(client, payload, len);
            }
            break;

        case MSG_PLAY_STATE:
            collab_client_handle_play_state(client, payload, len);
            break;

        case MSG_BPM_UPDATE:
            collab_client_handle_bpm_update(client, payload, len);
            break;

        case MSG_TICK_SYNC:
            collab_client_handle_tick_sync(client, payload, len);
            break;

        default:
            COLLAB_LOG_WARN("Unknown message type: %u", type);
            break;
    }
}

// Update client
bool collab_client_update(Collab_client *client) {
    if (client->state == COLLAB_CLIENT_DISCONNECTED ||
        client->state == COLLAB_CLIENT_ERROR) {
        return false;
    }

    // Handle connecting state
    if (client->state == COLLAB_CLIENT_CONNECTING) {
        Collab_conn_state conn_state = collab_connect_check(&client->conn, &client->last_error);
        switch (conn_state) {
            case COLLAB_CONN_CONNECTED:
                client->state = COLLAB_CLIENT_CONNECTED;
                COLLAB_LOG_INFO("TCP connected to %s:%u", client->server_host, client->server_port);
                break;
            case COLLAB_CONN_DISCONNECTED:
                client->state = COLLAB_CLIENT_ERROR;
                COLLAB_LOG_ERROR("Connection failed: %s", client->last_error.message);
                return false;
            case COLLAB_CONN_CONNECTING:
                // Still connecting
                return false;
        }
    }

    // Send join if just connected
    if (client->state == COLLAB_CLIENT_CONNECTED) {
        if (!collab_client_send_join(client)) {
            client->state = COLLAB_CLIENT_ERROR;
            COLLAB_LOG_ERROR("Failed to send join: %s", client->last_error.message);
            return false;
        }
    }

    // Poll for data
    collab_poll_clear(&client->poll_set);
    collab_poll_add_read(&client->poll_set, client->conn.fd);

    int ready = collab_poll(&client->poll_set, 0);
    if (ready > 0 && collab_poll_can_read(&client->poll_set, client->conn.fd)) {
        Collab_sock_result result = collab_recv(&client->conn, &client->last_error);
        if (result == COLLAB_SOCK_CLOSED) {
            COLLAB_LOG_ERROR("Server disconnected");
            client->state = COLLAB_CLIENT_ERROR;
            collab_error_set(&client->last_error, -1, "Server disconnected");
            collab_connection_close(&client->conn);
            return false;
        }
        if (result == COLLAB_SOCK_ERROR) {
            COLLAB_LOG_ERROR("Receive error: %s", client->last_error.message);
            client->state = COLLAB_CLIENT_ERROR;
            collab_connection_close(&client->conn);
            return false;
        }

        // Process messages
        while (collab_has_message(&client->conn)) {
            Collab_msg_header header;
            const U8 *payload;
            Usz payload_len;

            if (!collab_get_message(&client->conn, &header, &payload, &payload_len)) {
                COLLAB_LOG_ERROR("Failed to get message");
                break;
            }

            collab_client_handle_message(client, (Collab_msg_type)header.msg_type,
                                          payload, payload_len);
        }
    }

    // Periodic hash verification (if active)
    if (client->state == COLLAB_CLIENT_ACTIVE) {
        U64 now = collab_time_ms();
        if (now - client->last_hash_time_ms >= COLLAB_HASH_INTERVAL_MS) {
            collab_client_send_hash(client);
            client->last_hash_time_ms = now;
        }
    }

    return true;
}

// Write a single cell
bool collab_client_write(Collab_client *client, U16 y, U16 x, Glyph glyph) {
    if (client->state != COLLAB_CLIENT_ACTIVE) {
        return false;
    }

    // Apply locally (optimistic update)
    if (!collab_apply_write(&client->grid, y, x, glyph)) {
        return false;
    }

    collab_hash_cache_invalidate(&client->hash_cache);

    // Send to server
    U8 payload[6];
    collab_serialize_grid_write(payload, client->my_client_id, y, x, glyph);
    return collab_send_message(&client->conn, MSG_GRID_WRITE, payload, 6, &client->last_error);
}

// Write a batch of cells
bool collab_client_write_batch(Collab_client *client, const Collab_write_tuple *writes, U16 num_writes) {
    if (client->state != COLLAB_CLIENT_ACTIVE || num_writes == 0) {
        return false;
    }

    // Apply locally (optimistic update)
    collab_apply_write_batch(&client->grid, writes, num_writes);
    collab_hash_cache_invalidate(&client->hash_cache);

    // Send to server
    Usz payload_size = MSG_GRID_WRITE_BATCH_HEADER_SIZE + (Usz)num_writes * MSG_GRID_WRITE_TUPLE_SIZE;
    U8 *payload = malloc(payload_size);
    if (!payload) {
        return false;
    }

    Usz len = collab_serialize_grid_write_batch(payload, client->my_client_id, writes, num_writes);
    bool success = collab_send_message(&client->conn, MSG_GRID_WRITE_BATCH, payload, len, &client->last_error);
    free(payload);

    return success;
}

// Send cursor update
bool collab_client_send_cursor(Collab_client *client, U16 y, U16 x, U16 h, U16 w) {
    if (client->state != COLLAB_CLIENT_ACTIVE) {
        return false;
    }

    U8 payload[9];
    collab_serialize_cursor_update(payload, client->my_client_id, y, x, h, w);
    return collab_send_message(&client->conn, MSG_CURSOR_UPDATE, payload, 9, &client->last_error);
}

// Send hash verification
bool collab_client_send_hash(Collab_client *client) {
    if (client->state != COLLAB_CLIENT_ACTIVE) {
        return false;
    }

    U64 hash = collab_compute_grid_hash(&client->grid, &client->hash_cache);

    U8 payload[9];
    collab_serialize_client_hash(payload, client->my_client_id, hash);

    COLLAB_LOG_DEBUG("Sending hash: %llx", (unsigned long long)hash);
    return collab_send_message(&client->conn, MSG_CLIENT_HASH, payload, 9, &client->last_error);
}

// Get client state
Collab_client_state collab_client_get_state(const Collab_client *client) {
    return client->state;
}

// Get state string
const char *collab_client_state_string(Collab_client_state state) {
    switch (state) {
        case COLLAB_CLIENT_DISCONNECTED: return "Disconnected";
        case COLLAB_CLIENT_CONNECTING:   return "Connecting";
        case COLLAB_CLIENT_CONNECTED:    return "Connected";
        case COLLAB_CLIENT_JOINING:      return "Joining";
        case COLLAB_CLIENT_SYNCING:      return "Syncing";
        case COLLAB_CLIENT_ACTIVE:       return "Active";
        case COLLAB_CLIENT_ERROR:        return "Error";
        default:                         return "Unknown";
    }
}

// Get grid
Field *collab_client_get_grid(Collab_client *client) {
    return &client->grid;
}

const Field *collab_client_get_grid_const(const Collab_client *client) {
    return &client->grid;
}

// Get client ID
U8 collab_client_get_id(const Collab_client *client) {
    return client->my_client_id;
}

// Get client color
U8 collab_client_get_color(const Collab_client *client) {
    return client->my_color_index;
}

// Get client name
const char *collab_client_get_name(const Collab_client *client) {
    return client->my_name;
}

// Check if active
bool collab_client_is_active(const Collab_client *client) {
    return client->state == COLLAB_CLIENT_ACTIVE;
}

// Get error message
const char *collab_client_get_error(const Collab_client *client) {
    return client->last_error.message;
}

// True once after the server changed the grid (clears the flag)
bool collab_client_needs_redraw(Collab_client *client) {
    bool dirty = client->grid_dirty;
    client->grid_dirty = false;
    return dirty;
}

// Check if playback state changed (and clear the flag)
bool collab_client_playback_changed(Collab_client *client) {
    if (!client->playback_changed) {
        return false;
    }
    client->playback_changed = false;
    return true;
}

// Get current playback state from server
bool collab_client_get_playback_state(const Collab_client *client,
                                       bool *is_playing, U64 *tick_num,
                                       U16 *bpm, double *accum_secs) {
    if (client->state != COLLAB_CLIENT_ACTIVE) {
        return false;
    }

    if (is_playing) *is_playing = client->is_playing;
    if (tick_num) *tick_num = client->tick_num;
    if (bpm) *bpm = client->bpm;
    if (accum_secs) *accum_secs = client->accum_secs;

    return true;
}

// Set cursor update callback
void collab_client_set_cursor_callback(Collab_client *client,
                                        Collab_cursor_callback callback,
                                        void *userdata) {
    client->cursor_callback = callback;
    client->cursor_callback_userdata = userdata;
}

// Get last received cursor update (for polling)
bool collab_client_get_cursor_update(Collab_client *client, Msg_cursor_update *out_cursor) {
    if (!client->has_pending_cursor) {
        return false;
    }

    if (out_cursor) {
        *out_cursor = client->pending_cursor;
    }
    client->has_pending_cursor = false;
    return true;
}
