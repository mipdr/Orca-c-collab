// test_client.c - Simple test client for Orca collaboration
// This client connects to a server and exchanges heartbeat messages

// Enable POSIX and BSD features for usleep
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include "../collaboration/collab_net.h"
#include "../collaboration/collab_protocol.h"

static volatile int running = 1;

static void signal_handler(int sig) {
    (void)sig;
    running = 0;
    printf("\nDisconnecting...\n");
}

int main(int argc, char *argv[]) {
    const char *host = "127.0.0.1";
    U16 port = COLLAB_DEFAULT_PORT;
    const char *name = "TestClient";

    if (argc > 1) {
        host = argv[1];
    }
    if (argc > 2) {
        port = (U16)atoi(argv[2]);
    }
    if (argc > 3) {
        name = argv[3];
    }

    // Set up signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("=== Orca Collaboration Test Client ===\n");
    printf("Connecting to %s:%u as '%s'...\n", host, port, name);

    // Initialize
    Collab_connection conn;
    Collab_error err;

    collab_connection_init(&conn);
    collab_error_clear(&err);

    // Start connection
    if (!collab_connect_start(&conn, host, port, &err)) {
        fprintf(stderr, "Failed to start connection: %s\n", err.message);
        return 1;
    }

    // Wait for connection
    printf("Waiting for connection...\n");
    int max_wait = 50;  // 5 seconds
    while (max_wait > 0 && conn.state == COLLAB_CONN_CONNECTING) {
        Collab_conn_state state = collab_connect_check(&conn, &err);
        if (state == COLLAB_CONN_DISCONNECTED) {
            fprintf(stderr, "Connection failed: %s\n", err.message);
            return 1;
        }
        usleep(100000);  // 100ms
        max_wait--;
    }

    if (conn.state != COLLAB_CONN_CONNECTED) {
        fprintf(stderr, "Connection timed out\n");
        collab_connection_close(&conn);
        return 1;
    }

    printf("Connected!\n");

    // Send join message
    printf("Sending join request...\n");
    U8 join_payload[33];
    Usz join_len = collab_serialize_client_join(join_payload, name);
    if (!collab_send_message(&conn, MSG_CLIENT_JOIN, join_payload, join_len, &err)) {
        fprintf(stderr, "Failed to send join: %s\n", err.message);
        collab_connection_close(&conn);
        return 1;
    }

    // Create poll set
    Collab_poll_set poll;
    collab_poll_init(&poll);

    U64 last_heartbeat = collab_time_ms();
    bool joined = false;
    (void)host;  // Used for logging

    printf("Press Ctrl+C to disconnect\n\n");

    // Main loop
    while (running && conn.state == COLLAB_CONN_CONNECTED) {
        // Set up poll
        collab_poll_clear(&poll);
        collab_poll_add_read(&poll, conn.fd);

        // Poll with 100ms timeout
        int ready = collab_poll(&poll, 100);
        if (ready < 0) {
            break;
        }

        // Read data
        if (collab_poll_can_read(&poll, conn.fd)) {
            Collab_sock_result res = collab_recv(&conn, &err);

            if (res == COLLAB_SOCK_CLOSED) {
                printf("[CLIENT] Server disconnected\n");
                break;
            }

            if (res == COLLAB_SOCK_ERROR) {
                printf("[CLIENT] Error: %s\n", err.message);
                break;
            }

            // Process complete messages
            while (collab_has_message(&conn)) {
                Collab_msg_header header;
                const U8 *payload;
                Usz payload_len;

                if (collab_get_message(&conn, &header, &payload, &payload_len)) {
                    printf("[CLIENT] Received %s\n",
                           collab_msg_type_name((Collab_msg_type)header.msg_type));

                    // Handle message
                    switch ((Collab_msg_type)header.msg_type) {
                    case MSG_HEARTBEAT:
                        // Server heartbeat - just acknowledge receipt
                        break;

                    case MSG_CLIENT_ACCEPT: {
                        Msg_client_accept accept_msg;
                        if (collab_deserialize_client_accept(payload, payload_len, &accept_msg)) {
                            joined = true;
                            printf("[CLIENT] Joined! ID=%u, Color=%u, Total clients=%u\n",
                                   accept_msg.client_id, accept_msg.color_index,
                                   accept_msg.num_clients);
                        }
                        break;
                    }

                    case MSG_CLIENT_REJECT: {
                        Msg_client_reject reject_msg;
                        if (collab_deserialize_client_reject(payload, payload_len, &reject_msg)) {
                            printf("[CLIENT] Rejected! Reason: %s\n", reject_msg.reason_text);
                            running = 0;
                        }
                        break;
                    }

                    default:
                        printf("[CLIENT] Unhandled message type: %s\n",
                               collab_msg_type_name((Collab_msg_type)header.msg_type));
                        break;
                    }
                }
            }
        }

        // Send periodic heartbeats
        U64 now = collab_time_ms();
        if (joined && now - last_heartbeat > COLLAB_HEARTBEAT_INTERVAL_MS) {
            printf("[CLIENT] Sending heartbeat...\n");
            if (!collab_send_message(&conn, MSG_HEARTBEAT, NULL, 0, &err)) {
                printf("[CLIENT] Failed to send heartbeat: %s\n", err.message);
            }
            last_heartbeat = now;
        }
    }

    // Send leave message
    if (conn.state == COLLAB_CONN_CONNECTED && joined) {
        printf("Sending leave message...\n");
        collab_send_message(&conn, MSG_CLIENT_LEAVE, NULL, 0, &err);
    }

    // Cleanup
    collab_connection_close(&conn);
    collab_poll_deinit(&poll);

    printf("Disconnected.\n");
    return 0;
}
