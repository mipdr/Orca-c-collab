// collab_server.c - Server-side collaboration logic
// Client management, message broadcasting, grid source-of-truth

#include "collab_server.h"
#include <string.h>

// Initialize server
void collab_server_init(Collab_server *srv) {
    memset(srv, 0, sizeof(*srv));

    collab_listener_init(&srv->listener);

    for (int i = 0; i < COLLAB_MAX_CLIENTS; i++) {
        collab_connection_init(&srv->clients[i].conn);
        srv->clients[i].client_id = 0;
        srv->clients[i].color_index = 0;
        srv->clients[i].name[0] = '\0';
        srv->clients[i].is_joined = false;
        srv->color_available[i] = true;
    }

    field_init(&srv->grid);
    collab_hash_cache_init(&srv->hash_cache);
    collab_poll_init(&srv->poll_set);

    srv->num_connected = 0;
    srv->next_client_id = 1;
    srv->tick_num = 0;
    srv->bpm = 120;
    srv->is_playing = false;
    srv->accum_secs = 0.0;
    srv->last_tick_sync_ms = 0;
    srv->is_running = false;

    srv->cursor_callback = NULL;
    srv->cursor_callback_userdata = NULL;
    srv->has_pending_cursor = false;

    srv->client_event_callback = NULL;
    srv->client_event_userdata = NULL;

    collab_error_clear(&srv->last_error);
}

// Deinitialize server
void collab_server_deinit(Collab_server *srv) {
    if (srv->is_running) {
        collab_server_stop(srv);
    }

    for (int i = 0; i < COLLAB_MAX_CLIENTS; i++) {
        collab_connection_deinit(&srv->clients[i].conn);
    }

    collab_listener_deinit(&srv->listener);
    collab_poll_deinit(&srv->poll_set);
    field_deinit(&srv->grid);
}

// Start server
bool collab_server_start(Collab_server *srv, U16 port, const Field *initial_grid) {
    if (srv->is_running) {
        collab_error_set(&srv->last_error, -1, "Server already running");
        return false;
    }

    // Start listener
    if (!collab_listener_start(&srv->listener, port, &srv->last_error)) {
        return false;
    }

    // Initialize grid with initial state
    if (initial_grid && initial_grid->buffer) {
        collab_copy_grid(initial_grid, &srv->grid);
    } else {
        // Create default 64x32 grid
        field_init_fill(&srv->grid, 32, 64, '.');
    }

    collab_hash_cache_invalidate(&srv->hash_cache);

    srv->is_running = true;
    COLLAB_LOG_INFO("Server started on port %u with %ux%u grid",
                    port, srv->grid.width, srv->grid.height);

    return true;
}

// Stop server
void collab_server_stop(Collab_server *srv) {
    if (!srv->is_running) {
        return;
    }

    // Disconnect all clients
    for (int i = 0; i < COLLAB_MAX_CLIENTS; i++) {
        if (srv->clients[i].conn.fd >= 0) {
            collab_server_remove_client(srv, i);
        }
    }

    collab_listener_stop(&srv->listener);
    srv->is_running = false;

    COLLAB_LOG_INFO("Server stopped");
}

// Allocate client ID and color
bool collab_server_allocate_client_id(Collab_server *srv, U8 *out_id, U8 *out_color) {
    // Find first available color
    for (int i = 0; i < COLLAB_MAX_CLIENTS; i++) {
        if (srv->color_available[i]) {
            srv->color_available[i] = false;
            *out_color = (U8)i;
            *out_id = srv->next_client_id++;
            return true;
        }
    }
    return false;
}

// Release client ID and color
void collab_server_release_client_id(Collab_server *srv, U8 color_index) {
    if (color_index < COLLAB_MAX_CLIENTS) {
        srv->color_available[color_index] = true;
    }
}

// Accept a new client connection
bool collab_server_accept_client(Collab_server *srv) {
    // Find free slot
    int slot = -1;
    for (int i = 0; i < COLLAB_MAX_CLIENTS; i++) {
        if (srv->clients[i].conn.fd < 0) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        // Server full - accept and immediately reject
        Collab_connection temp;
        collab_connection_init(&temp);

        if (collab_listener_accept(&srv->listener, &temp, &srv->last_error)) {
            U8 payload[65];
            Usz payload_len = collab_serialize_client_reject(payload, REJECT_SERVER_FULL, "Server full");
            collab_send_message(&temp, MSG_CLIENT_REJECT, payload, payload_len, &srv->last_error);
            collab_connection_close(&temp);
            COLLAB_LOG_INFO("Rejected client: server full");
        }
        return false;
    }

    // Accept connection
    Collab_client_slot *client = &srv->clients[slot];
    if (!collab_listener_accept(&srv->listener, &client->conn, &srv->last_error)) {
        return false;
    }

    // Client is connected but not yet joined (waiting for MSG_CLIENT_JOIN)
    client->is_joined = false;
    client->client_id = 0;
    client->name[0] = '\0';
    srv->num_connected++;

    COLLAB_LOG_INFO("Client connected in slot %d (waiting for join)", slot);
    return true;
}

// Handle client join request
void collab_server_handle_join(Collab_server *srv, int slot,
                                const U8 *payload, Usz len) {
    Collab_client_slot *client = &srv->clients[slot];

    // Parse join message
    Msg_client_join join_msg;
    if (!collab_deserialize_client_join(payload, len, &join_msg)) {
        COLLAB_LOG_WARN("Invalid join message from slot %d", slot);
        collab_server_remove_client(srv, slot);
        return;
    }

    // Check protocol version
    if (join_msg.protocol_version != COLLAB_PROTOCOL_VERSION) {
        U8 reject_payload[65];
        Usz reject_len = collab_serialize_client_reject(reject_payload,
            REJECT_PROTOCOL_MISMATCH, "Protocol version mismatch");
        collab_send_message(&client->conn, MSG_CLIENT_REJECT, reject_payload, reject_len, &srv->last_error);
        collab_server_remove_client(srv, slot);
        return;
    }

    // Allocate ID and color
    U8 client_id, color_index;
    if (!collab_server_allocate_client_id(srv, &client_id, &color_index)) {
        U8 reject_payload[65];
        Usz reject_len = collab_serialize_client_reject(reject_payload,
            REJECT_SERVER_FULL, "Server full (no IDs available)");
        collab_send_message(&client->conn, MSG_CLIENT_REJECT, reject_payload, reject_len, &srv->last_error);
        collab_server_remove_client(srv, slot);
        return;
    }

    // Set client info
    client->client_id = client_id;
    client->color_index = color_index;
    strncpy(client->name, join_msg.client_name, COLLAB_MAX_CLIENT_NAME - 1);
    client->name[COLLAB_MAX_CLIENT_NAME - 1] = '\0';
    client->is_joined = true;
    client->join_time_ms = collab_time_ms();

    // Count joined clients
    U8 num_joined = 0;
    for (int i = 0; i < COLLAB_MAX_CLIENTS; i++) {
        if (srv->clients[i].is_joined) {
            num_joined++;
        }
    }

    // Send accept message
    U8 accept_payload[4];
    collab_serialize_client_accept(accept_payload, client_id, color_index, num_joined);
    if (!collab_send_message(&client->conn, MSG_CLIENT_ACCEPT, accept_payload, 4, &srv->last_error)) {
        COLLAB_LOG_ERROR("Failed to send accept to client %s", client->name);
        collab_server_remove_client(srv, slot);
        return;
    }

    // Send full grid sync
    if (!collab_server_send_full_sync(srv, slot)) {
        COLLAB_LOG_ERROR("Failed to send grid sync to client %s", client->name);
        collab_server_remove_client(srv, slot);
        return;
    }

    COLLAB_LOG_INFO("Client joined: %s (id=%u, color=%u)", client->name, client_id, color_index);

    // Notify via callback
    if (srv->client_event_callback) {
        srv->client_event_callback(srv->client_event_userdata,
                                   COLLAB_EVENT_CLIENT_JOINED,
                                   client_id, color_index, client->name);
    }
}

// Handle client leave
void collab_server_handle_leave(Collab_server *srv, int slot) {
    COLLAB_LOG_INFO("Client %s leaving gracefully", srv->clients[slot].name);
    collab_server_remove_client(srv, slot);
}

// Handle grid write from client
void collab_server_handle_write(Collab_server *srv, int slot,
                                 const U8 *payload, Usz len) {
    Msg_grid_write write_msg;
    if (!collab_deserialize_grid_write(payload, len, &write_msg)) {
        COLLAB_LOG_WARN("Invalid grid write from slot %d", slot);
        return;
    }

    // Apply to server grid
    if (!collab_apply_write(&srv->grid, write_msg.y, write_msg.x, write_msg.glyph)) {
        COLLAB_LOG_WARN("Grid write out of bounds from client %s: (%u, %u)",
                        srv->clients[slot].name, write_msg.y, write_msg.x);
        return;
    }

    collab_hash_cache_invalidate(&srv->hash_cache);
    srv->grid_dirty = true;

    // Broadcast to all clients (including sender for confirmation)
    collab_server_broadcast(srv, MSG_GRID_WRITE, payload, len);

    COLLAB_LOG_DEBUG("Write from %s: (%u, %u) = '%c'",
                     srv->clients[slot].name, write_msg.y, write_msg.x, write_msg.glyph);
}

// Handle grid write batch from client
void collab_server_handle_write_batch(Collab_server *srv, int slot,
                                       const U8 *payload, Usz len) {
    U8 client_id;
    U16 num_writes;
    if (!collab_deserialize_grid_write_batch_header(payload, len, &client_id, &num_writes)) {
        COLLAB_LOG_WARN("Invalid write batch from slot %d", slot);
        return;
    }

    // Parse and apply all writes
    const U8 *ptr = payload + MSG_GRID_WRITE_BATCH_HEADER_SIZE;
    Usz applied = 0;
    for (U16 i = 0; i < num_writes; i++) {
        Collab_write_tuple write;
        collab_parse_write_tuple(ptr, &write);
        if (collab_apply_write(&srv->grid, write.y, write.x, write.glyph)) {
            applied++;
        }
        ptr += MSG_GRID_WRITE_TUPLE_SIZE;
    }

    collab_hash_cache_invalidate(&srv->hash_cache);
    srv->grid_dirty = true;

    // Broadcast to all clients
    collab_server_broadcast(srv, MSG_GRID_WRITE_BATCH, payload, len);

    COLLAB_LOG_DEBUG("Write batch from %s: %u writes (%zu applied)",
                     srv->clients[slot].name, num_writes, applied);
}

// Handle client hash verification
void collab_server_handle_hash(Collab_server *srv, int slot,
                                const U8 *payload, Usz len) {
    Msg_client_hash hash_msg;
    if (!collab_deserialize_client_hash(payload, len, &hash_msg)) {
        COLLAB_LOG_WARN("Invalid hash message from slot %d", slot);
        return;
    }

    // Compute server hash
    U64 server_hash = collab_compute_grid_hash(&srv->grid, &srv->hash_cache);

    if (hash_msg.hash != server_hash) {
        COLLAB_LOG_WARN("Hash mismatch from %s: client=%llx, server=%llx",
                        srv->clients[slot].name,
                        (unsigned long long)hash_msg.hash,
                        (unsigned long long)server_hash);

        // Send resync request and full grid
        collab_server_send_resync_request(srv, slot);
        collab_server_send_full_sync(srv, slot);
    } else {
        COLLAB_LOG_DEBUG("Hash verified for %s: %llx",
                         srv->clients[slot].name, (unsigned long long)hash_msg.hash);
    }
}

// Remove a client
void collab_server_remove_client(Collab_server *srv, int slot) {
    Collab_client_slot *client = &srv->clients[slot];

    if (client->conn.fd < 0) {
        return;
    }

    COLLAB_LOG_INFO("Removing client: %s (slot %d)",
                    client->name[0] ? client->name : "unknown", slot);

    // Save client info for callback before clearing
    bool was_joined = client->is_joined;
    U8 client_id = client->client_id;
    U8 color_index = client->color_index;
    char name[COLLAB_MAX_CLIENT_NAME];
    strncpy(name, client->name, COLLAB_MAX_CLIENT_NAME);
    name[COLLAB_MAX_CLIENT_NAME - 1] = '\0';

    // Release color if joined
    if (was_joined) {
        collab_server_release_client_id(srv, color_index);
    }

    // Close connection
    collab_connection_close(&client->conn);

    // Reset slot
    client->client_id = 0;
    client->color_index = 0;
    client->name[0] = '\0';
    client->is_joined = false;

    if (srv->num_connected > 0) {
        srv->num_connected--;
    }

    // Notify via callback (after cleanup, so callback sees accurate state)
    if (was_joined && srv->client_event_callback) {
        srv->client_event_callback(srv->client_event_userdata,
                                   COLLAB_EVENT_CLIENT_LEFT,
                                   client_id, color_index, name);
    }
}

// Handle a client message
void collab_server_handle_message(Collab_server *srv, int slot,
                                   Collab_msg_type type,
                                   const U8 *payload, Usz len) {
    switch (type) {
        case MSG_HEARTBEAT:
            // Just update activity timestamp (done in recv)
            break;

        case MSG_CLIENT_JOIN:
            collab_server_handle_join(srv, slot, payload, len);
            break;

        case MSG_CLIENT_LEAVE:
            collab_server_handle_leave(srv, slot);
            break;

        case MSG_GRID_WRITE:
            if (srv->clients[slot].is_joined) {
                collab_server_handle_write(srv, slot, payload, len);
            }
            break;

        case MSG_GRID_WRITE_BATCH:
            if (srv->clients[slot].is_joined) {
                collab_server_handle_write_batch(srv, slot, payload, len);
            }
            break;

        case MSG_CLIENT_HASH:
            if (srv->clients[slot].is_joined) {
                collab_server_handle_hash(srv, slot, payload, len);
            }
            break;

        case MSG_CURSOR_UPDATE:
            // Broadcast cursor update to other clients and notify local TUI
            if (srv->clients[slot].is_joined) {
                // Broadcast to other clients
                collab_server_broadcast_except(srv, slot, MSG_CURSOR_UPDATE, payload, len);

                // Parse and store for local display
                Msg_cursor_update cursor_msg;
                if (collab_deserialize_cursor_update(payload, len, &cursor_msg)) {
                    srv->pending_cursor = cursor_msg;
                    srv->has_pending_cursor = true;

                    if (srv->cursor_callback) {
                        srv->cursor_callback(srv->cursor_callback_userdata, &cursor_msg);
                    }
                }
            }
            break;

        default:
            COLLAB_LOG_WARN("Unknown message type %u from slot %d", type, slot);
            break;
    }
}

// Update server
void collab_server_update(Collab_server *srv) {
    if (!srv->is_running) {
        return;
    }

    // Build poll set
    collab_poll_clear(&srv->poll_set);
    collab_poll_add_read(&srv->poll_set, srv->listener.listen_fd);

    for (int i = 0; i < COLLAB_MAX_CLIENTS; i++) {
        if (srv->clients[i].conn.fd >= 0) {
            collab_poll_add_read(&srv->poll_set, srv->clients[i].conn.fd);
        }
    }

    // Poll (non-blocking)
    int ready = collab_poll(&srv->poll_set, 0);
    if (ready <= 0) {
        return;
    }

    // Check for new connections
    if (collab_poll_can_read(&srv->poll_set, srv->listener.listen_fd)) {
        collab_server_accept_client(srv);
    }

    // Check clients for data
    for (int i = 0; i < COLLAB_MAX_CLIENTS; i++) {
        Collab_client_slot *client = &srv->clients[i];
        if (client->conn.fd < 0) {
            continue;
        }

        if (!collab_poll_can_read(&srv->poll_set, client->conn.fd)) {
            continue;
        }

        // Receive data
        Collab_sock_result result = collab_recv(&client->conn, &srv->last_error);
        if (result == COLLAB_SOCK_CLOSED) {
            COLLAB_LOG_INFO("Client %s disconnected", client->name[0] ? client->name : "unknown");
            collab_server_remove_client(srv, i);
            continue;
        }
        if (result == COLLAB_SOCK_ERROR) {
            COLLAB_LOG_WARN("Receive error from client %s: %s",
                           client->name[0] ? client->name : "unknown",
                           srv->last_error.message);
            collab_server_remove_client(srv, i);
            continue;
        }

        // Process complete messages
        while (collab_has_message(&client->conn)) {
            Collab_msg_header header;
            const U8 *payload;
            Usz payload_len;

            if (!collab_get_message(&client->conn, &header, &payload, &payload_len)) {
                COLLAB_LOG_WARN("Failed to get message from client %s",
                               client->name[0] ? client->name : "unknown");
                collab_server_remove_client(srv, i);
                break;
            }

            collab_server_handle_message(srv, i, (Collab_msg_type)header.msg_type,
                                          payload, payload_len);
        }
    }

    // Check for client timeouts
    U64 now = collab_time_ms();
    for (int i = 0; i < COLLAB_MAX_CLIENTS; i++) {
        Collab_client_slot *client = &srv->clients[i];
        if (client->conn.fd < 0) {
            continue;
        }

        U64 last_activity = client->conn.last_activity_ms;
        if (now - last_activity > COLLAB_CLIENT_TIMEOUT_MS) {
            COLLAB_LOG_WARN("Client %s timed out", client->name[0] ? client->name : "unknown");
            collab_server_remove_client(srv, i);
        }
    }
}

// Broadcast message to all connected clients
void collab_server_broadcast(Collab_server *srv, Collab_msg_type type,
                              const U8 *payload, Usz payload_len) {
    for (int i = 0; i < COLLAB_MAX_CLIENTS; i++) {
        if (srv->clients[i].conn.fd >= 0 && srv->clients[i].is_joined) {
            if (!collab_send_message(&srv->clients[i].conn, type, payload, payload_len, &srv->last_error)) {
                COLLAB_LOG_WARN("Failed to broadcast to client %s", srv->clients[i].name);
            }
        }
    }
}

// Broadcast message to all clients except one
void collab_server_broadcast_except(Collab_server *srv, int except_slot,
                                     Collab_msg_type type,
                                     const U8 *payload, Usz payload_len) {
    for (int i = 0; i < COLLAB_MAX_CLIENTS; i++) {
        if (i == except_slot) continue;
        if (srv->clients[i].conn.fd >= 0 && srv->clients[i].is_joined) {
            if (!collab_send_message(&srv->clients[i].conn, type, payload, payload_len, &srv->last_error)) {
                COLLAB_LOG_WARN("Failed to broadcast to client %s", srv->clients[i].name);
            }
        }
    }
}

// Send full grid sync to a client
bool collab_server_send_full_sync(Collab_server *srv, int slot) {
    Collab_client_slot *client = &srv->clients[slot];
    if (client->conn.fd < 0) {
        return false;
    }

    // Allocate buffer for full sync message
    Usz grid_size = (Usz)srv->grid.width * (Usz)srv->grid.height;
    Usz payload_size = MSG_GRID_FULL_SYNC_HEADER_SIZE + grid_size;

    U8 *payload = malloc(payload_size);
    if (!payload) {
        collab_error_set(&srv->last_error, -1, "Failed to allocate sync buffer");
        return false;
    }

    Usz actual_size = collab_serialize_grid_full_sync(payload, &srv->grid,
                                                       srv->tick_num, srv->is_playing, srv->bpm);

    bool success = collab_send_message(&client->conn, MSG_GRID_FULL_SYNC,
                                        payload, actual_size, &srv->last_error);
    free(payload);

    if (success) {
        COLLAB_LOG_DEBUG("Sent full sync to %s: %ux%u grid",
                         client->name, srv->grid.width, srv->grid.height);
    }

    return success;
}

// Send resync request to a client
bool collab_server_send_resync_request(Collab_server *srv, int slot) {
    Collab_client_slot *client = &srv->clients[slot];
    if (client->conn.fd < 0) {
        return false;
    }

    return collab_send_message(&client->conn, MSG_RESYNC_REQUEST, NULL, 0, &srv->last_error);
}

// Set playback state (broadcasts MSG_PLAY_STATE to all clients)
void collab_server_set_playing(Collab_server *srv, bool is_playing, U64 tick_num, double accum_secs) {
    srv->is_playing = is_playing;
    srv->tick_num = tick_num;
    srv->accum_secs = accum_secs;

    // Broadcast play state to all clients
    U8 payload[MSG_PLAY_STATE_SIZE];
    collab_serialize_play_state(payload, is_playing, tick_num, srv->bpm, accum_secs);
    collab_server_broadcast(srv, MSG_PLAY_STATE, payload, MSG_PLAY_STATE_SIZE);

    COLLAB_LOG_INFO("Broadcast play state: playing=%d, tick=%llu, bpm=%u",
                    is_playing, (unsigned long long)tick_num, srv->bpm);
}

// Set BPM (broadcasts MSG_BPM_UPDATE to all clients)
void collab_server_set_bpm(Collab_server *srv, U16 bpm) {
    if (srv->bpm == bpm) {
        return;  // No change
    }

    srv->bpm = bpm;

    // Broadcast BPM update to all clients
    U8 payload[MSG_BPM_UPDATE_SIZE];
    collab_serialize_bpm_update(payload, bpm);
    collab_server_broadcast(srv, MSG_BPM_UPDATE, payload, MSG_BPM_UPDATE_SIZE);

    COLLAB_LOG_INFO("Broadcast BPM update: %u", bpm);
}

// Set tick (update server state, for sync)
void collab_server_set_tick(Collab_server *srv, U64 tick_num, double accum_secs) {
    srv->tick_num = tick_num;
    srv->accum_secs = accum_secs;
}

// Broadcast tick sync to all clients (call periodically during playback)
void collab_server_broadcast_tick_sync(Collab_server *srv) {
    if (!srv->is_running) {
        return;
    }

    U8 payload[MSG_TICK_SYNC_SIZE];
    collab_serialize_tick_sync(payload, srv->tick_num, srv->accum_secs);
    collab_server_broadcast(srv, MSG_TICK_SYNC, payload, MSG_TICK_SYNC_SIZE);

    srv->last_tick_sync_ms = collab_time_ms();

    COLLAB_LOG_DEBUG("Broadcast tick sync: tick=%llu", (unsigned long long)srv->tick_num);
}

// Apply a write to server grid
bool collab_server_write(Collab_server *srv, U16 y, U16 x, Glyph glyph) {
    if (!collab_apply_write(&srv->grid, y, x, glyph)) {
        return false;
    }

    collab_hash_cache_invalidate(&srv->hash_cache);

    // Broadcast to all clients (client_id = 0 for server writes)
    U8 payload[6];
    collab_serialize_grid_write(payload, 0, y, x, glyph);
    collab_server_broadcast(srv, MSG_GRID_WRITE, payload, 6);

    return true;
}

// Apply a batch of writes to server grid
bool collab_server_write_batch(Collab_server *srv, const Collab_write_tuple *writes, U16 num_writes) {
    Usz applied = collab_apply_write_batch(&srv->grid, writes, num_writes);
    if (applied == 0) {
        return false;
    }

    collab_hash_cache_invalidate(&srv->hash_cache);

    // Broadcast to all clients
    Usz payload_size = MSG_GRID_WRITE_BATCH_HEADER_SIZE + (Usz)num_writes * MSG_GRID_WRITE_TUPLE_SIZE;
    U8 *payload = malloc(payload_size);
    if (!payload) {
        return false;
    }

    collab_serialize_grid_write_batch(payload, 0, writes, num_writes);
    collab_server_broadcast(srv, MSG_GRID_WRITE_BATCH, payload, payload_size);
    free(payload);

    return true;
}

// Get server grid
const Field *collab_server_get_grid(const Collab_server *srv) {
    return &srv->grid;
}

bool collab_server_needs_redraw(Collab_server *srv) {
    bool dirty = srv->grid_dirty;
    srv->grid_dirty = false;
    return dirty;
}

// Get number of connected clients
U8 collab_server_get_client_count(const Collab_server *srv) {
    U8 count = 0;
    for (int i = 0; i < COLLAB_MAX_CLIENTS; i++) {
        if (srv->clients[i].is_joined) {
            count++;
        }
    }
    return count;
}

// Get client info by slot
const Collab_client_slot *collab_server_get_client(const Collab_server *srv, int slot) {
    if (slot < 0 || slot >= COLLAB_MAX_CLIENTS) {
        return NULL;
    }
    if (!srv->clients[slot].is_joined) {
        return NULL;
    }
    return &srv->clients[slot];
}

// Check if server is running
bool collab_server_is_running(const Collab_server *srv) {
    return srv->is_running;
}

// Get last error
const char *collab_server_get_error(const Collab_server *srv) {
    return srv->last_error.message;
}

// Set cursor update callback
void collab_server_set_cursor_callback(Collab_server *srv,
                                        Collab_cursor_callback callback,
                                        void *userdata) {
    srv->cursor_callback = callback;
    srv->cursor_callback_userdata = userdata;
}

// Set client event callback
void collab_server_set_client_event_callback(Collab_server *srv,
                                              Collab_client_event_callback callback,
                                              void *userdata) {
    srv->client_event_callback = callback;
    srv->client_event_userdata = userdata;
}

// Get last received cursor update (for polling)
bool collab_server_get_cursor_update(Collab_server *srv, Msg_cursor_update *out_cursor) {
    if (!srv->has_pending_cursor) {
        return false;
    }

    if (out_cursor) {
        *out_cursor = srv->pending_cursor;
    }
    srv->has_pending_cursor = false;
    return true;
}

// Send server's cursor position to all clients
bool collab_server_send_cursor(Collab_server *srv, U16 y, U16 x, U16 h, U16 w) {
    if (!srv->is_running) {
        return false;
    }

    U8 payload[9];
    collab_serialize_cursor_update(payload, COLLAB_SERVER_CURSOR_ID, y, x, h, w);
    collab_server_broadcast(srv, MSG_CURSOR_UPDATE, payload, 9);

    return true;
}
