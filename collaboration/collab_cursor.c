// collab_cursor.c - Multi-cursor tracking for Orca collaboration
// Manages remote cursor positions and rendering

#include "collab_cursor.h"
#include <string.h>

// Initialize remote cursor list
void remote_cursor_list_init(Remote_cursor_list *rcl) {
    memset(rcl, 0, sizeof(*rcl));
    for (int i = 0; i < COLLAB_MAX_CLIENTS; i++) {
        rcl->cursors[i].client_id = 0;
        rcl->cursors[i].color_index = 0;
        rcl->cursors[i].y = 0;
        rcl->cursors[i].x = 0;
        rcl->cursors[i].h = 1;
        rcl->cursors[i].w = 1;
        rcl->cursors[i].active = false;
        rcl->cursors[i].last_update_ms = 0;
        rcl->cursors[i].name[0] = '\0';
    }
    rcl->num_active = 0;
}

// Clear all remote cursors
void remote_cursor_list_clear(Remote_cursor_list *rcl) {
    remote_cursor_list_init(rcl);
}

// Find slot for a client_id (existing or empty)
int remote_cursor_find_slot(const Remote_cursor_list *rcl, U8 client_id) {
    // First, try to find existing slot for this client
    for (int i = 0; i < COLLAB_MAX_CLIENTS; i++) {
        if (rcl->cursors[i].client_id == client_id && client_id != 0) {
            return i;
        }
    }

    // If not found, find first empty slot
    for (int i = 0; i < COLLAB_MAX_CLIENTS; i++) {
        if (rcl->cursors[i].client_id == 0 && !rcl->cursors[i].active) {
            return i;
        }
    }

    return -1; // No slot available
}

// Update or add a remote cursor
bool remote_cursor_update(Remote_cursor_list *rcl,
                          U8 client_id,
                          U8 color_index,
                          Usz y, Usz x, Usz h, Usz w,
                          const char *name) {
    if (client_id == 0) {
        return false; // Invalid client_id
    }

    int slot = remote_cursor_find_slot(rcl, client_id);
    if (slot < 0) {
        return false; // No slot available
    }

    Remote_cursor *cursor = &rcl->cursors[slot];

    // Update count if newly activated
    if (!cursor->active) {
        rcl->num_active++;
    }

    cursor->client_id = client_id;
    cursor->color_index = color_index;
    cursor->y = y;
    cursor->x = x;
    cursor->h = h;
    cursor->w = w;
    cursor->active = true;
    cursor->last_update_ms = collab_time_ms();

    if (name && name[0] != '\0') {
        strncpy(cursor->name, name, COLLAB_MAX_CLIENT_NAME - 1);
        cursor->name[COLLAB_MAX_CLIENT_NAME - 1] = '\0';
    }

    return true;
}

// Update a remote cursor from a cursor update message
bool remote_cursor_update_from_msg(Remote_cursor_list *rcl,
                                   const Msg_cursor_update *msg) {
    if (msg->client_id == 0) {
        return false;
    }

    // Find the existing cursor for this client (should already exist from join)
    int slot = remote_cursor_find_slot(rcl, msg->client_id);
    if (slot < 0) {
        return false;
    }

    Remote_cursor *cursor = &rcl->cursors[slot];

    // If cursor doesn't exist yet for this client, create with default name
    if (cursor->client_id == 0) {
        cursor->client_id = msg->client_id;
        cursor->color_index = msg->client_id % COLLAB_MAX_CLIENTS;
        cursor->name[0] = '\0';
    }

    // Update count if newly activated
    if (!cursor->active) {
        rcl->num_active++;
    }

    // Update position
    cursor->y = msg->y;
    cursor->x = msg->x;
    cursor->h = msg->h > 0 ? msg->h : 1;
    cursor->w = msg->w > 0 ? msg->w : 1;
    cursor->active = true;
    cursor->last_update_ms = collab_time_ms();

    return true;
}

// Remove a remote cursor by client_id
void remote_cursor_remove(Remote_cursor_list *rcl, U8 client_id) {
    for (int i = 0; i < COLLAB_MAX_CLIENTS; i++) {
        if (rcl->cursors[i].client_id == client_id) {
            if (rcl->cursors[i].active && rcl->num_active > 0) {
                rcl->num_active--;
            }
            // Reset the slot completely
            rcl->cursors[i].client_id = 0;
            rcl->cursors[i].color_index = 0;
            rcl->cursors[i].y = 0;
            rcl->cursors[i].x = 0;
            rcl->cursors[i].h = 1;
            rcl->cursors[i].w = 1;
            rcl->cursors[i].active = false;
            rcl->cursors[i].last_update_ms = 0;
            rcl->cursors[i].name[0] = '\0';
            return;
        }
    }
}

// Check for timed out cursors and mark them inactive
int remote_cursor_check_timeouts(Remote_cursor_list *rcl) {
    U64 now = collab_time_ms();
    int timed_out = 0;

    for (int i = 0; i < COLLAB_MAX_CLIENTS; i++) {
        Remote_cursor *cursor = &rcl->cursors[i];
        if (cursor->active && cursor->client_id != 0) {
            if (now - cursor->last_update_ms > COLLAB_CURSOR_TIMEOUT_MS) {
                cursor->active = false;
                if (rcl->num_active > 0) {
                    rcl->num_active--;
                }
                timed_out++;
                COLLAB_LOG_INFO("Cursor timed out for client %u (%s)",
                               cursor->client_id,
                               cursor->name[0] ? cursor->name : "unknown");
            }
        }
    }

    return timed_out;
}

// Get a cursor by client_id
const Remote_cursor *remote_cursor_get(const Remote_cursor_list *rcl, U8 client_id) {
    for (int i = 0; i < COLLAB_MAX_CLIENTS; i++) {
        if (rcl->cursors[i].client_id == client_id && rcl->cursors[i].active) {
            return &rcl->cursors[i];
        }
    }
    return NULL;
}

// Get the number of active cursors
U8 remote_cursor_count(const Remote_cursor_list *rcl) {
    return rcl->num_active;
}

// Set cursor info when client joins
bool remote_cursor_set_info(Remote_cursor_list *rcl,
                            U8 client_id,
                            U8 color_index,
                            const char *name) {
    if (client_id == 0) {
        return false;
    }

    int slot = remote_cursor_find_slot(rcl, client_id);
    if (slot < 0) {
        return false;
    }

    Remote_cursor *cursor = &rcl->cursors[slot];
    cursor->client_id = client_id;
    cursor->color_index = color_index;

    if (name) {
        strncpy(cursor->name, name, COLLAB_MAX_CLIENT_NAME - 1);
        cursor->name[COLLAB_MAX_CLIENT_NAME - 1] = '\0';
    }

    // Don't mark as active yet - wait for first position update
    cursor->last_update_ms = collab_time_ms();

    return true;
}

// Mark a cursor as inactive (but keep info for potential reconnection)
void remote_cursor_deactivate(Remote_cursor_list *rcl, U8 client_id) {
    for (int i = 0; i < COLLAB_MAX_CLIENTS; i++) {
        if (rcl->cursors[i].client_id == client_id) {
            if (rcl->cursors[i].active && rcl->num_active > 0) {
                rcl->num_active--;
            }
            rcl->cursors[i].active = false;
            return;
        }
    }
}

// Initialize throttle state
void cursor_throttle_init(Cursor_throttle *throttle) {
    memset(throttle, 0, sizeof(*throttle));
    throttle->last_send_ms = 0;
    throttle->last_y = 0;
    throttle->last_x = 0;
    throttle->last_h = 1;
    throttle->last_w = 1;
    throttle->pending = false;
}

// Check if cursor position has changed
bool cursor_throttle_position_changed(const Cursor_throttle *throttle,
                                       Usz y, Usz x, Usz h, Usz w) {
    return throttle->last_y != y ||
           throttle->last_x != x ||
           throttle->last_h != h ||
           throttle->last_w != w;
}

// Check if we should send a cursor update now
bool cursor_throttle_should_send(Cursor_throttle *throttle,
                                  Usz y, Usz x, Usz h, Usz w) {
    // Always track if position changed
    if (!cursor_throttle_position_changed(throttle, y, x, h, w)) {
        return false; // No change, no need to send
    }

    U64 now = collab_time_ms();
    if (now - throttle->last_send_ms >= COLLAB_CURSOR_THROTTLE_MS) {
        return true; // Enough time has passed
    }

    // Not enough time, mark as pending
    throttle->pending = true;
    return false;
}

// Mark that we sent an update
void cursor_throttle_mark_sent(Cursor_throttle *throttle,
                               Usz y, Usz x, Usz h, Usz w) {
    throttle->last_send_ms = collab_time_ms();
    throttle->last_y = y;
    throttle->last_x = x;
    throttle->last_h = h;
    throttle->last_w = w;
    throttle->pending = false;
}
