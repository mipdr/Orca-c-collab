# Orca-C Collaboration Features Design

**Version:** 1.0
**Date:** 2024
**Status:** Planning / Proof of Concept

---

## Table of Contents

1. [Overview](#overview)
2. [High-Level Requirements](#high-level-requirements)
3. [Architecture Overview](#architecture-overview)
4. [Network Protocol Design](#network-protocol-design)
5. [Grid State Synchronization](#grid-state-synchronization)
6. [Multi-Cursor System](#multi-cursor-system)
7. [Collaboration State Machine](#collaboration-state-machine)
8. [TUI Integration Points](#tui-integration-points)
9. [Server-Specific Logic](#server-specific-logic)
10. [Technical Implementation Details](#technical-implementation-details)
11. [User Experience Flow](#user-experience-flow)
12. [Configuration Persistence](#configuration-persistence)
13. [Security Considerations](#security-considerations)
14. [Performance Considerations](#performance-considerations)
15. [Testing Strategy](#testing-strategy)
16. [Error Cases & Edge Scenarios](#error-cases--edge-scenarios)
17. [Undo/Redo Behavior](#undoredo-behavior-in-collaborative-mode)
18. [Implementation Roadmap](#implementation-roadmap)
19. [Out of Scope](#out-of-scope-future-work)

---

## Overview

This document outlines the design for implementing real-time collaboration features in Orca-C, allowing multiple users to edit the same Orca grid simultaneously over a network.

### Goals

- Enable multiple users (up to 10 clients + 1 server) to collaboratively edit an Orca grid
- Maintain grid state consistency across all participants
- Provide visual feedback showing each user's cursor position with unique colors
- Minimize network latency and bandwidth usage
- Integrate seamlessly with existing Orca-C TUI workflow

### Non-Goals (MVP)

- Authentication/authorization
- Encryption
- NAT traversal
- Web-based clients
- Voice/chat communication

---

## High-Level Requirements

### Modes of Operation

- **Server Mode**: Host creates a session, holds source-of-truth grid
- **Client Mode**: Users connect to server, receive synchronized grid
- **Standalone Mode**: Default single-user mode (existing behavior)

### Connection Model

- **Protocol**: TCP (reliable, ordered delivery)
- **Capacity**: 1 server + up to 10 clients
- **Network**: LAN or trusted network (no encryption in MVP)

### State Synchronization

- **Initial Sync**: Full grid sent to client on join
- **Incremental Updates**: Only write operations exchanged during session
- **Verification**: Periodic hash-based validation
- **Resync**: Automatic recovery from desynchronization

### Multi-Cursor System

- Each participant has their own cursor (position + selection rectangle)
- Cursors differentiated by color (10 distinct colors)
- Real-time cursor position updates
- Visual rendering of all active cursors

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                     ORCA COLLABORATION                      │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌─────────────┐         ┌──────────────────────────────┐ │
│  │   SERVER    │         │    CLIENT (1-10 instances)   │ │
│  │   (Host)    │◄───────►│                              │ │
│  │             │  TCP    │  - Receives full grid sync   │ │
│  │  - Source   │  Socket │  - Sends write commands      │ │
│  │    of Truth │         │  - Sends periodic hash       │ │
│  │  - Manages  │         │  - Own cursor + color        │ │
│  │    Clients  │         │                              │ │
│  │  - Syncs    │         └──────────────────────────────┘ │
│  │    State    │                                           │
│  └─────────────┘                                           │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### Key Components

1. **Network Transport Layer** (`collab_net.c/h`)
   - TCP socket management
   - Non-blocking I/O
   - Connection lifecycle

2. **Protocol Layer** (`collab_protocol.c/h`)
   - Message serialization/deserialization
   - Binary protocol with length-prefixed framing
   - Message validation

3. **Synchronization Layer** (`collab_sync.c/h`)
   - Grid state synchronization
   - Hash-based verification (xxHash64)
   - Conflict resolution (last-write-wins)

4. **Server Logic** (`collab_server.c/h`)
   - Client connection management
   - Message broadcasting
   - Source-of-truth grid maintenance

5. **Client Logic** (`collab_client.c/h`)
   - Server connection handling
   - Local grid update application
   - Periodic hash submission

---

## Network Protocol Design

### Transport Layer

- **Protocol**: TCP
- **Port**: Configurable (default 7777)
- **Format**: Binary with length-prefixed messages

**Rationale for TCP:**
- Grid synchronization requires reliable, ordered delivery
- Message loss would cause permanent desynchronization
- TCP overhead acceptable for LAN use case

### Message Types

```c
typedef enum {
    MSG_CLIENT_JOIN = 0x01,      // Client → Server: Request to join
    MSG_CLIENT_ACCEPT = 0x02,    // Server → Client: Join accepted + client_id
    MSG_CLIENT_REJECT = 0x03,    // Server → Client: Join rejected (full/error)
    MSG_CLIENT_LEAVE = 0x04,     // Client → Server: Disconnect
    MSG_GRID_FULL_SYNC = 0x10,   // Server → Client: Full grid state
    MSG_GRID_WRITE = 0x11,       // Bidirectional: Single cell write
    MSG_GRID_WRITE_BATCH = 0x12, // Bidirectional: Multiple cell writes
    MSG_CURSOR_UPDATE = 0x20,    // Bidirectional: Cursor position
    MSG_CLIENT_HASH = 0x30,      // Client → Server: Grid hash for verification
    MSG_RESYNC_REQUEST = 0x31,   // Server → Client: Hash mismatch, re-sync
    MSG_PLAY_STATE = 0x40,       // Server → Clients: Play/pause state
    MSG_BPM_UPDATE = 0x41,       // Server → Clients: Tempo change
    MSG_TICK_SYNC = 0x42,        // Server → Clients: Tick counter sync
} Collab_msg_type;
```

### Message Structure

#### Header (8 bytes)

```c
typedef struct {
    U32 magic;           // 0x4F524341 ("ORCA")
    U16 length;          // Payload length (0-65535)
    U8 msg_type;         // Collab_msg_type
    U8 flags;            // Reserved for future use
} Collab_msg_header;
```

All messages start with this header, followed by type-specific payload.

#### CLIENT_JOIN Payload

```c
typedef struct {
    U8 protocol_version; // Currently 1
    char client_name[32]; // UTF-8 name
} Msg_client_join;
```

**Flow:**
1. Client connects to server TCP socket
2. Client sends `MSG_CLIENT_JOIN` with desired name
3. Server validates and responds with `MSG_CLIENT_ACCEPT` or `MSG_CLIENT_REJECT`

#### CLIENT_ACCEPT Payload

```c
typedef struct {
    U8 client_id;        // Assigned ID (1-10)
    U8 color_index;      // Assigned color (for cursor)
    U8 num_clients;      // Total clients (including this one)
    U8 reserved;
} Msg_client_accept;
```

#### GRID_FULL_SYNC Payload

```c
typedef struct {
    U16 width;
    U16 height;
    Usz tick_num;
    U8 is_playing;
    U16 bpm;
    // Followed by: width * height bytes (glyphs)
} Msg_grid_full_sync;
```

**Size Example:** 64×32 grid = 2048 bytes + 12 byte header = 2060 bytes total

#### GRID_WRITE Payload (Single Cell)

```c
typedef struct {
    U8 client_id;        // Who wrote this
    U16 y;
    U16 x;
    Glyph glyph;         // Single byte character
} Msg_grid_write;
```

**Size:** 6 bytes + 8 byte header = 14 bytes per write

#### GRID_WRITE_BATCH Payload

```c
typedef struct {
    U8 client_id;
    U16 num_writes;      // Number of writes in batch
    // Followed by: num_writes × (y, x, glyph) tuples
} Msg_grid_write_batch;

// Each tuple is:
struct {
    U16 y;
    U16 x;
    Glyph glyph;
} Write_tuple;  // 5 bytes
```

**Use Case:** Paste operations, fill operations, batch edits

**Size Example:** 100-cell paste = 3 bytes header + 500 bytes tuples = 503 bytes

#### CURSOR_UPDATE Payload

```c
typedef struct {
    U8 client_id;
    U16 y;
    U16 x;
    U16 h;               // Selection height
    U16 w;               // Selection width
} Msg_cursor_update;
```

**Size:** 9 bytes + 8 byte header = 17 bytes

#### CLIENT_HASH Payload

```c
typedef struct {
    U8 client_id;
    U64 hash;            // xxHash64 of grid buffer
} Msg_client_hash;
```

**Size:** 9 bytes + 8 byte header = 17 bytes

### Message Framing

**Wire Format:**
```
[Magic: 4 bytes]["ORCA"]
[Length: 2 bytes][Payload size in bytes]
[Type: 1 byte][Message type enum]
[Flags: 1 byte][Reserved, set to 0]
[Payload: N bytes][Type-specific data]
```

**Handling Partial Reads:**
```c
typedef struct {
    U8 buffer[65536];
    Usz offset;        // Current read position
    Usz expected_len;  // Total message length (from header)
    bool has_header;   // Have we read the 8-byte header yet?
} Msg_buffer;
```

Each connection maintains a receive buffer for assembling fragmented TCP packets.

---

## Grid State Synchronization

### Initial Sync (Client Join)

**Sequence:**

```
Client                          Server
  |                                |
  |--- TCP Connect -------------->|
  |                                |
  |--- MSG_CLIENT_JOIN ---------->|
  |     ("Alice", v1)              |
  |                                |--- Validate capacity
  |                                |--- Assign ID=3, color=yellow
  |                                |
  |<-- MSG_CLIENT_ACCEPT ---------|
  |     (id=3, color=yellow)       |
  |                                |
  |<-- MSG_GRID_FULL_SYNC --------|
  |     (64x32 grid, tick=150)     |
  |                                |
  |--- Apply grid to local copy --|
  |                                |
  |                                |--- Broadcast to all clients:
  |                                |    "Alice joined"
  |                                |
  [CONNECTED - Ready to edit]
```

### Incremental Updates (Write Broadcasting)

**Optimistic Update Model:**

```
User types 'A' at (10, 20) on Client
    ↓
Client applies locally (immediate visual feedback)
    ↓
Client sends MSG_GRID_WRITE to Server
    ↓
Server receives, validates coordinates
    ↓
Server applies to source-of-truth grid
    ↓
Server broadcasts MSG_GRID_WRITE to ALL clients (including sender)
    ↓
Clients receive, apply to local grid
    ↓
Sender's optimistic update is confirmed (or corrected if conflict)
```

**Conflict Resolution:**
- **Last-write-wins** based on server receive timestamp
- Example: If Client A and Client B both write to (5, 5) simultaneously:
  - Server receives A's write first → broadcasts A's write
  - Server receives B's write second → broadcasts B's write (overwrites A)
  - All clients end up with B's value

### Hash-Based Verification

**Purpose:** Detect desynchronization due to bugs, packet corruption, or edge cases

**Algorithm:**

```c
// Every 5 seconds (configurable):
Client computes: hash = xxHash64(field.buffer, width * height)
Client sends: MSG_CLIENT_HASH with hash value
    ↓
Server compares with: server_hash = xxHash64(server_field.buffer, width * height)
    ↓
If hash == server_hash:
    Continue normally (grids are in sync)
Else:
    Server sends MSG_RESYNC_REQUEST
    Client sets grid_needs_resync = true
    Client waits for MSG_GRID_FULL_SYNC
    Server sends full grid
    Client applies and recomputes hash
```

**xxHash64 Properties:**
- Very fast: ~10 GB/s throughput
- Good distribution: Low collision probability
- Small output: 64-bit = 8 bytes
- Public domain: No licensing issues

**Performance:** Hashing 64×32 grid (2048 bytes) takes ~0.2 microseconds on modern CPUs

### Batch Optimization

**Problem:** Operations like paste/fill generate many writes (e.g., 100+ cells)

**Solution:** Batch into `MSG_GRID_WRITE_BATCH`

**Example: Paste 10×10 Block**

Without batching:
- 100 individual `MSG_GRID_WRITE` messages
- 100 × 14 bytes = 1400 bytes
- 100 network send syscalls

With batching:
- 1 `MSG_GRID_WRITE_BATCH` message
- 8 + 3 + (100 × 5) = 511 bytes
- 1 network send syscall
- ~63% size reduction, 99% syscall reduction

**Implementation:**

```c
// In ged_copy_paste():
if (collab_is_active(&a->collab_ctx)) {
    // Collect all writes into batch
    Collab_write_batch batch;
    batch.client_id = a->collab_ctx.my_client_id;
    batch.num_writes = 0;

    for (Usz y = 0; y < paste_h; y++) {
        for (Usz x = 0; x < paste_w; x++) {
            batch.writes[batch.num_writes++] = (Write_tuple){
                .y = dest_y + y,
                .x = dest_x + x,
                .glyph = clipboard[y * paste_w + x]
            };
        }
    }

    collab_send_write_batch(&a->collab_ctx, &batch);
}
```

---

## Multi-Cursor System

### Data Structures

#### Local Cursor (Existing)

```c
typedef struct {
    Usz y, x, h, w;  // Position and selection rectangle
} Ged_cursor;
```

**No changes needed to existing structure.**

#### Remote Cursor (New)

```c
typedef struct {
    U8 client_id;
    U8 color_index;      // Maps to ncurses color pair
    Usz y, x, h, w;      // Same format as local cursor
    bool active;         // Is this client still connected?
    U64 last_update_ms;  // For timeout detection (10s no update = inactive)
    char name[32];       // Client display name
} Remote_cursor;
```

#### Remote Cursor List (New)

```c
typedef struct {
    Remote_cursor cursors[10];  // Max 10 clients
    U8 num_active;
} Remote_cursor_list;

// Add to Ged structure:
struct Ged {
    // ... existing fields ...
    Remote_cursor_list remote_cursors;
    U8 my_client_id;       // 0 for server, 1-10 for clients
    U8 my_color_index;
};
```

### Cursor Color Assignments

**Extended Color Palette:**

```c
enum {
    // Existing colors
    C_natural = 0,
    C_black, C_red, C_green, C_yellow,
    C_blue, C_magenta, C_cyan, C_white,

    // NEW: Client cursor colors
    C_client_1,   // Bright cyan
    C_client_2,   // Bright green
    C_client_3,   // Bright yellow
    C_client_4,   // Bright magenta
    C_client_5,   // Orange (via COLOR_RED + A_BOLD if 256-color unavailable)
    C_client_6,   // Light blue
    C_client_7,   // Pink
    C_client_8,   // Lime
    C_client_9,   // Purple
    C_client_10,  // Teal
};

// Initialize in setup_ncurses() (tui_main.c):
void setup_client_colors(void) {
    if (has_colors()) {
        init_pair(C_client_1, COLOR_CYAN, COLOR_BLACK);
        init_pair(C_client_2, COLOR_GREEN, COLOR_BLACK);
        init_pair(C_client_3, COLOR_YELLOW, COLOR_BLACK);
        init_pair(C_client_4, COLOR_MAGENTA, COLOR_BLACK);
        init_pair(C_client_5, COLOR_RED, COLOR_BLACK);    // Orange fallback
        init_pair(C_client_6, COLOR_BLUE, COLOR_BLACK);
        init_pair(C_client_7, COLOR_MAGENTA, COLOR_BLACK); // Pink fallback
        init_pair(C_client_8, COLOR_GREEN, COLOR_BLACK);   // Lime fallback
        init_pair(C_client_9, COLOR_MAGENTA, COLOR_BLACK); // Purple fallback
        init_pair(C_client_10, COLOR_CYAN, COLOR_BLACK);   // Teal fallback
    }
}
```

**Future Enhancement:** Use 256-color mode for better distinction if terminal supports it.

### Cursor Rendering

**Current Implementation:**
- `draw_grid_cursor()` in tui_main.c draws local cursor only
- Uses reverse video + bold yellow
- Shows `~` (paused) or `@` (playing) on empty cells

**New Implementation:**

```c
void draw_grid_cursor_multi(WINDOW *win, Remote_cursor_list *rcl,
                              Ged_cursor *local, int scroll_y, int scroll_x,
                              U8 local_color_idx, bool is_playing) {
    // Draw local cursor first (so it's visually on top)
    draw_single_cursor(win, local->y, local->x, local->h, local->w,
                       scroll_y, scroll_x, local_color_idx, is_playing, true);

    // Draw remote cursors
    for (int i = 0; i < 10; i++) {
        if (rcl->cursors[i].active) {
            draw_single_cursor(win,
                               rcl->cursors[i].y, rcl->cursors[i].x,
                               rcl->cursors[i].h, rcl->cursors[i].w,
                               scroll_y, scroll_x,
                               rcl->cursors[i].color_index,
                               is_playing,
                               false);  // Not local
        }
    }
}

void draw_single_cursor(WINDOW *win, Usz y, Usz x, Usz h, Usz w,
                        int scroll_y, int scroll_x, U8 color_idx,
                        bool is_playing, bool is_local) {
    // Calculate screen coordinates
    int screen_y = (int)y - scroll_y;
    int screen_x = (int)x - scroll_x;

    // Check if visible
    if (screen_y < 0 || screen_y >= grid_h ||
        screen_x < 0 || screen_x >= grid_w) {
        return;
    }

    // Build attributes
    attr_t attrs = A_REVERSE | COLOR_PAIR(color_idx);
    if (is_local) {
        attrs |= A_BOLD;  // Local cursor is bolder
    }

    // Draw cursor character
    Glyph cursor_char;
    if (h == 1 && w == 1) {
        // Single-cell cursor
        Glyph cell_glyph = gbuffer_peek(field.buffer, field.height, field.width, y, x);
        cursor_char = (cell_glyph == '.') ? (is_playing ? '@' : '~') : cell_glyph;
    } else {
        // Multi-cell selection: show selection bounds
        cursor_char = '+';  // Could use box-drawing chars
    }

    mvwaddch(win, screen_y, screen_x, cursor_char | attrs);

    // If selection, highlight entire rectangle
    if (h > 1 || w > 1) {
        for (Usz sel_y = 0; sel_y < h; sel_y++) {
            for (Usz sel_x = 0; sel_x < w; sel_x++) {
                int scr_y = (int)(y + sel_y) - scroll_y;
                int scr_x = (int)(x + sel_x) - scroll_x;
                if (scr_y >= 0 && scr_y < grid_h && scr_x >= 0 && scr_x < grid_w) {
                    chtype existing = mvwinch(win, scr_y, scr_x);
                    mvwaddch(win, scr_y, scr_x, existing | attrs);
                }
            }
        }
    }
}
```

### Cursor Update Broadcasting

**Frequency:** Throttled to 20 updates/second (50ms interval) to avoid flooding

```c
void ged_cursor_move_relative(Ged *a, int delta_y, int delta_x) {
    // Existing movement logic
    ged_cursor_move_relative_old(a, delta_y, delta_x);

    // NEW: Broadcast to server (if in collaborative mode)
    if (collab_is_active(&a->collab_state)) {
        U64 now_ms = get_time_ms();
        if (now_ms - a->collab_state.last_cursor_send_ms > 50) {  // 50ms throttle
            collab_send_cursor_update(&a->collab_state, &a->ged_cursor);
            a->collab_state.last_cursor_send_ms = now_ms;
        }
    }
}

void collab_send_cursor_update(Collab_context *ctx, Ged_cursor *cursor) {
    Msg_cursor_update msg = {
        .client_id = ctx->my_client_id,
        .y = cursor->y,
        .x = cursor->x,
        .h = cursor->h,
        .w = cursor->w,
    };
    collab_send_message(ctx, MSG_CURSOR_UPDATE, &msg, sizeof(msg));
}
```

**Server Handling:**

```c
void collab_server_handle_cursor_update(Collab_server *srv, U8 client_id,
                                         const Msg_cursor_update *msg) {
    // Update server's copy of this client's cursor
    Remote_cursor *cursor = &srv->remote_cursors.cursors[client_id - 1];
    cursor->y = msg->y;
    cursor->x = msg->x;
    cursor->h = msg->h;
    cursor->w = msg->w;
    cursor->last_update_ms = get_time_ms();

    // Broadcast to all OTHER clients
    collab_server_broadcast_except(srv, client_id, msg, sizeof(*msg));
}
```

**Timeout Handling:**

```c
// In collab_update() called every frame:
void collab_update_cursor_timeouts(Remote_cursor_list *rcl) {
    U64 now_ms = get_time_ms();
    for (int i = 0; i < 10; i++) {
        if (rcl->cursors[i].active) {
            if (now_ms - rcl->cursors[i].last_update_ms > 10000) {  // 10s timeout
                rcl->cursors[i].active = false;
                rcl->num_active--;
                // Could show notification: "Alice timed out"
            }
        }
    }
}
```

---

## Collaboration State Machine

### Mode Enum

```c
typedef enum {
    COLLAB_MODE_STANDALONE,   // No networking (default)
    COLLAB_MODE_SERVER,       // Hosting session
    COLLAB_MODE_CLIENT,       // Connected to server
} Collab_mode;

typedef enum {
    COLLAB_STATE_DISCONNECTED,   // Not connected
    COLLAB_STATE_CONNECTING,     // Attempting connection (client only)
    COLLAB_STATE_CONNECTED,      // TCP connected, not yet synced
    COLLAB_STATE_SYNCING,        // Receiving full grid sync
    COLLAB_STATE_ACTIVE,         // Fully operational
    COLLAB_STATE_ERROR,          // Error state (can retry)
} Collab_state;
```

### State Transitions

**Server Mode:**
```
DISCONNECTED
    ↓ collab_start_server()
ACTIVE (listening for clients)
    ↓ collab_stop_server()
DISCONNECTED
```

**Client Mode:**
```
DISCONNECTED
    ↓ collab_connect_to_server()
CONNECTING (TCP handshake)
    ↓ Connected
CONNECTED (TCP established)
    ↓ Send MSG_CLIENT_JOIN
    ↓ Receive MSG_CLIENT_ACCEPT
SYNCING (waiting for MSG_GRID_FULL_SYNC)
    ↓ Receive full grid
ACTIVE (synchronized)
    ↓ Connection lost / MSG_RESYNC_REQUEST / user disconnect
ERROR or DISCONNECTED
```

### Collaboration Context

```c
typedef struct {
    Collab_mode mode;
    Collab_state state;

    // Network
    int listen_fd;            // Server: listening socket (-1 if not server)
    int server_fd;            // Client: connection to server (-1 if not client)
    int client_fds[10];       // Server: connections to clients
    U8 client_ids[10];        // Server: assigned IDs for each connection

    // Message buffers (per-connection)
    Msg_buffer recv_buffers[11];  // [0] = server conn, [1-10] = client conns
    U8 send_buffer[65536];        // Shared send buffer

    // Timing
    U64 last_hash_time_ms;
    U64 last_cursor_send_ms;

    // Configuration
    U16 port;
    char server_address[256]; // Client: server IP/hostname
    char my_name[32];

    // State
    U8 my_client_id;          // 0 for server, 1-10 for clients
    U8 my_color_index;
    bool grid_needs_resync;

    // Error tracking
    char last_error[256];
} Collab_context;
```

### Lifecycle Functions

```c
// Initialize (call once at startup)
void collab_init(Collab_context *ctx);

// Server mode
bool collab_start_server(Collab_context *ctx, U16 port);
void collab_stop_server(Collab_context *ctx);

// Client mode
bool collab_connect_to_server(Collab_context *ctx, const char *address, U16 port);
void collab_disconnect(Collab_context *ctx);

// Update (call every frame in event loop)
void collab_update(Collab_context *ctx, Ged *ged);

// Cleanup (call at shutdown)
void collab_destroy(Collab_context *ctx);
```

---

## TUI Integration Points

### Menu System Integration

**Modify Main Menu (tui_main.c):**

```c
// In create_main_menu():
Qmenu_item items[] = {
    {0, "New", 'n'},
    {0, "Open", 'o'},
    {0, "Save", 's'},
    {0, "Save As", 'a'},
    {0, "Set BPM", 'b'},
    {0, "Set Grid Size", 'g'},
    {0, "Collaboration...", 'c'},  // NEW
    {0, "OSC/MIDI...", 'm'},
    {0, "Appearance...", 'p'},
    {0, "Preferences...", 'r'},
    {0, "Controls", 'h'},
    {0, "Opers Guide", '?'},
    {0, "About", 'd'},
    {0, "Quit", 'q'},
    {0, NULL, 0}
};
```

**Create Collaboration Submenu:**

```c
Qmenu_item collab_items[] = {
    {0, "Start Server", 's'},
    {0, "Join Server", 'j'},
    {0, "Disconnect", 'd'},
    {0, "Settings", 'c'},
    {0, "Back", '\033'},
    {0, NULL, 0}
};

// Menu handler:
void handle_collab_menu(Ged *ged, int selection) {
    switch (selection) {
    case 0:  // Start Server
        show_start_server_form(ged);
        break;
    case 1:  // Join Server
        show_join_server_form(ged);
        break;
    case 2:  // Disconnect
        if (ged->collab_ctx.mode != COLLAB_MODE_STANDALONE) {
            if (ged->collab_ctx.mode == COLLAB_MODE_SERVER) {
                collab_stop_server(&ged->collab_ctx);
            } else {
                collab_disconnect(&ged->collab_ctx);
            }
        }
        break;
    case 3:  // Settings
        show_collab_settings_form(ged);
        break;
    }
}
```

### Settings Form

```c
void show_start_server_form(Ged *ged) {
    Qform form;
    Qform_item items[] = {
        {"Port", Qform_input_type_text_line, .text_value = "7777"},
        {NULL}
    };
    qform_create(&form, items);

    // Show modal form
    int result = qnav_push_form(&qnav_stack, &form);
    if (result == QNAV_COMMIT) {
        U16 port = (U16)atoi(items[0].text_value);
        if (collab_start_server(&ged->collab_ctx, port)) {
            // Success: show status
            show_message("Server started on port %u", port);
        } else {
            show_error("Failed to start server: %s", ged->collab_ctx.last_error);
        }
    }

    qform_destroy(&form);
}

void show_join_server_form(Ged *ged) {
    Qform form;
    Qform_item items[] = {
        {"Server", Qform_input_type_text_line, .text_value = "localhost"},
        {"Port", Qform_input_type_text_line, .text_value = "7777"},
        {"Your Name", Qform_input_type_text_line, .text_value = getenv("USER")},
        {NULL}
    };
    qform_create(&form, items);

    int result = qnav_push_form(&qnav_stack, &form);
    if (result == QNAV_COMMIT) {
        const char *server = items[0].text_value;
        U16 port = (U16)atoi(items[1].text_value);
        const char *name = items[2].text_value;

        strncpy(ged->collab_ctx.my_name, name, 31);
        ged->collab_ctx.my_name[31] = '\0';

        if (collab_connect_to_server(&ged->collab_ctx, server, port)) {
            show_message("Connecting to %s:%u...", server, port);
        } else {
            show_error("Failed to connect: %s", ged->collab_ctx.last_error);
        }
    }

    qform_destroy(&form);
}
```

### Event Loop Integration

**Main Event Loop Modifications (tui_main.c:3452):**

```c
event_loop:;
    // NEW: Process network events
    if (tui.ged.collab_ctx.mode != COLLAB_MODE_STANDALONE) {
        collab_update(&tui.ged.collab_ctx, &tui.ged);

        // Update cursor timeouts
        if (tui.ged.collab_ctx.mode == COLLAB_MODE_SERVER ||
            tui.ged.collab_ctx.state == COLLAB_STATE_ACTIVE) {
            collab_update_cursor_timeouts(&tui.ged.remote_cursors);
        }
    }

    int key = wgetch(stdscr);

    if (key == ERR) {
        // ... existing no-input handling ...

        // NEW: Send periodic hash check (client only)
        if (tui.ged.collab_ctx.state == COLLAB_STATE_ACTIVE &&
            tui.ged.collab_ctx.mode == COLLAB_MODE_CLIENT) {
            U64 now_ms = get_time_ms();
            if (now_ms - tui.ged.collab_ctx.last_hash_time_ms > 5000) {  // Every 5s
                collab_send_hash(&tui.ged.collab_ctx, &tui.ged.field);
                tui.ged.collab_ctx.last_hash_time_ms = now_ms;
            }
        }

        // ... existing simulation/render logic ...
    }
    // ... rest of event loop ...
```

### Edit Operation Hooks

**Hook All Write Operations:**

```c
// In ged_input_character() - single character input:
void ged_input_character(Ged *a, char c) {
    // Existing logic to write to grid
    gbuffer_poke(a->field.buffer, a->field.height, a->field.width,
                 a->ged_cursor.y, a->ged_cursor.x, c);

    // NEW: Broadcast write
    if (collab_is_active(&a->collab_ctx)) {
        Msg_grid_write msg = {
            .client_id = a->collab_ctx.my_client_id,
            .y = a->ged_cursor.y,
            .x = a->ged_cursor.x,
            .glyph = c,
        };
        collab_send_message(&a->collab_ctx, MSG_GRID_WRITE, &msg, sizeof(msg));
    }

    // ... rest of function ...
}

// In ged_fill_selection() - fill rectangle:
void ged_fill_selection(Ged *a, Glyph fill_char) {
    Usz y0 = a->ged_cursor.y;
    Usz x0 = a->ged_cursor.x;
    Usz h = a->ged_cursor.h;
    Usz w = a->ged_cursor.w;

    // Existing fill logic
    gbuffer_fill_subrect(a->field.buffer, a->field.height, a->field.width,
                         y0, x0, h, w, fill_char);

    // NEW: Batch broadcast
    if (collab_is_active(&a->collab_ctx)) {
        Collab_write_batch batch;
        batch.client_id = a->collab_ctx.my_client_id;
        batch.num_writes = 0;

        for (Usz y = 0; y < h; y++) {
            for (Usz x = 0; x < w; x++) {
                batch.writes[batch.num_writes++] = (Write_tuple){
                    .y = y0 + y,
                    .x = x0 + x,
                    .glyph = fill_char,
                };
            }
        }

        collab_send_write_batch(&a->collab_ctx, &batch);
    }

    // ... rest of function ...
}

// Similar hooks needed in:
// - ged_copy_paste()
// - ged_input_cmd() for cuts/clears
// - Any other grid-modifying operation
```

### Status Bar Updates

**Show Collaboration Status:**

```c
void draw_hud(/* ... */) {
    // Existing HUD drawing...

    // NEW: Show collaboration status
    if (ged->collab_ctx.mode == COLLAB_MODE_SERVER) {
        wprintw(win, " | SERVER | Clients: %u/10", ged->remote_cursors.num_active);
    } else if (ged->collab_ctx.mode == COLLAB_MODE_CLIENT) {
        const char *state_str = collab_state_to_string(ged->collab_ctx.state);
        wprintw(win, " | CLIENT | %s", state_str);
    }

    // ... rest of HUD ...
}
```

**Show Connected Users:**

```c
void draw_client_list(WINDOW *win, Remote_cursor_list *rcl) {
    int x = 0;
    for (int i = 0; i < 10; i++) {
        if (rcl->cursors[i].active) {
            attr_t color_attr = COLOR_PAIR(rcl->cursors[i].color_index);
            wattron(win, color_attr);
            mvwprintw(win, 1, x, "%s", rcl->cursors[i].name);
            wattroff(win, color_attr);
            x += strlen(rcl->cursors[i].name) + 2;
        }
    }
}
```

---

## Server-Specific Logic

### Server State

```c
typedef struct {
    bool is_running;
    U8 num_clients;

    struct {
        int fd;                      // Client socket FD
        U8 id;                       // Assigned ID (1-10)
        U8 color_index;              // Assigned color
        char name[32];               // Client name
        struct sockaddr_storage addr; // Client address
        U64 last_activity_ms;        // For timeout detection
        bool is_active;
    } clients[10];

    U8 next_client_id;               // ID to assign to next client
    U8 available_colors[10];         // Pool of available colors
    U8 num_available_colors;
} Collab_server;
```

### Server Operations

#### Accept New Client

```c
void collab_server_accept_client(Collab_server *srv, Collab_context *ctx, Field *grid) {
    struct sockaddr_storage addr;
    socklen_t addrlen = sizeof(addr);
    int client_fd = accept(ctx->listen_fd, (struct sockaddr *)&addr, &addrlen);

    if (client_fd < 0) {
        // Error handling
        return;
    }

    // Set non-blocking
    fcntl(client_fd, F_SETFL, O_NONBLOCK);

    // Find free slot
    int slot = -1;
    for (int i = 0; i < 10; i++) {
        if (!srv->clients[i].is_active) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        // Server full - send rejection
        Msg_client_reject reject = {.reason = "Server full"};
        send_message(client_fd, MSG_CLIENT_REJECT, &reject, sizeof(reject));
        close(client_fd);
        return;
    }

    // Wait for MSG_CLIENT_JOIN (handled in collab_update)
    srv->clients[slot].fd = client_fd;
    srv->clients[slot].is_active = true;
    // ID assigned after receiving MSG_CLIENT_JOIN
}
```

#### Handle Client Join

```c
void collab_server_handle_join(Collab_server *srv, int slot, const Msg_client_join *msg, Field *grid) {
    // Assign ID and color
    U8 id = srv->next_client_id++;
    U8 color = srv->available_colors[--srv->num_available_colors];

    srv->clients[slot].id = id;
    srv->clients[slot].color_index = color;
    strncpy(srv->clients[slot].name, msg->client_name, 31);
    srv->clients[slot].name[31] = '\0';
    srv->clients[slot].last_activity_ms = get_time_ms();
    srv->num_clients++;

    // Send acceptance
    Msg_client_accept accept = {
        .client_id = id,
        .color_index = color,
        .num_clients = srv->num_clients,
    };
    send_message(srv->clients[slot].fd, MSG_CLIENT_ACCEPT, &accept, sizeof(accept));

    // Send full grid sync
    collab_send_full_grid(srv->clients[slot].fd, grid);

    // Broadcast to other clients: new user joined
    collab_server_broadcast_join_notification(srv, id, msg->client_name, color);
}
```

#### Broadcast to All Clients

```c
void collab_server_broadcast(Collab_server *srv, Collab_msg_type type,
                              const void *payload, Usz payload_len) {
    for (int i = 0; i < 10; i++) {
        if (srv->clients[i].is_active) {
            send_message(srv->clients[i].fd, type, payload, payload_len);
        }
    }
}

void collab_server_broadcast_except(Collab_server *srv, U8 except_id,
                                     Collab_msg_type type,
                                     const void *payload, Usz payload_len) {
    for (int i = 0; i < 10; i++) {
        if (srv->clients[i].is_active && srv->clients[i].id != except_id) {
            send_message(srv->clients[i].fd, type, payload, payload_len);
        }
    }
}
```

#### Handle Client Disconnect

```c
void collab_server_remove_client(Collab_server *srv, int slot) {
    if (!srv->clients[slot].is_active) return;

    U8 id = srv->clients[slot].id;
    U8 color = srv->clients[slot].color_index;
    char name[32];
    strncpy(name, srv->clients[slot].name, 32);

    // Close socket
    close(srv->clients[slot].fd);

    // Return color to pool
    srv->available_colors[srv->num_available_colors++] = color;

    // Mark inactive
    srv->clients[slot].is_active = false;
    srv->num_clients--;

    // Broadcast to other clients
    collab_server_broadcast_leave_notification(srv, id, name);
}
```

### Playback Synchronization

**Only Server Controls Playback:**

```c
// Modify ged_toggle_playing() to broadcast:
void ged_toggle_playing(Ged *a) {
    a->is_playing = !a->is_playing;

    // Reset timing accumulator
    a->accum_secs = 0.0;

    // NEW: Broadcast to clients
    if (a->collab_ctx.mode == COLLAB_MODE_SERVER) {
        Msg_play_state msg = {
            .is_playing = a->is_playing,
            .tick_num = a->tick_num,
            .bpm = a->bpm,
        };
        collab_server_broadcast(&a->collab_server, MSG_PLAY_STATE, &msg, sizeof(msg));
    }
}

// Client receives:
void collab_client_handle_play_state(Ged *a, const Msg_play_state *msg) {
    a->is_playing = msg->is_playing;
    a->tick_num = msg->tick_num;
    a->bpm = msg->bpm;
    a->accum_secs = 0.0;  // Sync timing
}
```

**Disable Client Playback Controls:**

```c
// In event loop, when processing spacebar:
if (key == ' ') {
    if (tui.ged.collab_ctx.mode == COLLAB_MODE_CLIENT) {
        // Show message: "Playback controlled by server"
        show_message("Playback is controlled by the server");
    } else {
        ged_toggle_playing(&tui.ged);
    }
}

// Similarly disable BPM changes on clients
```

---

## Technical Implementation Details

### File Structure

**New Files:**

```
collaboration/
├── collab_net.c/.h       # Network transport layer
├── collab_protocol.c/.h  # Message serialization/deserialization
├── collab_sync.c/.h      # Grid synchronization logic
├── collab_server.c/.h    # Server-specific logic
├── collab_client.c/.h    # Client-specific logic
└── collab_util.c/.h      # Utilities (hash, time, etc.)

thirdparty/
└── xxhash.h/.c           # Hash function (single-header library)
```

**Module Responsibilities:**

| Module | Purpose |
|--------|---------|
| `collab_net` | TCP socket creation, non-blocking I/O, send/recv primitives |
| `collab_protocol` | Message pack/unpack, framing, validation |
| `collab_sync` | Grid diff/apply, hash computation, resync logic |
| `collab_server` | Server state, client management, broadcasting |
| `collab_client` | Client state, server connection, local updates |
| `collab_util` | Time functions, logging, error handling |

### Build System Updates

**Modify `tool` Script:**

```bash
# Add to compilation units:
collaboration/collab_net.c
collaboration/collab_protocol.c
collaboration/collab_sync.c
collaboration/collab_server.c
collaboration/collab_client.c
collaboration/collab_util.c
thirdparty/xxhash.c
```

**Dependencies:**
- No new external dependencies
- Use POSIX sockets (already available on Linux/macOS)
- xxHash is single-file, public domain

### Non-Blocking I/O

**Socket Setup:**

```c
// Server socket
int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
int flags = fcntl(listen_fd, F_GETFL, 0);
fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK);

// Client socket
int client_fd = socket(AF_INET, SOCK_STREAM, 0);
flags = fcntl(client_fd, F_GETFL, 0);
fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
connect(client_fd, ...);  // Returns EINPROGRESS for non-blocking
```

**Polling with `select()`:**

```c
void collab_update(Collab_context *ctx, Ged *ged) {
    fd_set readfds, writefds;
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);

    int max_fd = -1;

    // Add listen socket (server only)
    if (ctx->mode == COLLAB_MODE_SERVER && ctx->listen_fd >= 0) {
        FD_SET(ctx->listen_fd, &readfds);
        max_fd = ctx->listen_fd;
    }

    // Add client sockets (server only)
    if (ctx->mode == COLLAB_MODE_SERVER) {
        for (int i = 0; i < 10; i++) {
            if (ctx->client_fds[i] >= 0) {
                FD_SET(ctx->client_fds[i], &readfds);
                if (ctx->client_fds[i] > max_fd) max_fd = ctx->client_fds[i];
            }
        }
    }

    // Add server socket (client only)
    if (ctx->mode == COLLAB_MODE_CLIENT && ctx->server_fd >= 0) {
        FD_SET(ctx->server_fd, &readfds);
        max_fd = ctx->server_fd;
    }

    // Non-blocking poll (timeout = 0)
    struct timeval tv = {0, 0};
    int ready = select(max_fd + 1, &readfds, NULL, NULL, &tv);

    if (ready > 0) {
        // Process ready sockets
        if (ctx->mode == COLLAB_MODE_SERVER) {
            collab_server_process_sockets(ctx, &readfds);
        } else {
            collab_client_process_socket(ctx, &readfds);
        }
    }
}
```

**Alternative: `poll()`**

Could use `poll()` instead of `select()` for cleaner code:

```c
struct pollfd fds[11];  // 1 listen + 10 clients, or 1 server connection
int nfds = 0;

// Setup fds array...
poll(fds, nfds, 0);  // Non-blocking poll
```

### Message Framing Implementation

**Handling Partial Reads:**

```c
typedef struct {
    U8 buffer[65536];
    Usz offset;        // Current position in buffer
    Usz expected_len;  // Total message length (from header)
    bool has_header;   // Have we read the 8-byte header?
} Msg_buffer;

// Per-connection receive buffer
Msg_buffer recv_buf;

// Receive data
int collab_recv_messages(int fd, Msg_buffer *buf,
                         void (*handle_msg)(Collab_msg_type, const void *, Usz)) {
    while (1) {
        // Try to read more data
        ssize_t n = recv(fd, buf->buffer + buf->offset,
                         sizeof(buf->buffer) - buf->offset, 0);

        if (n <= 0) {
            if (n == 0) return -1;  // Connection closed
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;  // No more data
            return -1;  // Error
        }

        buf->offset += n;

        // Process complete messages
        while (1) {
            if (!buf->has_header) {
                if (buf->offset < sizeof(Collab_msg_header)) break;  // Need more

                Collab_msg_header *hdr = (Collab_msg_header *)buf->buffer;

                // Validate magic
                if (hdr->magic != 0x4F524341) {
                    // Invalid magic - disconnect
                    return -1;
                }

                buf->expected_len = sizeof(Collab_msg_header) + hdr->length;
                buf->has_header = true;
            }

            if (buf->offset < buf->expected_len) break;  // Need more data

            // We have a complete message
            Collab_msg_header *hdr = (Collab_msg_header *)buf->buffer;
            const void *payload = buf->buffer + sizeof(Collab_msg_header);

            // Handle message
            handle_msg((Collab_msg_type)hdr->msg_type, payload, hdr->length);

            // Shift remaining data
            Usz remaining = buf->offset - buf->expected_len;
            if (remaining > 0) {
                memmove(buf->buffer, buf->buffer + buf->expected_len, remaining);
            }
            buf->offset = remaining;
            buf->has_header = false;
        }
    }
}
```

### Error Handling Strategy

**Principle:** Network errors should be non-fatal to the application

**Error Categories:**

1. **Connection Errors** (client can't connect, server can't bind)
   - Show error message to user
   - Return to standalone mode
   - Allow retry

2. **Transmission Errors** (send/recv fails)
   - Log error
   - Disconnect client
   - Notify other clients

3. **Protocol Errors** (invalid message, bad magic)
   - Log warning
   - Skip message
   - Continue processing (don't disconnect)

4. **Desync Errors** (hash mismatch)
   - Trigger automatic resync
   - Don't notify user (transparent recovery)

**Logging:**

```c
void collab_log(const char *fmt, ...) {
    // Write to ~/.config/orca/collab.log
    FILE *f = fopen(log_path, "a");
    if (!f) return;

    time_t now = time(NULL);
    fprintf(f, "[%s] ", ctime(&now));

    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);

    fprintf(f, "\n");
    fclose(f);
}
```

---

## User Experience Flow

### Server Workflow

1. User launches Orca TUI
2. Opens menu (Ctrl+G or F1) → "Collaboration" → "Start Server"
3. Form appears:
   ```
   ┌─ Start Server ─────────┐
   │ Port: [7777_______]    │
   │                        │
   │  [OK]      [Cancel]    │
   └────────────────────────┘
   ```
4. User presses Enter (accept default) or types custom port
5. Server starts, status bar shows:
   ```
   SERVER | Clients: 0/10 | example.orca | 64x32 @ 15:23 | BPM: 120
   ```
6. User continues editing normally
7. When client joins, notification appears:
   ```
   Alice (🟢) joined
   ```
8. Alice's cursor appears in green
9. User can see Alice's edits in real-time

### Client Workflow

1. User launches Orca TUI
2. Opens menu → "Collaboration" → "Join Server"
3. Form appears:
   ```
   ┌─ Join Server ──────────────────┐
   │ Server: [localhost________]    │
   │ Port:   [7777_____________]    │
   │ Name:   [bob______________]    │
   │                                │
   │  [Connect]      [Cancel]       │
   └────────────────────────────────┘
   ```
4. User enters server IP/hostname and presses Enter
5. Status shows: "Connecting to 192.168.1.10:7777..."
6. Grid clears and receives full sync
7. Status bar shows:
   ```
   CLIENT | Server: 192.168.1.10:7777 | example.orca | 64x32
   ```
8. User sees server's cursor (host color) and other clients
9. Playback controls (spacebar) are disabled
10. User can edit, sees others' edits

### Visual Indicators

**Grid with Multiple Cursors:**

```
┌─ ORCA ────────────────────────────────────────────────────────┐
│ SERVER | Clients: 3/10 | example.orca | 64x32 @ 15:23         │
│ 🟢 Alice  🟡 Bob  🔵 Carol                                     │
├───────────────────────────────────────────────────────────────┤
│ . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . │
│ . . . D 4 . . . . . . . . . . . . . . . . . . . . . . . . . . │ ← Alice (green)
│ . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . │
│ . . . . . . . . . . 3 C . . . . . . . . . . . . . . . . . . . │ ← Bob (yellow)
│ . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . │
│ . . . . . . . . . . . . . . . . . . . R . . . . . . . . . . . │ ← Carol (cyan)
│ . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . │
```

**Cursor Styles:**
- **Local cursor**: Bold + reverse video + assigned color + `@` (playing) or `~` (paused)
- **Remote cursors**: Reverse video + assigned color + actual glyph or `·` on empty

**Selection Highlighting:**
- Rectangle outline in cursor color
- Semi-transparent overlay (if terminal supports)

### Notification System

**Events to Display:**

| Event | Message | Duration |
|-------|---------|----------|
| Client join | "Alice (🟢) joined" | 3s |
| Client leave | "Bob (🟡) disconnected" | 3s |
| Connection lost | "Connection lost, reconnecting..." | Persistent |
| Resync | "Resyncing grid..." | Until complete |
| Server stop | "Server stopped" | Persistent |
| Client kicked | "Disconnected by server" | Persistent |

**Implementation:**

```c
typedef struct {
    char text[256];
    U64 timestamp_ms;
    U32 duration_ms;
    bool persistent;
} Notification;

Notification active_notification;

void show_notification(const char *text, U32 duration_ms) {
    strncpy(active_notification.text, text, 255);
    active_notification.timestamp_ms = get_time_ms();
    active_notification.duration_ms = duration_ms;
    active_notification.persistent = (duration_ms == 0);
}

void draw_notification(WINDOW *win) {
    if (active_notification.text[0] == '\0') return;

    U64 now = get_time_ms();
    if (!active_notification.persistent &&
        now - active_notification.timestamp_ms > active_notification.duration_ms) {
        active_notification.text[0] = '\0';
        return;
    }

    // Draw at bottom of screen
    int screen_h, screen_w;
    getmaxyx(win, screen_h, screen_w);

    attr_t attrs = A_REVERSE | COLOR_PAIR(C_yellow);
    wattron(win, attrs);
    mvwprintw(win, screen_h - 1, 0, "%s", active_notification.text);
    wattroff(win, attrs);
}
```

---

## Configuration Persistence

### Configuration File Format

**Location:** `~/.config/orca/orca.conf`

**New Section:**

```ini
[collaboration]
default_port=7777
default_server=localhost
default_name=YourName
auto_announce=true
hash_interval_ms=5000
cursor_throttle_ms=50
max_clients=10
```

### Configuration Structure

```c
typedef struct {
    U16 default_port;              // Default server port
    char default_server[256];      // Default server address
    char default_name[64];         // Default client name
    bool auto_announce;            // Show join/leave messages
    Usz hash_interval_ms;          // Hash check interval (client)
    Usz cursor_throttle_ms;        // Cursor update throttle
    U8 max_clients;                // Max clients (1-10)
} Collab_config;

// Add to Tui structure:
struct Tui {
    // ... existing fields ...
    Collab_config collab_config;
};
```

### Load/Save Functions

**Reuse Existing Config System (sysmisc.c):**

```c
// In load_orca_config():
void load_collab_config(Collab_config *cfg) {
    // Read from orca.conf
    cfg->default_port = read_config_u16("collaboration.default_port", 7777);
    read_config_string("collaboration.default_server", cfg->default_server, 256, "localhost");
    read_config_string("collaboration.default_name", cfg->default_name, 64, getenv("USER"));
    cfg->auto_announce = read_config_bool("collaboration.auto_announce", true);
    cfg->hash_interval_ms = read_config_usz("collaboration.hash_interval_ms", 5000);
    cfg->cursor_throttle_ms = read_config_usz("collaboration.cursor_throttle_ms", 50);
    cfg->max_clients = read_config_u8("collaboration.max_clients", 10);
}

// In save_orca_config():
void save_collab_config(const Collab_config *cfg) {
    write_config_u16("collaboration.default_port", cfg->default_port);
    write_config_string("collaboration.default_server", cfg->default_server);
    write_config_string("collaboration.default_name", cfg->default_name);
    write_config_bool("collaboration.auto_announce", cfg->auto_announce);
    write_config_usz("collaboration.hash_interval_ms", cfg->hash_interval_ms);
    write_config_usz("collaboration.cursor_throttle_ms", cfg->cursor_throttle_ms);
    write_config_u8("collaboration.max_clients", cfg->max_clients);
}
```

---

## Security Considerations

### Current Scope (MVP)

**Security Model:**

❌ **No authentication** - Anyone can connect
❌ **No encryption** - All data sent in plaintext
❌ **No authorization** - All clients have full write access
❌ **No input validation** - Trust client data (except bounds checking)
⚠️ **For LAN/trusted networks ONLY**

**Acceptable Use Cases:**
- Local network (home/studio LAN)
- VPN-connected peers
- Trusted collaborators only

**Unacceptable Use Cases:**
- Public internet exposure
- Untrusted users
- Sensitive/private grids

### Security Warnings

**Display Warning When Starting Server:**

```
┌─ Security Warning ─────────────────────────────────┐
│                                                    │
│  WARNING: Server is UNENCRYPTED and UNAUTHENTICATED│
│                                                    │
│  Only use on trusted networks (LAN/VPN)           │
│  Do NOT expose to public internet                 │
│                                                    │
│  [I Understand]      [Cancel]                     │
└────────────────────────────────────────────────────┘
```

### Future Security Enhancements

**Phase 2 (Future Work):**

1. **TLS/SSL Encryption**
   - Use OpenSSL or mbedTLS
   - Self-signed certificates for simplicity
   - Optional for LAN, required for WAN

2. **Password Authentication**
   - Server-side password
   - Hashed with argon2 or bcrypt
   - Optional (trust mode vs password mode)

3. **Permission System**
   - Read-only mode for spectators
   - Protected cells (lock regions)
   - Admin role (server owner)

4. **Rate Limiting**
   - Max writes per second per client
   - Prevent spam/DoS

5. **Input Validation**
   - Validate all coordinates
   - Sanitize client names (prevent injection)
   - Check grid dimensions

---

## Performance Considerations

### Benchmarks (Estimated)

**Network Latency (LAN):**

| Operation | Size (bytes) | Latency (ms) | Notes |
|-----------|--------------|--------------|-------|
| Single write | 14 | 1-2 | Round-trip: client → server → broadcast |
| Batch write (100 cells) | 511 | 5-10 | Paste operation |
| Full sync (64×32) | 2060 | 2-5 | Initial join or resync |
| Cursor update | 17 | 1 | No acknowledgment needed |
| Hash submission | 17 | 1 | Every 5 seconds |

**Computation:**

| Operation | Time (µs) | Notes |
|-----------|-----------|-------|
| xxHash64 (2KB grid) | 0.2 | ~10 GB/s throughput |
| Message serialize | 0.1 | memcpy + header write |
| Message deserialize | 0.1 | memcpy + validation |
| Cursor render (10 cursors) | 10 | NCurses draw calls |

**Memory Overhead:**

| Component | Size (bytes) | Notes |
|-----------|--------------|-------|
| Collab_context | ~70 KB | Buffers + state |
| Remote_cursor_list | ~1 KB | 10 cursors × 64 bytes |
| Per-client buffer (server) | 65 KB × 10 | Receive buffers |

**Total Memory:** ~720 KB (negligible compared to grid + undo history)

### Optimizations

#### 1. Write Batching

**Problem:** Many individual writes (paste, fill) flood network

**Solution:** Collect writes in time window, send batch

```c
typedef struct {
    Write_tuple writes[1024];
    Usz num_writes;
    U64 batch_start_ms;
} Write_batch_accumulator;

// In edit operations:
void accumulate_write(Write_batch_accumulator *acc, Usz y, Usz x, Glyph g) {
    acc->writes[acc->num_writes++] = (Write_tuple){y, x, g};

    // Send batch if:
    // - Buffer full (1024 writes)
    // - 16ms elapsed (1 frame at 60 FPS)
    U64 now = get_time_ms();
    if (acc->num_writes >= 1024 || (now - acc->batch_start_ms) > 16) {
        flush_write_batch(acc);
    }
}
```

**Savings:** 1024 writes batched = 5 KB vs 14 KB unbatched (64% reduction)

#### 2. Delta Compression

**Problem:** Large pastes send redundant data (filled regions)

**Solution:** Run-length encode identical glyphs

```c
// Instead of sending 100 identical '.' glyphs:
// Send: {y, x, w, h, glyph}  (fill rectangle)
typedef struct {
    U16 y, x, w, h;
    Glyph glyph;
} Write_fill;
```

**Savings:** 100-cell fill = 9 bytes vs 500 bytes (98% reduction)

#### 3. Cursor Update Throttling

**Already Implemented:** Max 20 updates/second (50ms throttle)

**Rationale:**
- Humans can't move cursor > 20 cells/second meaningfully
- Reduces bandwidth by ~95% vs unlimited
- Still feels instant to users

#### 4. Hash Computation Caching

**Problem:** Computing hash every 5s wastes CPU if grid unchanged

**Solution:** Track dirty flag

```c
typedef struct {
    U64 cached_hash;
    bool is_dirty;
} Hash_cache;

U64 get_grid_hash(Field *field, Hash_cache *cache) {
    if (!cache->is_dirty) {
        return cache->cached_hash;
    }

    cache->cached_hash = xxHash64(field->buffer, field->width * field->height, 0);
    cache->is_dirty = false;
    return cache->cached_hash;
}

// Set dirty on any grid write
```

**Savings:** Avoid ~99% of hash computations (grid rarely changes every 5s)

#### 5. Lazy Full Sync

**Problem:** Sending full 64×32 grid to new client joining empty session wastes bandwidth

**Solution:** Check if grid is empty, send placeholder

```c
bool is_grid_empty(Field *field) {
    for (Usz i = 0; i < field->width * field->height; i++) {
        if (field->buffer[i] != '.') return false;
    }
    return true;
}

void send_initial_sync(int client_fd, Field *field) {
    if (is_grid_empty(field)) {
        // Send minimal empty marker
        Msg_grid_empty empty = {field->width, field->height};
        send_message(client_fd, MSG_GRID_EMPTY, &empty, sizeof(empty));
    } else {
        // Send full grid
        send_full_grid(client_fd, field);
    }
}
```

**Savings:** 2060 bytes → 12 bytes for empty grid (99.4% reduction)

---

## Testing Strategy

### Unit Tests

**Test Coverage:**

```c
// test_collab_protocol.c
void test_message_header_serialization();
void test_message_payload_serialization();
void test_partial_message_assembly();
void test_invalid_magic_rejection();
void test_oversized_message_rejection();

// test_collab_sync.c
void test_grid_hash_computation();
void test_grid_hash_consistency();  // Same grid → same hash
void test_grid_full_sync_encode_decode();
void test_write_application();
void test_batch_write_application();

// test_collab_net.c
void test_socket_creation();
void test_nonblocking_send_recv();
void test_connection_timeout();
void test_disconnection_detection();

// test_collab_cursor.c
void test_cursor_serialization();
void test_remote_cursor_update();
void test_cursor_timeout();
```

**Test Runner:**

```bash
#!/bin/bash
# tests/run_collab_tests.sh

gcc -o test_collab_protocol tests/test_collab_protocol.c collaboration/*.c -I. -lncurses
./test_collab_protocol

gcc -o test_collab_sync tests/test_collab_sync.c collaboration/*.c -I. -lncurses
./test_collab_sync

# ... etc
```

### Integration Tests

**Automated Scenario Testing:**

```bash
#!/bin/bash
# tests/test_collab_integration.sh

# Start server in background
./orca --headless --server --port 7777 --load empty.orca &
SERVER_PID=$!
sleep 1

# Start 3 clients
./orca --headless --client --server localhost:7777 --name Alice &
CLIENT1_PID=$!
./orca --headless --client --server localhost:7777 --name Bob &
CLIENT2_PID=$!
./orca --headless --client --server localhost:7777 --name Carol &
CLIENT3_PID=$!

sleep 2

# Send test commands to clients
echo "write 10 20 A" | nc localhost 7778  # Alice writes 'A' at (10, 20)
sleep 0.5

echo "verify 10 20 A" | nc localhost 7779  # Bob verifies he sees 'A'
echo "verify 10 20 A" | nc localhost 7780  # Carol verifies

# Kill client 1
kill $CLIENT1_PID
sleep 1

# Verify server still responsive
echo "write 5 5 B" | nc localhost 7779

# Cleanup
kill $SERVER_PID $CLIENT2_PID $CLIENT3_PID
```

**Test Scenarios:**

1. **Basic Sync Test**
   - Server starts with grid containing "HELLO"
   - Client joins
   - Verify client receives "HELLO"

2. **Write Propagation Test**
   - Server + 2 clients
   - Client 1 writes "ORCA"
   - Verify server + client 2 see "ORCA"

3. **Batch Write Test**
   - Server + client
   - Client pastes 10×10 block
   - Verify server receives all 100 cells

4. **Hash Verification Test**
   - Server + client, synced
   - Artificially corrupt client grid
   - Wait 5 seconds
   - Verify resync triggered

5. **Disconnection Test**
   - Server + 3 clients
   - Kill client 2 (ungraceful disconnect)
   - Verify server removes client 2
   - Verify clients 1 and 3 still synced

6. **Capacity Test**
   - Server starts
   - Connect 10 clients (max capacity)
   - Attempt 11th client
   - Verify rejection

### Manual Tests

**Test Plan:**

1. **Latency Test**
   - Setup: Server on machine A, client on machine B (different LAN hosts)
   - Action: Client types rapidly
   - Measure: Time from keypress to server display
   - Target: < 50ms

2. **Throughput Test**
   - Setup: Server + 10 clients
   - Action: All clients paste 10×10 block simultaneously
   - Verify: All clients converge to same state
   - Measure: Time to convergence
   - Target: < 2 seconds

3. **Stability Test**
   - Setup: Server + 5 clients
   - Action: Collaborative editing for 30 minutes
   - Verify: No crashes, no desyncs, no memory leaks

4. **Resync Test**
   - Setup: Server + client, synced
   - Action: Use debugger to corrupt client grid
   - Wait: 5 seconds (hash interval)
   - Verify: Automatic resync, grids match

5. **Edge Case Test**
   - Grid resize during session
   - Client joins during playback
   - Rapid undo/redo
   - Copy/paste across clients
   - Large grid (256×256)

---

## Error Cases & Edge Scenarios

### Comprehensive Error Handling

| Scenario | Detection | Recovery | User Feedback |
|----------|-----------|----------|---------------|
| **Client joins during server playback** | Server state check | Send current tick + play state in sync | None (transparent) |
| **Client network lag (slow upload)** | Send buffer full | Disconnect after 1MB buffered | "Client disconnected (timeout)" |
| **Server crashes** | Client recv returns 0 | Show error, return to standalone | "Server disconnected" |
| **Client crashes** | Server no activity for 10s | Remove from client list, broadcast leave | "[Name] disconnected" |
| **Conflicting writes (same cell)** | Server timestamp ordering | Last-write-wins | None (expected behavior) |
| **Grid resize** | Server changes dimensions | Broadcast new size, all clients resize | Notification: "Grid resized to 128×64" |
| **Hash mismatch** | Periodic hash check | Server sends MSG_RESYNC_REQUEST | Notification: "Resyncing..." |
| **Invalid message (bad magic)** | Protocol validation | Log warning, skip message | None |
| **Oversized message (> 64KB)** | Length check | Disconnect client | "Client disconnected (protocol error)" |
| **Socket error (EPIPE, etc.)** | Send/recv return code | Disconnect, log error | "[Name] connection lost" |
| **Port already in use** | bind() fails | Show error, don't start server | "Port 7777 in use" |
| **Connection refused** | connect() fails | Show error, return to standalone | "Cannot connect to server" |
| **Clipboard paste (large)** | Size check | Batch into multiple messages | None (automatic batching) |
| **Undo operation** | User command | Apply locally, send resulting writes | None (per-client undo) |
| **BPM change (server)** | Server command | Broadcast MSG_BPM_UPDATE | None (automatic sync) |
| **BPM change (client)** | User command | Ignore, show warning | "BPM controlled by server" |
| **Playback toggle (client)** | User command | Ignore, show warning | "Playback controlled by server" |
| **File save (server)** | User command | Save normally, clients unaffected | None |
| **File save (client)** | User command | Save local copy (may diverge) | Warning: "You are editing a shared grid" |
| **File load (server)** | User command | Load, broadcast full sync to all | Notification: "Grid reloaded" |
| **File load (client)** | User command | Disconnect, load file | "Disconnected from server" |
| **New grid (server)** | User command | Clear, broadcast full sync | "Grid cleared" |
| **Client joins full server** | Connection count check | Send MSG_CLIENT_REJECT | "Server is full (10/10)" |

---

## Undo/Redo Behavior in Collaborative Mode

### Design Decision

**Per-Client Undo History:**
- Each participant maintains their own undo stack
- Undo only affects local state, but broadcasts writes as if typed
- Server maintains its own undo history independently

**Rationale:**
- Shared undo is complex (requires Operational Transform or CRDT)
- Users expect undo to revert *their* changes
- MVP simplicity

### Behavior Example

**Scenario:**
1. Alice writes "HELLO" at (10, 10)
2. Bob writes "WORLD" at (10, 20)
3. Alice presses Undo
4. What happens?

**Implementation:**
1. Alice's local undo reverts to state before "HELLO"
2. Alice's grid shows empty cells at (10, 10-14)
3. Alice's client sends 5 × MSG_GRID_WRITE with '.' (blank)
4. Server receives and broadcasts these writes
5. Bob sees "HELLO" disappear
6. **Result:** Alice's undo works, but Bob sees it as new writes

**Limitation:**
- If Bob wrote "WORLD" at (10, 10) (overlapping with Alice's "HELLO"), Alice's undo might delete Bob's work

**Mitigation:**
- Document in user guide: "Undo in collaborative mode has limitations"
- Consider future enhancement: collaborative undo (out of scope for MVP)

**Alternative Considered:**
- Disable undo entirely in collaborative mode
- **Rejected:** Too restrictive, users expect undo

---

## Implementation Roadmap

### Phase 1: Core Networking (2-3 weeks)

**Goals:**
- Establish TCP client/server connections
- Implement message framing and serialization
- Non-blocking I/O with `select()`

**Tasks:**
- [ ] Create `collaboration/` directory structure
- [ ] Implement `collab_net.c`: TCP socket primitives
- [ ] Implement `collab_protocol.c`: Message pack/unpack
- [ ] Implement `collab_util.c`: Time, logging, utilities
- [ ] Add xxHash library to `thirdparty/`
- [ ] Write unit tests for networking and protocol
- [ ] Test basic client-server connection (no grid sync yet)

**Deliverable:** Client can connect to server, exchange heartbeat messages

---

### Phase 2: Grid Synchronization (2-3 weeks)

**Goals:**
- Full grid sync on client join
- Incremental write broadcasting
- Hash-based verification

**Tasks:**
- [ ] Implement `collab_sync.c`: Grid hash, full sync, write apply
- [ ] Add `MSG_GRID_FULL_SYNC` handling
- [ ] Add `MSG_GRID_WRITE` and `MSG_GRID_WRITE_BATCH` handling
- [ ] Add `MSG_CLIENT_HASH` and `MSG_RESYNC_REQUEST` handling
- [ ] Hook grid write operations in `tui_main.c`
- [ ] Implement server broadcast logic
- [ ] Write unit tests for sync logic
- [ ] Integration test: 2 clients editing simultaneously

**Deliverable:** Server + clients stay synchronized, edits propagate

---

### Phase 3: Multi-Cursor System (1-2 weeks)

**Goals:**
- Render remote cursors with unique colors
- Broadcast cursor updates
- Handle cursor timeouts

**Tasks:**
- [ ] Add `Remote_cursor` and `Remote_cursor_list` to `Ged`
- [ ] Extend NCurses color palette (10 client colors)
- [ ] Modify `draw_grid_cursor()` to draw all cursors
- [ ] Implement `MSG_CURSOR_UPDATE` handling
- [ ] Hook cursor movement in `ged_cursor_move_relative()`
- [ ] Add cursor update throttling (50ms)
- [ ] Add cursor timeout logic (10s inactivity)
- [ ] Test with 5 clients moving cursors

**Deliverable:** All users see each other's cursors in real-time

---

### Phase 4: TUI Integration (1-2 weeks)

**Goals:**
- Add collaboration menu
- Implement server/client forms
- Show status in HUD
- Display notifications

**Tasks:**
- [ ] Add "Collaboration" menu item
- [ ] Create "Start Server" form
- [ ] Create "Join Server" form
- [ ] Add collaboration status to HUD
- [ ] Implement client list display (colored names)
- [ ] Add notification system (join/leave messages)
- [ ] Integrate `collab_update()` into main event loop
- [ ] Test full workflow: server start → client join → edit → disconnect

**Deliverable:** Complete UI for collaboration features

---

### Phase 5: Playback Synchronization (1 week)

**Goals:**
- Server controls playback
- Clients receive play state
- BPM and tick sync

**Tasks:**
- [ ] Add `MSG_PLAY_STATE`, `MSG_BPM_UPDATE`, `MSG_TICK_SYNC`
- [ ] Hook `ged_toggle_playing()` to broadcast state
- [ ] Disable playback controls on clients
- [ ] Sync BPM changes from server
- [ ] Sync tick counter on join
- [ ] Test: server plays, clients follow

**Deliverable:** Synchronized playback across all clients

---

### Phase 6: Polish & Testing (1 week)

**Goals:**
- Configuration persistence
- Error handling polish
- Documentation
- Final testing

**Tasks:**
- [ ] Add collaboration settings to `orca.conf`
- [ ] Implement load/save config
- [ ] Add comprehensive error messages
- [ ] Write `COLLABORATION.md` user guide
- [ ] Run integration test suite
- [ ] Manual testing: latency, throughput, stability
- [ ] Fix bugs found during testing
- [ ] Code review and cleanup

**Deliverable:** Production-ready MVP

---

### Total Timeline: 4-6 weeks

**Milestones:**
- **Week 2:** Network layer complete
- **Week 4:** Grid sync working
- **Week 5:** Multi-cursor rendering
- **Week 6:** Full UI integration
- **Week 7:** Playback sync + polish
- **Week 8:** Testing & documentation

---

## Out of Scope (Future Work)

**Not Included in MVP:**

### Security
- [ ] TLS/SSL encryption
- [ ] Password authentication
- [ ] Permission system (read-only, admin roles)
- [ ] Input sanitization (XSS, injection prevention)
- [ ] Rate limiting (DoS prevention)

### Networking
- [ ] NAT traversal (UPnP, STUN/TURN)
- [ ] WebSocket support (for web clients)
- [ ] UDP option (lower latency, less reliable)
- [ ] Peer-to-peer mode (no dedicated server)
- [ ] Relay servers (for WAN)

### Features
- [ ] Text chat
- [ ] Voice communication
- [ ] Shared undo/redo (CRDT-based)
- [ ] Version control (commit/branch)
- [ ] Session recording/playback
- [ ] Spectator mode (read-only observers)
- [ ] Grid region locking (protected areas)
- [ ] User presence indicators (away/typing)

### Integration
- [ ] Ableton Link integration (sync with DAWs)
- [ ] Web-based client
- [ ] Mobile client (iOS/Android)
- [ ] Plugin system (custom operators)
- [ ] Cloud hosting (dedicated servers)

### Performance
- [ ] Binary delta compression (reduce bandwidth)
- [ ] Predictive cursor smoothing (reduce perceived latency)
- [ ] P2P mesh network (reduce server load)
- [ ] Grid chunking (only sync visible regions)

---

## Appendix: Codebase Architecture Summary

**From Exploration Phase:**

### Key Files
- **tui_main.c** (3875 lines): Main TUI application
- **sim.c** (25626 lines): VM simulation engine
- **field.c/h**: Grid buffer management
- **term_util.c/h**: Terminal UI utilities (menus, forms)
- **osc_out.c/h**: OSC/UDP networking output

### Important Structures
```c
// Grid editor state
typedef struct {
    Field field;              // Main grid
    Ged_cursor ged_cursor;   // Cursor position
    Undo_history undo_hist;  // Undo stack
    Oevent_list oevent_list; // MIDI/OSC events
    // ... collaboration fields to add ...
} Ged;

// Grid storage
typedef struct {
    Glyph *buffer;  // Flat 2D array
    U16 width, height;
} Field;

// Cursor
typedef struct {
    Usz y, x, h, w;  // Position + selection
} Ged_cursor;
```

### Event Loop Flow
```
wgetch() → Menu processing → Input handling → Simulation step → Rendering
```

### Existing Network Capabilities
- **OSC output**: UDP-based, unidirectional
- **MIDI output**: Via PortMidi or OSC
- **No input networking**: Collaboration must add this

---

## Appendix: Protocol Wire Format Examples

### Example: Client Join

**Client → Server:**
```
Offset  Value           Description
------  -----           -----------
0-3     0x4F524341      Magic ("ORCA")
4-5     0x0021          Length: 33 bytes
6       0x01            MSG_CLIENT_JOIN
7       0x00            Flags (reserved)
8       0x01            Protocol version 1
9-40    "Alice\0..."    Client name (32 bytes, null-padded)
```

**Server → Client:**
```
Offset  Value           Description
------  -----           -----------
0-3     0x4F524341      Magic
4-5     0x0004          Length: 4 bytes
6       0x02            MSG_CLIENT_ACCEPT
7       0x00            Flags
8       0x03            Client ID: 3
9       0x02            Color index: 2 (green)
10      0x03            Num clients: 3
11      0x00            Reserved
```

### Example: Grid Write

**Client → Server:**
```
Offset  Value           Description
------  -----           -----------
0-3     0x4F524341      Magic
4-5     0x0005          Length: 5 bytes
6       0x11            MSG_GRID_WRITE
7       0x00            Flags
8       0x03            Client ID: 3
9-10    0x000A          Y: 10
11-12   0x0014          X: 20
13      'A'             Glyph: 'A'
```

### Example: Full Grid Sync

**Server → Client (64×32 grid):**
```
Offset  Value           Description
------  -----           -----------
0-3     0x4F524341      Magic
4-5     0x0804          Length: 2052 bytes (12 + 2048)
6       0x10            MSG_GRID_FULL_SYNC
7       0x00            Flags
8-9     0x0040          Width: 64
10-11   0x0020          Height: 32
12-19   0x000...096     Tick num: 150
20      0x01            Is playing: true
21-22   0x0078          BPM: 120
23-2070 [grid data]     2048 bytes of glyphs (row-major)
```

---

## Appendix: References

### Libraries
- **xxHash**: https://github.com/Cyan4973/xxHash (hash function)
- **NCurses**: https://invisible-island.net/ncurses/ (TUI library)

### Protocols
- **OSC (Open Sound Control)**: http://opensoundcontrol.org/
- **MIDI**: https://www.midi.org/specifications

### Related Work
- **Orca**: https://github.com/hundredrabbits/Orca (original JavaScript version)
- **Teletype**: https://monome.org/docs/teletype/ (similar live coding environment)

### Concepts
- **CRDT (Conflict-free Replicated Data Type)**: https://crdt.tech/
- **Operational Transform**: https://en.wikipedia.org/wiki/Operational_transformation
- **NAT Traversal**: https://en.wikipedia.org/wiki/NAT_traversal

---

**End of Document**
