// test_integration.c - Integration tests for collaboration
// Tests server and client working together

// Enable usleep
#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include "../collaboration/collab_server.h"
#include "../collaboration/collab_client.h"
#include "../gbuffer.h"

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

// Helper to run event loop for some iterations
static void run_updates(Collab_server *srv, Collab_client *clients[], int num_clients, int iterations) {
    for (int i = 0; i < iterations; i++) {
        if (srv) {
            collab_server_update(srv);
        }
        for (int c = 0; c < num_clients; c++) {
            if (clients[c]) {
                collab_client_update(clients[c]);
            }
        }
        usleep(10000);  // 10ms
    }
}

// Test 1: Server starts and stops
static void test_server_lifecycle(void) {
    TEST("server start and stop");

    Collab_server srv;
    collab_server_init(&srv);

    // Create initial grid
    Field initial;
    field_init_fill(&initial, 32, 64, '.');
    gbuffer_poke(initial.buffer, initial.height, initial.width, 0, 0, 'S');

    if (!collab_server_start(&srv, 17777, &initial)) {
        FAIL(collab_server_get_error(&srv));
        field_deinit(&initial);
        collab_server_deinit(&srv);
        return;
    }

    if (collab_server_is_running(&srv)) {
        collab_server_stop(&srv);
        if (!collab_server_is_running(&srv)) {
            PASS();
        } else {
            FAIL("Server should be stopped");
        }
    } else {
        FAIL("Server should be running");
    }

    field_deinit(&initial);
    collab_server_deinit(&srv);
}

// Test 2: Client connects to server
static void test_client_connect(void) {
    TEST("client connects to server");

    Collab_server srv;
    collab_server_init(&srv);

    Field initial;
    field_init_fill(&initial, 32, 64, '.');

    if (!collab_server_start(&srv, 17778, &initial)) {
        FAIL(collab_server_get_error(&srv));
        field_deinit(&initial);
        collab_server_deinit(&srv);
        return;
    }

    Collab_client client;
    collab_client_init(&client);

    if (!collab_client_connect(&client, "127.0.0.1", 17778, "TestClient")) {
        FAIL(collab_client_get_error(&client));
        field_deinit(&initial);
        collab_server_deinit(&srv);
        collab_client_deinit(&client);
        return;
    }

    // Run updates until client is active or timeout
    Collab_client *clients[] = { &client };
    run_updates(&srv, clients, 1, 100);

    if (collab_client_is_active(&client)) {
        if (collab_server_get_client_count(&srv) == 1) {
            PASS();
        } else {
            FAIL("Server should have 1 client");
        }
    } else {
        printf(" (state=%s) ", collab_client_state_string(collab_client_get_state(&client)));
        FAIL("Client should be active");
    }

    collab_client_disconnect(&client);
    run_updates(&srv, NULL, 0, 10);

    field_deinit(&initial);
    collab_server_deinit(&srv);
    collab_client_deinit(&client);
}

// Test 3: Client receives full grid sync
static void test_full_sync(void) {
    TEST("client receives full grid sync");

    Collab_server srv;
    collab_server_init(&srv);

    Field initial;
    field_init_fill(&initial, 32, 64, '.');
    // Write "HELLO" at top
    gbuffer_poke(initial.buffer, initial.height, initial.width, 0, 0, 'H');
    gbuffer_poke(initial.buffer, initial.height, initial.width, 0, 1, 'E');
    gbuffer_poke(initial.buffer, initial.height, initial.width, 0, 2, 'L');
    gbuffer_poke(initial.buffer, initial.height, initial.width, 0, 3, 'L');
    gbuffer_poke(initial.buffer, initial.height, initial.width, 0, 4, 'O');

    if (!collab_server_start(&srv, 17779, &initial)) {
        FAIL(collab_server_get_error(&srv));
        field_deinit(&initial);
        collab_server_deinit(&srv);
        return;
    }

    Collab_client client;
    collab_client_init(&client);

    if (!collab_client_connect(&client, "127.0.0.1", 17779, "SyncTest")) {
        FAIL(collab_client_get_error(&client));
        field_deinit(&initial);
        collab_server_deinit(&srv);
        collab_client_deinit(&client);
        return;
    }

    Collab_client *clients[] = { &client };
    run_updates(&srv, clients, 1, 100);

    if (!collab_client_is_active(&client)) {
        FAIL("Client should be active");
        field_deinit(&initial);
        collab_server_deinit(&srv);
        collab_client_deinit(&client);
        return;
    }

    // Check client grid matches server
    const Field *client_grid = collab_client_get_grid_const(&client);
    const Field *server_grid = collab_server_get_grid(&srv);

    if (collab_grids_equal(client_grid, server_grid)) {
        // Verify HELLO is there
        if (client_grid->buffer[0] == 'H' &&
            client_grid->buffer[1] == 'E' &&
            client_grid->buffer[2] == 'L' &&
            client_grid->buffer[3] == 'L' &&
            client_grid->buffer[4] == 'O') {
            PASS();
        } else {
            FAIL("HELLO not found in client grid");
        }
    } else {
        FAIL("Client grid does not match server");
    }

    collab_client_disconnect(&client);

    field_deinit(&initial);
    collab_server_deinit(&srv);
    collab_client_deinit(&client);
}

// Test 4: Client write propagates to server
static void test_client_write(void) {
    TEST("client write propagates to server");

    Collab_server srv;
    collab_server_init(&srv);

    Field initial;
    field_init_fill(&initial, 32, 64, '.');

    if (!collab_server_start(&srv, 17780, &initial)) {
        FAIL(collab_server_get_error(&srv));
        field_deinit(&initial);
        collab_server_deinit(&srv);
        return;
    }

    Collab_client client;
    collab_client_init(&client);

    if (!collab_client_connect(&client, "127.0.0.1", 17780, "WriteTest")) {
        FAIL(collab_client_get_error(&client));
        field_deinit(&initial);
        collab_server_deinit(&srv);
        collab_client_deinit(&client);
        return;
    }

    Collab_client *clients[] = { &client };
    run_updates(&srv, clients, 1, 100);

    if (!collab_client_is_active(&client)) {
        FAIL("Client should be active");
        field_deinit(&initial);
        collab_server_deinit(&srv);
        collab_client_deinit(&client);
        return;
    }

    // Client writes to grid
    collab_client_write(&client, 10, 20, 'X');

    // Run updates to propagate
    run_updates(&srv, clients, 1, 50);

    // Check server received the write
    const Field *server_grid = collab_server_get_grid(&srv);
    Glyph g = server_grid->buffer[10 * 64 + 20];

    if (g == 'X') {
        PASS();
    } else {
        printf(" (got '%c') ", g);
        FAIL("Server should have 'X'");
    }

    collab_client_disconnect(&client);

    field_deinit(&initial);
    collab_server_deinit(&srv);
    collab_client_deinit(&client);
}

// Test 5: Two clients see each other's writes
static void test_two_clients(void) {
    TEST("two clients see each other's writes");

    Collab_server srv;
    collab_server_init(&srv);

    Field initial;
    field_init_fill(&initial, 32, 64, '.');

    if (!collab_server_start(&srv, 17781, &initial)) {
        FAIL(collab_server_get_error(&srv));
        field_deinit(&initial);
        collab_server_deinit(&srv);
        return;
    }

    Collab_client client1, client2;
    collab_client_init(&client1);
    collab_client_init(&client2);

    // Connect client 1
    if (!collab_client_connect(&client1, "127.0.0.1", 17781, "Client1")) {
        FAIL(collab_client_get_error(&client1));
        field_deinit(&initial);
        collab_server_deinit(&srv);
        collab_client_deinit(&client1);
        collab_client_deinit(&client2);
        return;
    }

    Collab_client *clients1[] = { &client1 };
    run_updates(&srv, clients1, 1, 100);

    // Connect client 2
    if (!collab_client_connect(&client2, "127.0.0.1", 17781, "Client2")) {
        FAIL(collab_client_get_error(&client2));
        field_deinit(&initial);
        collab_server_deinit(&srv);
        collab_client_deinit(&client1);
        collab_client_deinit(&client2);
        return;
    }

    Collab_client *clients2[] = { &client1, &client2 };
    run_updates(&srv, clients2, 2, 100);

    if (!collab_client_is_active(&client1) || !collab_client_is_active(&client2)) {
        FAIL("Both clients should be active");
        field_deinit(&initial);
        collab_server_deinit(&srv);
        collab_client_deinit(&client1);
        collab_client_deinit(&client2);
        return;
    }

    // Client 1 writes
    collab_client_write(&client1, 5, 5, 'A');
    run_updates(&srv, clients2, 2, 50);

    // Client 2 writes
    collab_client_write(&client2, 6, 6, 'B');
    run_updates(&srv, clients2, 2, 50);

    // Check both clients have both writes
    const Field *grid1 = collab_client_get_grid_const(&client1);
    const Field *grid2 = collab_client_get_grid_const(&client2);

    Glyph g1a = grid1->buffer[5 * 64 + 5];
    Glyph g1b = grid1->buffer[6 * 64 + 6];
    Glyph g2a = grid2->buffer[5 * 64 + 5];
    Glyph g2b = grid2->buffer[6 * 64 + 6];

    if (g1a == 'A' && g1b == 'B' && g2a == 'A' && g2b == 'B') {
        PASS();
    } else {
        printf(" (c1: %c,%c  c2: %c,%c) ", g1a, g1b, g2a, g2b);
        FAIL("Writes not synced properly");
    }

    collab_client_disconnect(&client1);
    collab_client_disconnect(&client2);

    field_deinit(&initial);
    collab_server_deinit(&srv);
    collab_client_deinit(&client1);
    collab_client_deinit(&client2);
}

// Test 6: Batch write
static void test_batch_write(void) {
    TEST("batch write propagates correctly");

    Collab_server srv;
    collab_server_init(&srv);

    Field initial;
    field_init_fill(&initial, 32, 64, '.');

    if (!collab_server_start(&srv, 17782, &initial)) {
        FAIL(collab_server_get_error(&srv));
        field_deinit(&initial);
        collab_server_deinit(&srv);
        return;
    }

    Collab_client client;
    collab_client_init(&client);

    if (!collab_client_connect(&client, "127.0.0.1", 17782, "BatchTest")) {
        FAIL(collab_client_get_error(&client));
        field_deinit(&initial);
        collab_server_deinit(&srv);
        collab_client_deinit(&client);
        return;
    }

    Collab_client *clients[] = { &client };
    run_updates(&srv, clients, 1, 100);

    if (!collab_client_is_active(&client)) {
        FAIL("Client should be active");
        field_deinit(&initial);
        collab_server_deinit(&srv);
        collab_client_deinit(&client);
        return;
    }

    // Write a batch of cells (simulating paste)
    Collab_write_tuple writes[10];
    for (int i = 0; i < 10; i++) {
        writes[i].y = 10;
        writes[i].x = (U16)(10 + i);
        writes[i].glyph = (Glyph)('0' + i);
    }

    collab_client_write_batch(&client, writes, 10);
    run_updates(&srv, clients, 1, 50);

    // Check server has all the writes
    const Field *server_grid = collab_server_get_grid(&srv);
    bool all_match = true;
    for (int i = 0; i < 10; i++) {
        Glyph g = server_grid->buffer[10 * 64 + 10 + i];
        if (g != (Glyph)('0' + i)) {
            all_match = false;
            break;
        }
    }

    if (all_match) {
        PASS();
    } else {
        FAIL("Batch write not received correctly");
    }

    collab_client_disconnect(&client);

    field_deinit(&initial);
    collab_server_deinit(&srv);
    collab_client_deinit(&client);
}

// Test 7: Hash verification and resync
static void test_hash_resync(void) {
    TEST("hash mismatch triggers resync");

    Collab_server srv;
    collab_server_init(&srv);

    Field initial;
    field_init_fill(&initial, 32, 64, '.');

    if (!collab_server_start(&srv, 17783, &initial)) {
        FAIL(collab_server_get_error(&srv));
        field_deinit(&initial);
        collab_server_deinit(&srv);
        return;
    }

    Collab_client client;
    collab_client_init(&client);

    if (!collab_client_connect(&client, "127.0.0.1", 17783, "HashTest")) {
        FAIL(collab_client_get_error(&client));
        field_deinit(&initial);
        collab_server_deinit(&srv);
        collab_client_deinit(&client);
        return;
    }

    Collab_client *clients[] = { &client };
    run_updates(&srv, clients, 1, 100);

    if (!collab_client_is_active(&client)) {
        FAIL("Client should be active");
        field_deinit(&initial);
        collab_server_deinit(&srv);
        collab_client_deinit(&client);
        return;
    }

    // Corrupt client grid directly (bypass normal write)
    Field *client_grid = collab_client_get_grid(&client);
    client_grid->buffer[0] = 'Z';
    collab_hash_cache_invalidate(&client.hash_cache);

    // Send hash manually
    collab_client_send_hash(&client);
    run_updates(&srv, clients, 1, 100);

    // After resync, client grid should match server (corruption fixed)
    const Field *server_grid = collab_server_get_grid(&srv);
    if (collab_grids_equal(client_grid, server_grid)) {
        PASS();
    } else {
        FAIL("Resync did not fix corruption");
    }

    collab_client_disconnect(&client);

    field_deinit(&initial);
    collab_server_deinit(&srv);
    collab_client_deinit(&client);
}

// Test 8: Server write propagates to clients
static void test_server_write(void) {
    TEST("server write propagates to client");

    Collab_server srv;
    collab_server_init(&srv);

    Field initial;
    field_init_fill(&initial, 32, 64, '.');

    if (!collab_server_start(&srv, 17784, &initial)) {
        FAIL(collab_server_get_error(&srv));
        field_deinit(&initial);
        collab_server_deinit(&srv);
        return;
    }

    Collab_client client;
    collab_client_init(&client);

    if (!collab_client_connect(&client, "127.0.0.1", 17784, "ServerWriteTest")) {
        FAIL(collab_client_get_error(&client));
        field_deinit(&initial);
        collab_server_deinit(&srv);
        collab_client_deinit(&client);
        return;
    }

    Collab_client *clients[] = { &client };
    run_updates(&srv, clients, 1, 100);

    if (!collab_client_is_active(&client)) {
        FAIL("Client should be active");
        field_deinit(&initial);
        collab_server_deinit(&srv);
        collab_client_deinit(&client);
        return;
    }

    // Server writes to grid
    collab_server_write(&srv, 15, 25, 'S');
    run_updates(&srv, clients, 1, 50);

    // Check client received the write
    const Field *client_grid = collab_client_get_grid_const(&client);
    Glyph g = client_grid->buffer[15 * 64 + 25];

    if (g == 'S') {
        PASS();
    } else {
        printf(" (got '%c') ", g);
        FAIL("Client should have 'S'");
    }

    collab_client_disconnect(&client);

    field_deinit(&initial);
    collab_server_deinit(&srv);
    collab_client_deinit(&client);
}

// Test 9: Multiple clients capacity
static void test_multiple_clients(void) {
    TEST("multiple clients connect successfully");

    Collab_server srv;
    collab_server_init(&srv);

    Field initial;
    field_init_fill(&initial, 32, 64, '.');

    if (!collab_server_start(&srv, 17785, &initial)) {
        FAIL(collab_server_get_error(&srv));
        field_deinit(&initial);
        collab_server_deinit(&srv);
        return;
    }

    Collab_client clients_arr[5];
    Collab_client *clients[5];
    char names[5][32];

    for (int i = 0; i < 5; i++) {
        collab_client_init(&clients_arr[i]);
        clients[i] = &clients_arr[i];
        snprintf(names[i], 32, "Client%d", i + 1);
    }

    // Connect all clients
    for (int i = 0; i < 5; i++) {
        if (!collab_client_connect(clients[i], "127.0.0.1", 17785, names[i])) {
            printf(" (failed to connect client %d) ", i + 1);
            FAIL(collab_client_get_error(clients[i]));
            goto cleanup;
        }
        run_updates(&srv, clients, i + 1, 50);
    }

    run_updates(&srv, clients, 5, 100);

    // Check all clients are active
    bool all_active = true;
    for (int i = 0; i < 5; i++) {
        if (!collab_client_is_active(clients[i])) {
            all_active = false;
            break;
        }
    }

    if (all_active && collab_server_get_client_count(&srv) == 5) {
        PASS();
    } else {
        FAIL("Not all clients active");
    }

cleanup:
    for (int i = 0; i < 5; i++) {
        collab_client_disconnect(clients[i]);
        collab_client_deinit(clients[i]);
    }

    field_deinit(&initial);
    collab_server_deinit(&srv);
}

// Test 10: Client disconnect is handled
static void test_client_disconnect(void) {
    TEST("client disconnect is handled properly");

    Collab_server srv;
    collab_server_init(&srv);

    Field initial;
    field_init_fill(&initial, 32, 64, '.');

    if (!collab_server_start(&srv, 17786, &initial)) {
        FAIL(collab_server_get_error(&srv));
        field_deinit(&initial);
        collab_server_deinit(&srv);
        return;
    }

    Collab_client client;
    collab_client_init(&client);

    if (!collab_client_connect(&client, "127.0.0.1", 17786, "DisconnectTest")) {
        FAIL(collab_client_get_error(&client));
        field_deinit(&initial);
        collab_server_deinit(&srv);
        collab_client_deinit(&client);
        return;
    }

    Collab_client *clients[] = { &client };
    run_updates(&srv, clients, 1, 100);

    if (collab_server_get_client_count(&srv) != 1) {
        FAIL("Server should have 1 client");
        field_deinit(&initial);
        collab_server_deinit(&srv);
        collab_client_deinit(&client);
        return;
    }

    // Disconnect client
    collab_client_disconnect(&client);
    run_updates(&srv, NULL, 0, 50);

    if (collab_server_get_client_count(&srv) == 0) {
        PASS();
    } else {
        FAIL("Server should have 0 clients after disconnect");
    }

    field_deinit(&initial);
    collab_server_deinit(&srv);
    collab_client_deinit(&client);
}

int main(void) {
    printf("Running integration tests...\n\n");

    // Set log level to suppress normal output during tests
    collab_log_set_level(COLLAB_LOG_ERROR);

    test_server_lifecycle();
    test_client_connect();
    test_full_sync();
    test_client_write();
    test_two_clients();
    test_batch_write();
    test_hash_resync();
    test_server_write();
    test_multiple_clients();
    test_client_disconnect();

    printf("\n=================================\n");
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("=================================\n");

    return tests_failed > 0 ? 1 : 0;
}
