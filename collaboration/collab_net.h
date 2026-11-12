// collab_net.h - TCP socket primitives for Orca collaboration
// Non-blocking I/O with select()

#pragma once
#include "../base.h"
#include "collab_util.h"
#include "collab_protocol.h"

// Default port
#define COLLAB_DEFAULT_PORT 7777

// Maximum clients
#define COLLAB_MAX_CLIENTS 10
// Client ids are 1..COLLAB_MAX_CLIENTS and 0 means "empty" in cursor lists,
// so the server's own cursor travels under a reserved id.
#define COLLAB_SERVER_CURSOR_ID 0xFF

// Connection timeout in milliseconds
#define COLLAB_CONNECT_TIMEOUT_MS 5000
#define COLLAB_HEARTBEAT_INTERVAL_MS 2000
#define COLLAB_CLIENT_TIMEOUT_MS 10000

// Socket result codes
typedef enum {
    COLLAB_SOCK_OK = 0,
    COLLAB_SOCK_WOULD_BLOCK = 1,   // Non-blocking operation would block
    COLLAB_SOCK_CLOSED = 2,        // Connection closed by peer
    COLLAB_SOCK_ERROR = -1,        // Error occurred
} Collab_sock_result;

// Connection state
typedef enum {
    COLLAB_CONN_DISCONNECTED = 0,
    COLLAB_CONN_CONNECTING,        // Non-blocking connect in progress
    COLLAB_CONN_CONNECTED,         // TCP connected
} Collab_conn_state;

// Connection structure (for both client connections and server's view of clients)
typedef struct {
    int fd;                        // Socket file descriptor (-1 if not connected)
    Collab_conn_state state;
    Collab_msg_buffer recv_buf;    // Receive buffer for partial messages
    U64 last_activity_ms;          // Last time data was received
    U64 last_heartbeat_ms;         // Last time heartbeat was sent
    U64 connect_start_ms;          // When connection attempt started
} Collab_connection;

// Server listener structure
typedef struct {
    int listen_fd;                 // Listening socket (-1 if not listening)
    U16 port;
    bool is_listening;
} Collab_listener;

// Initialize/deinitialize
void collab_connection_init(Collab_connection *conn);
void collab_connection_deinit(Collab_connection *conn);
void collab_listener_init(Collab_listener *listener);
void collab_listener_deinit(Collab_listener *listener);

// Server operations
// Start listening on port, returns true on success
bool collab_listener_start(Collab_listener *listener, U16 port, Collab_error *err);

// Stop listening
void collab_listener_stop(Collab_listener *listener);

// Accept new connection (non-blocking)
// Returns true if a new connection was accepted, false if no pending connections
bool collab_listener_accept(Collab_listener *listener, Collab_connection *conn, Collab_error *err);

// Client operations
// Start non-blocking connect to server
bool collab_connect_start(Collab_connection *conn, const char *host, U16 port, Collab_error *err);

// Check if non-blocking connect completed
// Returns: COLLAB_CONN_CONNECTING (still in progress), COLLAB_CONN_CONNECTED (success),
//          or COLLAB_CONN_DISCONNECTED (failed)
Collab_conn_state collab_connect_check(Collab_connection *conn, Collab_error *err);

// Close connection
void collab_connection_close(Collab_connection *conn);

// Send data (non-blocking)
// Returns number of bytes sent, or -1 on error
// Sets err if error occurs
Isz collab_send(Collab_connection *conn, const U8 *data, Usz len, Collab_error *err);

// Send complete message (header + payload)
// Blocks until all data is sent or error
bool collab_send_message(Collab_connection *conn, Collab_msg_type type,
                         const U8 *payload, Usz payload_len, Collab_error *err);

// Receive data into connection buffer (non-blocking)
// Returns: COLLAB_SOCK_OK (data received), COLLAB_SOCK_WOULD_BLOCK (no data),
//          COLLAB_SOCK_CLOSED (peer closed), COLLAB_SOCK_ERROR (error)
Collab_sock_result collab_recv(Collab_connection *conn, Collab_error *err);

// Check if a complete message is available in receive buffer
bool collab_has_message(Collab_connection *conn);

// Get next complete message from receive buffer
// Returns true if message was retrieved, false if no complete message
// On success, header and payload are set (payload points into recv buffer)
bool collab_get_message(Collab_connection *conn, Collab_msg_header *header,
                        const U8 **payload, Usz *payload_len);

// Polling helpers using select()
typedef struct {
    int max_fd;
    void *read_set;   // fd_set* (opaque to hide system headers)
    void *write_set;  // fd_set*
} Collab_poll_set;

// Initialize poll set
void collab_poll_init(Collab_poll_set *poll);

// Cleanup poll set
void collab_poll_deinit(Collab_poll_set *poll);

// Clear poll set
void collab_poll_clear(Collab_poll_set *poll);

// Add file descriptor for read monitoring
void collab_poll_add_read(Collab_poll_set *poll, int fd);

// Add file descriptor for write monitoring
void collab_poll_add_write(Collab_poll_set *poll, int fd);

// Poll with timeout (milliseconds, 0 for non-blocking)
// Returns number of ready file descriptors, or -1 on error
int collab_poll(Collab_poll_set *poll, int timeout_ms);

// Check if file descriptor is ready for reading
bool collab_poll_can_read(Collab_poll_set *poll, int fd);

// Check if file descriptor is ready for writing
bool collab_poll_can_write(Collab_poll_set *poll, int fd);

// Set socket to non-blocking mode
bool collab_set_nonblocking(int fd);

// Set TCP_NODELAY (disable Nagle's algorithm)
bool collab_set_nodelay(int fd);

// Set SO_REUSEADDR
bool collab_set_reuseaddr(int fd);
