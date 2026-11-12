// collab_server.h - Server-side collaboration logic
// Client management, message broadcasting, grid source-of-truth

#pragma once
#include "../base.h"
#include "../field.h"
#include "collab_net.h"
#include "collab_sync.h"

// Client colors for cursor rendering
typedef enum {
    COLLAB_COLOR_CYAN = 0,
    COLLAB_COLOR_GREEN = 1,
    COLLAB_COLOR_YELLOW = 2,
    COLLAB_COLOR_MAGENTA = 3,
    COLLAB_COLOR_RED = 4,
    COLLAB_COLOR_BLUE = 5,
    COLLAB_COLOR_PINK = 6,
    COLLAB_COLOR_LIME = 7,
    COLLAB_COLOR_PURPLE = 8,
    COLLAB_COLOR_TEAL = 9,
} Collab_client_color;

// Client slot structure
typedef struct {
    Collab_connection conn;
    U8 client_id;
    U8 color_index;
    char name[COLLAB_MAX_CLIENT_NAME];
    bool is_joined;           // Has completed join handshake
    U64 join_time_ms;
} Collab_client_slot;

// Forward declare cursor callback type
#ifndef COLLAB_CURSOR_CALLBACK_DEFINED
#define COLLAB_CURSOR_CALLBACK_DEFINED
typedef void (*Collab_cursor_callback)(void *userdata, const Msg_cursor_update *cursor);
#endif

// Client event types for notification callbacks
typedef enum {
    COLLAB_EVENT_CLIENT_JOINED = 0,
    COLLAB_EVENT_CLIENT_LEFT,
} Collab_client_event_type;

// Client event callback type
typedef void (*Collab_client_event_callback)(void *userdata,
                                              Collab_client_event_type event,
                                              U8 client_id,
                                              U8 color_index,
                                              const char *name);

// Server state
typedef struct {
    // Networking
    Collab_listener listener;
    Collab_client_slot clients[COLLAB_MAX_CLIENTS];
    U8 num_connected;

    // Grid (source of truth)
    Field grid;
    Collab_hash_cache hash_cache;

    // Playback state
    U64 tick_num;
    U16 bpm;
    bool is_playing;
    double accum_secs;  // Timing accumulator
    U64 last_tick_sync_ms;  // Last time tick sync was sent

    // Client ID assignment
    U8 next_client_id;
    bool color_available[COLLAB_MAX_CLIENTS];

    // Polling
    Collab_poll_set poll_set;

    // Status
    bool is_running;
    Collab_error last_error;

    // Cursor updates (for TUI integration)
    Collab_cursor_callback cursor_callback;
    void *cursor_callback_userdata;
    Msg_cursor_update pending_cursor;
    bool has_pending_cursor;

    // Client event callback (for TUI notifications)
    Collab_client_event_callback client_event_callback;
    void *client_event_userdata;
} Collab_server;

// Initialize server
void collab_server_init(Collab_server *srv);

// Deinitialize server (stops if running, frees resources)
void collab_server_deinit(Collab_server *srv);

// Start server on specified port
// Initializes with given grid as source of truth
bool collab_server_start(Collab_server *srv, U16 port, const Field *initial_grid);

// Stop server
void collab_server_stop(Collab_server *srv);

// Update server (call every frame)
// - Accepts new connections
// - Receives and processes messages
// - Handles disconnects and timeouts
void collab_server_update(Collab_server *srv);

// Set playback state (broadcasts to all clients)
// accum_secs is the timing accumulator for precise sync
void collab_server_set_playing(Collab_server *srv, bool is_playing, U64 tick_num, double accum_secs);

// Set BPM (broadcasts to all clients)
void collab_server_set_bpm(Collab_server *srv, U16 bpm);

// Set tick (for sync purposes - called periodically during playback)
void collab_server_set_tick(Collab_server *srv, U64 tick_num, double accum_secs);

// Broadcast tick sync to all clients (call periodically during playback)
void collab_server_broadcast_tick_sync(Collab_server *srv);

// Apply a write to server grid (and broadcast to all clients)
// Called when server user makes an edit
bool collab_server_write(Collab_server *srv, U16 y, U16 x, Glyph glyph);

// Apply a batch of writes to server grid (and broadcast to all clients)
bool collab_server_write_batch(Collab_server *srv, const Collab_write_tuple *writes, U16 num_writes);

// Get server grid (for rendering)
const Field *collab_server_get_grid(const Collab_server *srv);

// Get number of connected clients
U8 collab_server_get_client_count(const Collab_server *srv);

// Get client info by slot index (returns NULL if slot empty)
const Collab_client_slot *collab_server_get_client(const Collab_server *srv, int slot);

// Check if server is running
bool collab_server_is_running(const Collab_server *srv);

// Get last error message
const char *collab_server_get_error(const Collab_server *srv);

// Internal functions (exposed for testing)

// Accept a new client connection
bool collab_server_accept_client(Collab_server *srv);

// Handle a client message
void collab_server_handle_message(Collab_server *srv, int slot,
                                   Collab_msg_type type,
                                   const U8 *payload, Usz len);

// Handle client join request
void collab_server_handle_join(Collab_server *srv, int slot,
                                const U8 *payload, Usz len);

// Handle client leave
void collab_server_handle_leave(Collab_server *srv, int slot);

// Handle grid write from client
void collab_server_handle_write(Collab_server *srv, int slot,
                                 const U8 *payload, Usz len);

// Handle grid write batch from client
void collab_server_handle_write_batch(Collab_server *srv, int slot,
                                       const U8 *payload, Usz len);

// Handle client hash verification
void collab_server_handle_hash(Collab_server *srv, int slot,
                                const U8 *payload, Usz len);

// Remove a client (close connection, cleanup)
void collab_server_remove_client(Collab_server *srv, int slot);

// Broadcast message to all connected clients
void collab_server_broadcast(Collab_server *srv, Collab_msg_type type,
                              const U8 *payload, Usz payload_len);

// Broadcast message to all clients except one
void collab_server_broadcast_except(Collab_server *srv, int except_slot,
                                     Collab_msg_type type,
                                     const U8 *payload, Usz payload_len);

// Send full grid sync to a client
bool collab_server_send_full_sync(Collab_server *srv, int slot);

// Send resync request to a client
bool collab_server_send_resync_request(Collab_server *srv, int slot);

// Allocate a client ID and color
bool collab_server_allocate_client_id(Collab_server *srv, U8 *out_id, U8 *out_color);

// Release a client ID and color
void collab_server_release_client_id(Collab_server *srv, U8 color_index);

// Set callback for cursor updates from clients
void collab_server_set_cursor_callback(Collab_server *srv,
                                        Collab_cursor_callback callback,
                                        void *userdata);

// Set callback for client events (join/leave)
void collab_server_set_client_event_callback(Collab_server *srv,
                                              Collab_client_event_callback callback,
                                              void *userdata);

// Get last received cursor update (for polling)
bool collab_server_get_cursor_update(Collab_server *srv, Msg_cursor_update *out_cursor);

// Send server's cursor position to all clients
bool collab_server_send_cursor(Collab_server *srv, U16 y, U16 x, U16 h, U16 w);
