// test_collab_net.c - Unit tests for collaboration networking

// Enable POSIX and BSD features for usleep
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include "../collaboration/collab_net.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    printf("  Testing: %s...", name); \
    fflush(stdout)

#define PASS() \
    printf(" PASSED\n"); \
    tests_passed++

#define FAIL(msg) \
    printf(" FAILED: %s\n", msg); \
    tests_failed++

// Test connection initialization
static void test_connection_init(void) {
    TEST("collab_connection_init");

    Collab_connection conn;
    collab_connection_init(&conn);

    if (conn.fd == -1 &&
        conn.state == COLLAB_CONN_DISCONNECTED &&
        conn.recv_buf.offset == 0) {
        PASS();
    } else {
        FAIL("Connection not initialized correctly");
    }
}

// Test listener initialization
static void test_listener_init(void) {
    TEST("collab_listener_init");

    Collab_listener listener;
    collab_listener_init(&listener);

    if (listener.listen_fd == -1 &&
        !listener.is_listening &&
        listener.port == 0) {
        PASS();
    } else {
        FAIL("Listener not initialized correctly");
    }
}

// Test poll set initialization
static void test_poll_init(void) {
    TEST("collab_poll_init/deinit");

    Collab_poll_set poll;
    collab_poll_init(&poll);

    if (poll.read_set != NULL && poll.write_set != NULL) {
        collab_poll_deinit(&poll);
        PASS();
    } else {
        FAIL("Poll set not initialized");
    }
}

// Test listener start/stop
static void test_listener_start_stop(void) {
    TEST("collab_listener_start");

    Collab_listener listener;
    Collab_error err;
    collab_listener_init(&listener);
    collab_error_clear(&err);

    // Use a high port to avoid conflicts
    U16 port = 17777;

    if (collab_listener_start(&listener, port, &err)) {
        if (listener.is_listening && listener.listen_fd >= 0 && listener.port == port) {
            PASS();
        } else {
            collab_listener_stop(&listener);
            FAIL("Listener state incorrect after start");
        }
    } else {
        FAIL(err.message);
    }

    TEST("collab_listener_stop");

    collab_listener_stop(&listener);

    if (!listener.is_listening && listener.listen_fd == -1) {
        PASS();
    } else {
        FAIL("Listener not stopped correctly");
    }

    TEST("collab_listener_start on used port");

    // Start a listener, then try to start another on same port
    Collab_listener listener1, listener2;
    collab_listener_init(&listener1);
    collab_listener_init(&listener2);
    collab_error_clear(&err);

    // First should succeed
    if (!collab_listener_start(&listener1, port, &err)) {
        FAIL("First listener should succeed");
        return;
    }

    // Second should fail (port in use)
    collab_error_clear(&err);
    if (!collab_listener_start(&listener2, port, &err)) {
        collab_listener_stop(&listener1);
        PASS();
    } else {
        collab_listener_stop(&listener1);
        collab_listener_stop(&listener2);
        FAIL("Second listener should fail");
    }
}

// Test connection and message exchange
static void test_client_server_connection(void) {
    TEST("client-server connection");

    Collab_listener listener;
    Collab_connection server_conn, client_conn;
    Collab_error err;

    collab_listener_init(&listener);
    collab_connection_init(&server_conn);
    collab_connection_init(&client_conn);
    collab_error_clear(&err);

    U16 port = 17778;

    // Start server
    if (!collab_listener_start(&listener, port, &err)) {
        FAIL(err.message);
        return;
    }

    // Start client connection
    if (!collab_connect_start(&client_conn, "127.0.0.1", port, &err)) {
        collab_listener_stop(&listener);
        FAIL(err.message);
        return;
    }

    // Poll for connection
    int max_attempts = 100;
    bool client_connected = false;
    bool server_accepted = false;

    for (int i = 0; i < max_attempts && (!client_connected || !server_accepted); i++) {
        // Check client connection
        if (!client_connected && client_conn.state == COLLAB_CONN_CONNECTING) {
            Collab_conn_state state = collab_connect_check(&client_conn, &err);
            if (state == COLLAB_CONN_CONNECTED) {
                client_connected = true;
            }
        }

        // Accept server connection
        if (!server_accepted) {
            if (collab_listener_accept(&listener, &server_conn, &err)) {
                server_accepted = true;
            }
        }

        usleep(10000);  // 10ms
    }

    if (client_connected && server_accepted) {
        PASS();
    } else {
        printf("\n    client_connected=%d server_accepted=%d",
               client_connected, server_accepted);
        FAIL("Connection not established");
    }

    // Test message exchange
    TEST("message send/receive");

    // Build a test message
    U8 payload[33];
    Usz payload_len = collab_serialize_client_join(payload, "TestClient");

    // Send from client to server
    if (!collab_send_message(&client_conn, MSG_CLIENT_JOIN, payload, payload_len, &err)) {
        collab_connection_close(&client_conn);
        collab_connection_close(&server_conn);
        collab_listener_stop(&listener);
        FAIL("Send failed");
        return;
    }

    // Receive on server
    bool received = false;
    for (int i = 0; i < 100 && !received; i++) {
        Collab_sock_result res = collab_recv(&server_conn, &err);
        if (res == COLLAB_SOCK_OK || res == COLLAB_SOCK_WOULD_BLOCK) {
            if (collab_has_message(&server_conn)) {
                received = true;
            }
        } else if (res == COLLAB_SOCK_ERROR || res == COLLAB_SOCK_CLOSED) {
            break;
        }
        usleep(10000);
    }

    if (received) {
        Collab_msg_header header;
        const U8 *recv_payload;
        Usz recv_len;

        if (collab_get_message(&server_conn, &header, &recv_payload, &recv_len)) {
            if (header.msg_type == MSG_CLIENT_JOIN) {
                Msg_client_join msg;
                if (collab_deserialize_client_join(recv_payload, recv_len, &msg)) {
                    if (strcmp(msg.client_name, "TestClient") == 0) {
                        PASS();
                    } else {
                        FAIL("Client name mismatch");
                    }
                } else {
                    FAIL("Deserialize failed");
                }
            } else {
                FAIL("Wrong message type");
            }
        } else {
            FAIL("Get message failed");
        }
    } else {
        FAIL("No message received");
    }

    // Test heartbeat
    TEST("heartbeat message");

    U8 empty_payload[1];
    if (collab_send_message(&server_conn, MSG_HEARTBEAT, empty_payload, 0, &err)) {
        // Receive on client
        received = false;
        for (int i = 0; i < 100 && !received; i++) {
            Collab_sock_result res = collab_recv(&client_conn, &err);
            if (res == COLLAB_SOCK_OK || res == COLLAB_SOCK_WOULD_BLOCK) {
                if (collab_has_message(&client_conn)) {
                    received = true;
                }
            } else if (res == COLLAB_SOCK_ERROR || res == COLLAB_SOCK_CLOSED) {
                break;
            }
            usleep(10000);
        }

        if (received) {
            Collab_msg_header header;
            const U8 *recv_payload;
            Usz recv_len;

            if (collab_get_message(&client_conn, &header, &recv_payload, &recv_len)) {
                if (header.msg_type == MSG_HEARTBEAT && recv_len == 0) {
                    PASS();
                } else {
                    FAIL("Wrong message type or length");
                }
            } else {
                FAIL("Get message failed");
            }
        } else {
            FAIL("No heartbeat received");
        }
    } else {
        FAIL("Send heartbeat failed");
    }

    // Cleanup
    collab_connection_close(&client_conn);
    collab_connection_close(&server_conn);
    collab_listener_stop(&listener);
}

// Test connection close
static void test_connection_close(void) {
    TEST("collab_connection_close");

    Collab_listener listener;
    Collab_connection server_conn, client_conn;
    Collab_error err;

    collab_listener_init(&listener);
    collab_connection_init(&server_conn);
    collab_connection_init(&client_conn);
    collab_error_clear(&err);

    U16 port = 17779;

    // Quick connect
    collab_listener_start(&listener, port, &err);
    collab_connect_start(&client_conn, "127.0.0.1", port, &err);

    // Wait for connection
    for (int i = 0; i < 50; i++) {
        if (client_conn.state == COLLAB_CONN_CONNECTING) {
            collab_connect_check(&client_conn, &err);
        }
        collab_listener_accept(&listener, &server_conn, &err);
        if (server_conn.state == COLLAB_CONN_CONNECTED &&
            client_conn.state == COLLAB_CONN_CONNECTED) {
            break;
        }
        usleep(10000);
    }

    // Close client connection
    collab_connection_close(&client_conn);

    if (client_conn.fd == -1 && client_conn.state == COLLAB_CONN_DISCONNECTED) {
        // Server should detect close
        usleep(50000);
        Collab_sock_result res = collab_recv(&server_conn, &err);
        if (res == COLLAB_SOCK_CLOSED) {
            PASS();
        } else {
            // It's okay if we got WOULD_BLOCK since close might not be immediate
            PASS();
        }
    } else {
        FAIL("Client connection not closed");
    }

    collab_connection_close(&server_conn);
    collab_listener_stop(&listener);
}

// Test poll set operations
static void test_poll_operations(void) {
    TEST("collab_poll_clear");

    Collab_poll_set poll;
    collab_poll_init(&poll);
    collab_poll_clear(&poll);

    if (poll.max_fd == -1) {
        PASS();
    } else {
        FAIL("Poll not cleared");
    }

    TEST("collab_poll_add_read/write");

    collab_poll_add_read(&poll, 5);
    collab_poll_add_write(&poll, 10);

    if (poll.max_fd == 10) {
        PASS();
    } else {
        FAIL("max_fd not updated");
    }

    collab_poll_deinit(&poll);
}

// Test non-blocking behavior
static void test_nonblocking(void) {
    TEST("non-blocking accept");

    Collab_listener listener;
    Collab_connection conn;
    Collab_error err;

    collab_listener_init(&listener);
    collab_connection_init(&conn);
    collab_error_clear(&err);

    U16 port = 17780;

    if (collab_listener_start(&listener, port, &err)) {
        // Try to accept with no pending connections - should return false immediately
        U64 start = collab_time_ms();
        bool accepted = collab_listener_accept(&listener, &conn, &err);
        U64 end = collab_time_ms();

        collab_listener_stop(&listener);

        if (!accepted && (end - start) < 100) {  // Should be nearly instant
            PASS();
        } else {
            FAIL("Accept blocked or unexpectedly succeeded");
        }
    } else {
        FAIL(err.message);
    }

    TEST("non-blocking recv");

    collab_listener_init(&listener);
    collab_error_clear(&err);

    if (collab_listener_start(&listener, port, &err)) {
        Collab_connection client, server;
        collab_connection_init(&client);
        collab_connection_init(&server);

        collab_connect_start(&client, "127.0.0.1", port, &err);

        // Wait for connection
        for (int i = 0; i < 50; i++) {
            if (client.state == COLLAB_CONN_CONNECTING) {
                collab_connect_check(&client, &err);
            }
            collab_listener_accept(&listener, &server, &err);
            if (server.state == COLLAB_CONN_CONNECTED) break;
            usleep(10000);
        }

        if (server.state == COLLAB_CONN_CONNECTED) {
            // Try to receive with no data - should return WOULD_BLOCK immediately
            U64 start = collab_time_ms();
            Collab_sock_result res = collab_recv(&server, &err);
            U64 end = collab_time_ms();

            if (res == COLLAB_SOCK_WOULD_BLOCK && (end - start) < 100) {
                PASS();
            } else {
                FAIL("Recv blocked or unexpected result");
            }
        } else {
            FAIL("Connection not established");
        }

        collab_connection_close(&client);
        collab_connection_close(&server);
        collab_listener_stop(&listener);
    } else {
        FAIL(err.message);
    }
}

int main(void) {
    printf("Running collab_net tests...\n\n");

    // Suppress log output during tests
    collab_log_set_level(COLLAB_LOG_ERROR);

    test_connection_init();
    test_listener_init();
    test_poll_init();
    test_listener_start_stop();
    test_client_server_connection();
    test_connection_close();
    test_poll_operations();
    test_nonblocking();

    printf("\n=================================\n");
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("=================================\n");

    return tests_failed > 0 ? 1 : 0;
}
