// collab_cursor.h - Multi-cursor tracking for Orca collaboration
// Manages remote cursor positions and rendering
//
// Phase 3 of collaboration features

#pragma once
#include "../base.h"
#include "collab_net.h"
#include "collab_protocol.h"

// Cursor update throttling - max 20 updates/sec
#define COLLAB_CURSOR_THROTTLE_MS 50

// Cursor timeout - inactive for 10 seconds
#define COLLAB_CURSOR_TIMEOUT_MS 10000

// Remote cursor structure
// Represents another user's cursor position and selection
typedef struct {
    U8 client_id;           // Client ID (1-10, 0 = unused)
    U8 color_index;         // Maps to ncurses color pair (0-9)
    Usz y, x;               // Cursor position
    Usz h, w;               // Selection rectangle (1,1 = single cell)
    bool active;            // Is this cursor currently active?
    U64 last_update_ms;     // Time of last update (for timeout detection)
    char name[COLLAB_MAX_CLIENT_NAME]; // Client display name
} Remote_cursor;

// Remote cursor list
// Tracks up to 10 remote cursors (one per client)
typedef struct {
    Remote_cursor cursors[COLLAB_MAX_CLIENTS];
    U8 num_active;          // Count of active cursors
} Remote_cursor_list;

// Initialize remote cursor list
void remote_cursor_list_init(Remote_cursor_list *rcl);

// Clear all remote cursors
void remote_cursor_list_clear(Remote_cursor_list *rcl);

// Update or add a remote cursor
// Returns true if cursor was updated, false if list is full
bool remote_cursor_update(Remote_cursor_list *rcl,
                          U8 client_id,
                          U8 color_index,
                          Usz y, Usz x, Usz h, Usz w,
                          const char *name);

// Update a remote cursor from a cursor update message
// Uses existing color_index and name if already known
bool remote_cursor_update_from_msg(Remote_cursor_list *rcl,
                                   const Msg_cursor_update *msg);

// Remove a remote cursor by client_id
// Called when client disconnects
void remote_cursor_remove(Remote_cursor_list *rcl, U8 client_id);

// Check for timed out cursors and mark them inactive
// Returns number of cursors that timed out
int remote_cursor_check_timeouts(Remote_cursor_list *rcl);

// Get a cursor by client_id
// Returns NULL if not found or inactive
const Remote_cursor *remote_cursor_get(const Remote_cursor_list *rcl, U8 client_id);

// Get the number of active cursors
U8 remote_cursor_count(const Remote_cursor_list *rcl);

// Find an available slot in the cursor list
// Returns -1 if no slot available
int remote_cursor_find_slot(const Remote_cursor_list *rcl, U8 client_id);

// Set cursor info when client joins
// This stores the color and name so cursor updates can use them
bool remote_cursor_set_info(Remote_cursor_list *rcl,
                            U8 client_id,
                            U8 color_index,
                            const char *name);

// Mark a cursor as inactive (but keep info for potential reconnection)
void remote_cursor_deactivate(Remote_cursor_list *rcl, U8 client_id);

// Cursor update throttling state
typedef struct {
    U64 last_send_ms;       // Last time cursor update was sent
    Usz last_y, last_x;     // Last sent position
    Usz last_h, last_w;     // Last sent selection
    bool pending;           // Is there a pending update to send?
} Cursor_throttle;

// Initialize throttle state
void cursor_throttle_init(Cursor_throttle *throttle);

// Check if we should send a cursor update now
// Returns true if enough time has passed since last update
bool cursor_throttle_should_send(Cursor_throttle *throttle,
                                  Usz y, Usz x, Usz h, Usz w);

// Mark that we sent an update
void cursor_throttle_mark_sent(Cursor_throttle *throttle,
                               Usz y, Usz x, Usz h, Usz w);

// Check if cursor position has changed
bool cursor_throttle_position_changed(const Cursor_throttle *throttle,
                                       Usz y, Usz x, Usz h, Usz w);
