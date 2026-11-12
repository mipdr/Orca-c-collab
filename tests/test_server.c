// test_server.c - Simple test server for Orca collaboration
// This server accepts connections and exchanges heartbeat messages

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
    printf("\nShutting down...\n");
}

int main(int argc, char *argv[]) {
    U16 port = COLLAB_DEFAULT_PORT;

    if (argc > 1) {
        port = (U16)atoi(argv[1]);
    }

    // Set up signal handler for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("=== Orca Collaboration Test Server ===\n");
    printf("Starting server on port %u...\n", port);

    // Initialize
    Collab_listener listener;
    Collab_connection clients[COLLAB_MAX_CLIENTS];
    U8 num_clients = 0;
    Collab_error err;

    collab_listener_init(&listener);
    for (int i = 0; i < COLLAB_MAX_CLIENTS; i++) {
        collab_connection_init(&clients[i]);
    }
    collab_error_clear(&err);

    // Start listening
    if (!collab_listener_start(&listener, port, &err)) {
        fprintf(stderr, "Failed to start server: %s\n", err.message);
        return 1;
    }

    printf("Server listening on port %u\n", port);
    printf("Press Ctrl+C to stop\n\n");

    // Create poll set
    Collab_poll_set poll;
    collab_poll_init(&poll);

    U64 last_heartbeat = collab_time_ms();

    // Main loop
    while (running) {
        // Set up poll
        collab_poll_clear(&poll);
        collab_poll_add_read(&poll, listener.listen_fd);

        for (int i = 0; i < COLLAB_MAX_CLIENTS; i++) {
            if (clients[i].state == COLLAB_CONN_CONNECTED) {
                collab_poll_add_read(&poll, clients[i].fd);
            }
        }

        // Poll with 100ms timeout
        int ready = collab_poll(&poll, 100);
        if (ready < 0) {
            break;
        }

        // Accept new connections
        if (collab_poll_can_read(&poll, listener.listen_fd)) {
            // Find free slot
            int slot = -1;
            for (int i = 0; i < COLLAB_MAX_CLIENTS; i++) {
                if (clients[i].state == COLLAB_CONN_DISCONNECTED) {
                    slot = i;
                    break;
                }
            }

            if (slot >= 0) {
                if (collab_listener_accept(&listener, &clients[slot], &err)) {
                    num_clients++;
                    printf("[SERVER] Client connected in slot %d (total: %u)\n",
                           slot, num_clients);
                }
            } else {
                // Server full - need to accept and reject
                Collab_connection temp;
                collab_connection_init(&temp);
                if (collab_listener_accept(&listener, &temp, &err)) {
                    printf("[SERVER] Rejecting client - server full\n");

                    // Send rejection message
                    U8 payload[65];
                    Usz len = collab_serialize_client_reject(payload, REJECT_SERVER_FULL,
                                                              "Server is full (10/10)");
                    collab_send_message(&temp, MSG_CLIENT_REJECT, payload, len, &err);
                    collab_connection_close(&temp);
                }
            }
        }

        // Process client data
        for (int i = 0; i < COLLAB_MAX_CLIENTS; i++) {
            if (clients[i].state != COLLAB_CONN_CONNECTED) {
                continue;
            }

            if (collab_poll_can_read(&poll, clients[i].fd)) {
                Collab_sock_result res = collab_recv(&clients[i], &err);

                if (res == COLLAB_SOCK_CLOSED) {
                    printf("[SERVER] Client %d disconnected\n", i);
                    collab_connection_close(&clients[i]);
                    num_clients--;
                    continue;
                }

                if (res == COLLAB_SOCK_ERROR) {
                    printf("[SERVER] Client %d error: %s\n", i, err.message);
                    collab_connection_close(&clients[i]);
                    num_clients--;
                    continue;
                }

                // Process complete messages
                while (collab_has_message(&clients[i])) {
                    Collab_msg_header header;
                    const U8 *payload;
                    Usz payload_len;

                    if (collab_get_message(&clients[i], &header, &payload, &payload_len)) {
                        printf("[SERVER] Received %s from client %d\n",
                               collab_msg_type_name((Collab_msg_type)header.msg_type), i);

                        // Handle message
                        switch ((Collab_msg_type)header.msg_type) {
                        case MSG_HEARTBEAT:
                            // Send heartbeat back
                            collab_send_message(&clients[i], MSG_HEARTBEAT, NULL, 0, &err);
                            break;

                        case MSG_CLIENT_JOIN: {
                            Msg_client_join join_msg;
                            if (collab_deserialize_client_join(payload, payload_len, &join_msg)) {
                                printf("[SERVER] Client %d joined as '%s'\n",
                                       i, join_msg.client_name);

                                // Send accept
                                U8 accept_payload[4];
                                Usz len = collab_serialize_client_accept(accept_payload,
                                    (U8)(i + 1), (U8)(i % 10), num_clients);
                                collab_send_message(&clients[i], MSG_CLIENT_ACCEPT,
                                                   accept_payload, len, &err);
                            }
                            break;
                        }

                        case MSG_CLIENT_LEAVE:
                            printf("[SERVER] Client %d sent leave message\n", i);
                            collab_connection_close(&clients[i]);
                            num_clients--;
                            break;

                        default:
                            printf("[SERVER] Unhandled message type: %s\n",
                                   collab_msg_type_name((Collab_msg_type)header.msg_type));
                            break;
                        }
                    }
                }
            }

            // Check for client timeout
            U64 now = collab_time_ms();
            if (clients[i].state == COLLAB_CONN_CONNECTED &&
                now - clients[i].last_activity_ms > COLLAB_CLIENT_TIMEOUT_MS) {
                printf("[SERVER] Client %d timed out\n", i);
                collab_connection_close(&clients[i]);
                num_clients--;
            }
        }

        // Send periodic heartbeats to all clients
        U64 now = collab_time_ms();
        if (now - last_heartbeat > COLLAB_HEARTBEAT_INTERVAL_MS) {
            for (int i = 0; i < COLLAB_MAX_CLIENTS; i++) {
                if (clients[i].state == COLLAB_CONN_CONNECTED) {
                    if (!collab_send_message(&clients[i], MSG_HEARTBEAT, NULL, 0, &err)) {
                        printf("[SERVER] Failed to send heartbeat to client %d\n", i);
                    }
                }
            }
            last_heartbeat = now;
        }
    }

    // Cleanup
    printf("Cleaning up...\n");
    for (int i = 0; i < COLLAB_MAX_CLIENTS; i++) {
        collab_connection_deinit(&clients[i]);
    }
    collab_listener_stop(&listener);
    collab_poll_deinit(&poll);

    printf("Server stopped.\n");
    return 0;
}
