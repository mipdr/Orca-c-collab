// collab_client.h - Client-side collaboration logic
// Server connection, grid synchronization, write operations

#pragma once
#include "../base.h"
#include "../field.h"
#include "collab_net.h"
#include "collab_sync.h"

// Client states
typedef enum {
    COLLAB_CLIENT_DISCONNECTED = 0,
    COLLAB_CLIENT_CONNECTING,      // TCP connection in progress
    COLLAB_CLIENT_CONNECTED,       // TCP connected, sending join
    COLLAB_CLIENT_JOINING,         // Waiting for accept/reject
    COLLAB_CLIENT_SYNCING,         // Waiting for full grid sync
    COLLAB_CLIENT_ACTIVE,          // Fully connected and synchronized
    COLLAB_CLIENT_ERROR,           // Error state
} Collab_client_state;

// Forward declare cursor callback type (defined in collab_server.h or collab_protocol.h)
// This is a function pointer: void (*)(void *userdata, const Msg_cursor_update *cursor)
#ifndef COLLAB_CURSOR_CALLBACK_DEFINED
#define COLLAB_CURSOR_CALLBACK_DEFINED
typedef void (*Collab_cursor_callback)(void *userdata, const Msg_cursor_update *cursor);
#endif

// Client structure
typedef struct {
    // Networking
    Collab_connection conn;
    char server_host[256];
    U16 server_port;

    // State
    Collab_client_state state;

    // Identity
    U8 my_client_id;
    U8 my_color_index;
    char my_name[COLLAB_MAX_CLIENT_NAME];

    // Grid (local copy)
    Field grid;
    Collab_hash_cache hash_cache;
    bool needs_resync;

    // Playback state (from server)
    U64 tick_num;
    U16 bpm;
    bool is_playing;
    double accum_secs;  // Timing accumulator
    bool playback_changed;  // Flag to notify TUI of playback state change

    // Timing
    U64 last_hash_time_ms;
    U64 last_cursor_time_ms;

    // Polling
    Collab_poll_set poll_set;

    // Error tracking
    Collab_error last_error;

    // Cursor updates (for TUI integration)
    Collab_cursor_callback cursor_callback;
    void *cursor_callback_userdata;
    Msg_cursor_update pending_cursor;
    bool has_pending_cursor;
} Collab_client;

// Initialize client
void collab_client_init(Collab_client *client);

// Deinitialize client
void collab_client_deinit(Collab_client *client);

// Connect to server
// name: client display name (up to 31 chars)
bool collab_client_connect(Collab_client *client, const char *host, U16 port, const char *name);

// Disconnect from server
void collab_client_disconnect(Collab_client *client);

// Update client (call every frame)
// Returns true if there are pending updates to process
bool collab_client_update(Collab_client *client);

// Get client state
Collab_client_state collab_client_get_state(const Collab_client *client);

// Get client state as string (for debugging/display)
const char *collab_client_state_string(Collab_client_state state);

// Get client grid (local copy)
Field *collab_client_get_grid(Collab_client *client);

// Get client grid (const)
const Field *collab_client_get_grid_const(const Collab_client *client);

// Write a single cell (applies locally and sends to server)
bool collab_client_write(Collab_client *client, U16 y, U16 x, Glyph glyph);

// Write a batch of cells (applies locally and sends to server)
bool collab_client_write_batch(Collab_client *client, const Collab_write_tuple *writes, U16 num_writes);

// Send cursor update to server
bool collab_client_send_cursor(Collab_client *client, U16 y, U16 x, U16 h, U16 w);

// Send hash verification to server
bool collab_client_send_hash(Collab_client *client);

// Get client ID
U8 collab_client_get_id(const Collab_client *client);

// Get client color
U8 collab_client_get_color(const Collab_client *client);

// Get client name
const char *collab_client_get_name(const Collab_client *client);

// Check if client is fully connected and active
bool collab_client_is_active(const Collab_client *client);

// Get last error message
const char *collab_client_get_error(const Collab_client *client);

// Check if client needs display update (grid changed)
bool collab_client_needs_redraw(const Collab_client *client);

// Check if playback state changed (and clear the flag)
bool collab_client_playback_changed(Collab_client *client);

// Get current playback state from server
bool collab_client_get_playback_state(const Collab_client *client,
                                       bool *is_playing, U64 *tick_num,
                                       U16 *bpm, double *accum_secs);

// Internal handlers

// Handle accept message
void collab_client_handle_accept(Collab_client *client, const U8 *payload, Usz len);

// Handle reject message
void collab_client_handle_reject(Collab_client *client, const U8 *payload, Usz len);

// Handle full grid sync
void collab_client_handle_full_sync(Collab_client *client, const U8 *payload, Usz len);

// Handle single grid write
void collab_client_handle_write(Collab_client *client, const U8 *payload, Usz len);

// Handle batch grid write
void collab_client_handle_write_batch(Collab_client *client, const U8 *payload, Usz len);

// Handle resync request
void collab_client_handle_resync_request(Collab_client *client);

// Handle cursor update from another client
void collab_client_handle_cursor_update(Collab_client *client, const U8 *payload, Usz len);

// Handle play state from server
void collab_client_handle_play_state(Collab_client *client, const U8 *payload, Usz len);

// Handle BPM update from server
void collab_client_handle_bpm_update(Collab_client *client, const U8 *payload, Usz len);

// Handle tick sync from server
void collab_client_handle_tick_sync(Collab_client *client, const U8 *payload, Usz len);

// Process a received message
void collab_client_handle_message(Collab_client *client, Collab_msg_type type,
                                   const U8 *payload, Usz len);

// Set callback for cursor updates (called when remote cursor position changes)
// The callback receives the parsed cursor update message
void collab_client_set_cursor_callback(Collab_client *client,
                                        Collab_cursor_callback callback,
                                        void *userdata);

// Get last received cursor update (for polling instead of callback)
// Returns true if there was a new cursor update since last call
bool collab_client_get_cursor_update(Collab_client *client, Msg_cursor_update *out_cursor);
