// collab_net.c - TCP socket primitives for Orca collaboration
// Non-blocking I/O with select()

// Enable POSIX features
#if !defined(_POSIX_C_SOURCE) || _POSIX_C_SOURCE < 200112L
#undef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif

#include "collab_net.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>

// Initialize connection
void collab_connection_init(Collab_connection *conn) {
    conn->fd = -1;
    conn->state = COLLAB_CONN_DISCONNECTED;
    collab_msg_buffer_init(&conn->recv_buf);
    conn->last_activity_ms = 0;
    conn->last_heartbeat_ms = 0;
    conn->connect_start_ms = 0;
}

// Deinitialize connection
void collab_connection_deinit(Collab_connection *conn) {
    collab_connection_close(conn);
}

// Initialize listener
void collab_listener_init(Collab_listener *listener) {
    listener->listen_fd = -1;
    listener->port = 0;
    listener->is_listening = false;
}

// Deinitialize listener
void collab_listener_deinit(Collab_listener *listener) {
    collab_listener_stop(listener);
}

// Set socket to non-blocking mode
bool collab_set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return false;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        return false;
    }
    return true;
}

// Set TCP_NODELAY
bool collab_set_nodelay(int fd) {
    int flag = 1;
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) == 0;
}

// Set SO_REUSEADDR
bool collab_set_reuseaddr(int fd) {
    int flag = 1;
    return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) == 0;
}

// Start listening
bool collab_listener_start(Collab_listener *listener, U16 port, Collab_error *err) {
    // Create socket
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        collab_error_set(err, errno, "Failed to create socket: %s", strerror(errno));
        return false;
    }

    // Set socket options
    if (!collab_set_reuseaddr(fd)) {
        collab_error_set(err, errno, "Failed to set SO_REUSEADDR: %s", strerror(errno));
        close(fd);
        return false;
    }

    if (!collab_set_nonblocking(fd)) {
        collab_error_set(err, errno, "Failed to set non-blocking: %s", strerror(errno));
        close(fd);
        return false;
    }

    // Bind
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        collab_error_set_network(err, errno, "bind", NULL, port);
        close(fd);
        return false;
    }

    // Listen
    if (listen(fd, COLLAB_MAX_CLIENTS) < 0) {
        collab_error_set(err, errno, "Failed to listen: %s", strerror(errno));
        close(fd);
        return false;
    }

    listener->listen_fd = fd;
    listener->port = port;
    listener->is_listening = true;

    COLLAB_LOG_INFO("Server listening on port %u", port);
    return true;
}

// Stop listening
void collab_listener_stop(Collab_listener *listener) {
    if (listener->listen_fd >= 0) {
        close(listener->listen_fd);
        listener->listen_fd = -1;
    }
    listener->is_listening = false;
    listener->port = 0;
}

// Accept new connection (non-blocking)
bool collab_listener_accept(Collab_listener *listener, Collab_connection *conn, Collab_error *err) {
    if (!listener->is_listening || listener->listen_fd < 0) {
        collab_error_set(err, -1, "Listener not active");
        return false;
    }

    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int client_fd = accept(listener->listen_fd, (struct sockaddr *)&client_addr, &addr_len);

    if (client_fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // No pending connections
            return false;
        }
        collab_error_set(err, errno, "Accept failed: %s", strerror(errno));
        return false;
    }

    // Set client socket options
    if (!collab_set_nonblocking(client_fd)) {
        collab_error_set(err, errno, "Failed to set client non-blocking: %s", strerror(errno));
        close(client_fd);
        return false;
    }

    if (!collab_set_nodelay(client_fd)) {
        COLLAB_LOG_WARN("Failed to set TCP_NODELAY on client socket");
    }

    // Initialize connection
    collab_connection_init(conn);
    conn->fd = client_fd;
    conn->state = COLLAB_CONN_CONNECTED;
    conn->last_activity_ms = collab_time_ms();

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    COLLAB_LOG_INFO("Accepted connection from %s:%u", client_ip, ntohs(client_addr.sin_port));

    return true;
}

// Start non-blocking connect to server
bool collab_connect_start(Collab_connection *conn, const char *host, U16 port, Collab_error *err) {
    // Close any existing connection
    collab_connection_close(conn);

    // Resolve hostname
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", port);

    int rc = getaddrinfo(host, port_str, &hints, &result);
    if (rc != 0) {
        collab_error_set_network(err, EAI_NONAME, "connect to", host, port);
        return false;
    }

    // Create socket
    int fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (fd < 0) {
        collab_error_set(err, errno, "Failed to create socket: %s", strerror(errno));
        freeaddrinfo(result);
        return false;
    }

    // Set non-blocking before connect
    if (!collab_set_nonblocking(fd)) {
        collab_error_set(err, errno, "Failed to set non-blocking: %s", strerror(errno));
        close(fd);
        freeaddrinfo(result);
        return false;
    }

    if (!collab_set_nodelay(fd)) {
        COLLAB_LOG_WARN("Failed to set TCP_NODELAY");
    }

    // Start non-blocking connect
    rc = connect(fd, result->ai_addr, result->ai_addrlen);
    freeaddrinfo(result);

    if (rc < 0) {
        if (errno == EINPROGRESS) {
            // Connection in progress (expected for non-blocking)
            conn->fd = fd;
            conn->state = COLLAB_CONN_CONNECTING;
            conn->connect_start_ms = collab_time_ms();
            COLLAB_LOG_INFO("Connecting to %s:%u...", host, port);
            return true;
        } else {
            collab_error_set_network(err, errno, "connect to", host, port);
            close(fd);
            return false;
        }
    }

    // Connected immediately (rare, but possible on localhost)
    conn->fd = fd;
    conn->state = COLLAB_CONN_CONNECTED;
    conn->last_activity_ms = collab_time_ms();
    COLLAB_LOG_INFO("Connected to %s:%u", host, port);
    return true;
}

// Check if non-blocking connect completed
Collab_conn_state collab_connect_check(Collab_connection *conn, Collab_error *err) {
    if (conn->state != COLLAB_CONN_CONNECTING) {
        return conn->state;
    }

    // Check for timeout
    U64 now = collab_time_ms();
    if (now - conn->connect_start_ms > COLLAB_CONNECT_TIMEOUT_MS) {
        collab_error_set_network(err, ETIMEDOUT, "connect", NULL, 0);
        collab_connection_close(conn);
        return COLLAB_CONN_DISCONNECTED;
    }

    // Use select to check if socket is writable (connect completed)
    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(conn->fd, &writefds);

    struct timeval tv = {0, 0};  // Non-blocking
    int rc = select(conn->fd + 1, NULL, &writefds, NULL, &tv);

    if (rc < 0) {
        collab_error_set(err, errno, "Select failed: %s", strerror(errno));
        collab_connection_close(conn);
        return COLLAB_CONN_DISCONNECTED;
    }

    if (rc == 0) {
        // Still connecting
        return COLLAB_CONN_CONNECTING;
    }

    // Check for errors
    int socket_error = 0;
    socklen_t len = sizeof(socket_error);
    if (getsockopt(conn->fd, SOL_SOCKET, SO_ERROR, &socket_error, &len) < 0) {
        collab_error_set(err, errno, "getsockopt failed: %s", strerror(errno));
        collab_connection_close(conn);
        return COLLAB_CONN_DISCONNECTED;
    }

    if (socket_error != 0) {
        collab_error_set_network(err, socket_error, "connect", NULL, 0);
        collab_connection_close(conn);
        return COLLAB_CONN_DISCONNECTED;
    }

    // Connected!
    conn->state = COLLAB_CONN_CONNECTED;
    conn->last_activity_ms = collab_time_ms();
    COLLAB_LOG_INFO("Connection established");
    return COLLAB_CONN_CONNECTED;
}

// Close connection
void collab_connection_close(Collab_connection *conn) {
    if (conn->fd >= 0) {
        close(conn->fd);
        conn->fd = -1;
    }
    conn->state = COLLAB_CONN_DISCONNECTED;
    collab_msg_buffer_reset(&conn->recv_buf);
}

// Send data (non-blocking)
Isz collab_send(Collab_connection *conn, const U8 *data, Usz len, Collab_error *err) {
    if (conn->fd < 0 || conn->state != COLLAB_CONN_CONNECTED) {
        collab_error_set(err, -1, "Not connected");
        return -1;
    }

    ssize_t sent = send(conn->fd, data, len, MSG_NOSIGNAL);
    if (sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;  // Would block
        }
        collab_error_set(err, errno, "Send failed: %s", strerror(errno));
        return -1;
    }

    return sent;
}

// Send complete message (blocking until all sent or error)
bool collab_send_message(Collab_connection *conn, Collab_msg_type type,
                         const U8 *payload, Usz payload_len, Collab_error *err) {
    U8 buf[COLLAB_MAX_MESSAGE_SIZE];
    Usz total_len = collab_build_message(buf, type, payload, payload_len);

    Usz sent = 0;
    while (sent < total_len) {
        Isz n = collab_send(conn, buf + sent, total_len - sent, err);
        if (n < 0) {
            return false;
        }
        if (n == 0) {
            // Would block - use select to wait
            fd_set writefds;
            FD_ZERO(&writefds);
            FD_SET(conn->fd, &writefds);

            struct timeval tv;
            tv.tv_sec = 5;
            tv.tv_usec = 0;

            int rc = select(conn->fd + 1, NULL, &writefds, NULL, &tv);
            if (rc < 0) {
                collab_error_set(err, errno, "Select failed: %s", strerror(errno));
                return false;
            }
            if (rc == 0) {
                collab_error_set(err, ETIMEDOUT, "Send timed out");
                return false;
            }
            continue;
        }
        sent += (Usz)n;
    }

    return true;
}

// Receive data into connection buffer (non-blocking)
Collab_sock_result collab_recv(Collab_connection *conn, Collab_error *err) {
    if (conn->fd < 0 || conn->state != COLLAB_CONN_CONNECTED) {
        collab_error_set(err, -1, "Not connected");
        return COLLAB_SOCK_ERROR;
    }

    Collab_msg_buffer *buf = &conn->recv_buf;

    // Read into buffer
    Usz space = sizeof(buf->buffer) - buf->offset;
    if (space == 0) {
        collab_error_set(err, ENOBUFS, "Receive buffer full");
        return COLLAB_SOCK_ERROR;
    }

    ssize_t n = recv(conn->fd, buf->buffer + buf->offset, space, 0);

    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return COLLAB_SOCK_WOULD_BLOCK;
        }
        collab_error_set(err, errno, "Recv failed: %s", strerror(errno));
        return COLLAB_SOCK_ERROR;
    }

    if (n == 0) {
        return COLLAB_SOCK_CLOSED;
    }

    buf->offset += (Usz)n;
    conn->last_activity_ms = collab_time_ms();

    return COLLAB_SOCK_OK;
}

// Perform pending buffer shift if needed
static void collab_msg_buffer_shift_if_needed(Collab_msg_buffer *buf) {
    if (buf->needs_shift && buf->shift_amount > 0) {
        Usz remaining = buf->offset - buf->shift_amount;
        if (remaining > 0) {
            memmove(buf->buffer, buf->buffer + buf->shift_amount, remaining);
        }
        buf->offset = remaining;
        buf->has_header = false;
        buf->expected_len = 0;
        buf->needs_shift = false;
        buf->shift_amount = 0;
    }
}

// Check if a complete message is available
bool collab_has_message(Collab_connection *conn) {
    Collab_msg_buffer *buf = &conn->recv_buf;

    // First, perform any pending shift from previous message
    collab_msg_buffer_shift_if_needed(buf);

    // Do we have the header?
    if (!buf->has_header) {
        if (buf->offset < COLLAB_HEADER_SIZE) {
            return false;
        }

        // Parse header to get expected length
        Collab_msg_header header;
        if (!collab_parse_header(buf->buffer, &header)) {
            // Invalid header - this is a protocol error
            COLLAB_LOG_ERROR("Invalid message header (bad magic)");
            return false;
        }

        buf->expected_len = COLLAB_HEADER_SIZE + header.length;
        buf->has_header = true;
    }

    return buf->offset >= buf->expected_len;
}

// Get next complete message from receive buffer
bool collab_get_message(Collab_connection *conn, Collab_msg_header *header,
                        const U8 **payload, Usz *payload_len) {
    if (!collab_has_message(conn)) {
        return false;
    }

    Collab_msg_buffer *buf = &conn->recv_buf;

    // Parse header
    if (!collab_parse_header(buf->buffer, header)) {
        return false;
    }

    // Set payload pointer (points directly into buffer)
    *payload = buf->buffer + COLLAB_HEADER_SIZE;
    *payload_len = header->length;

    // Mark buffer for shifting on next has_message/get_message call
    // This allows the caller to use the payload before it gets overwritten
    buf->needs_shift = true;
    buf->shift_amount = buf->expected_len;

    return true;
}

// Poll set implementation
void collab_poll_init(Collab_poll_set *poll) {
    poll->read_set = malloc(sizeof(fd_set));
    poll->write_set = malloc(sizeof(fd_set));
    poll->max_fd = -1;
    FD_ZERO((fd_set *)poll->read_set);
    FD_ZERO((fd_set *)poll->write_set);
}

void collab_poll_deinit(Collab_poll_set *poll) {
    free(poll->read_set);
    free(poll->write_set);
    poll->read_set = NULL;
    poll->write_set = NULL;
}

void collab_poll_clear(Collab_poll_set *poll) {
    FD_ZERO((fd_set *)poll->read_set);
    FD_ZERO((fd_set *)poll->write_set);
    poll->max_fd = -1;
}

void collab_poll_add_read(Collab_poll_set *poll, int fd) {
    if (fd < 0) return;
    FD_SET(fd, (fd_set *)poll->read_set);
    if (fd > poll->max_fd) {
        poll->max_fd = fd;
    }
}

void collab_poll_add_write(Collab_poll_set *poll, int fd) {
    if (fd < 0) return;
    FD_SET(fd, (fd_set *)poll->write_set);
    if (fd > poll->max_fd) {
        poll->max_fd = fd;
    }
}

int collab_poll(Collab_poll_set *poll, int timeout_ms) {
    if (poll->max_fd < 0) {
        return 0;
    }

    struct timeval tv;
    struct timeval *tvp = NULL;

    if (timeout_ms >= 0) {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        tvp = &tv;
    }

    return select(poll->max_fd + 1, (fd_set *)poll->read_set,
                  (fd_set *)poll->write_set, NULL, tvp);
}

bool collab_poll_can_read(Collab_poll_set *poll, int fd) {
    if (fd < 0) return false;
    return FD_ISSET(fd, (fd_set *)poll->read_set) != 0;
}

bool collab_poll_can_write(Collab_poll_set *poll, int fd) {
    if (fd < 0) return false;
    return FD_ISSET(fd, (fd_set *)poll->write_set) != 0;
}
